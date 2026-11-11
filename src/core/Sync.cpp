#include "mail/Sync.h"
#include "mail/Mime.h"
#include <algorithm>
#include <ctime>
#include <set>

namespace mail {
SyncReport syncAccount(Store& store,const Account& account,const std::string& password,
    const std::atomic<bool>& cancelled,const SyncProgressFunction& progress)
{
    SyncReport report;
    auto checkCancelled=[&] {
        if (cancelled) throw NetworkError(NetworkFailure::Cancelled,"Checking mail was cancelled.");
    };
    if (progress) progress({"Connecting to "+account.name+"…"});
    auto provider=connectMail(account,password,cancelled);
    auto folders=provider->folders();
    if (!account.sentFolder.empty()) {
        auto count=std::count_if(folders.begin(),folders.end(),[&](const RemoteFolder& f){return f.id==account.sentFolder || f.name==account.sentFolder;});
        for (auto& folder:folders) {
            if (count==1 && (folder.id==account.sentFolder || folder.name==account.sentFolder)) folder.role="sent";
            else if (folder.role=="sent") folder.role.clear();
        }
    }
    store.saveRemoteFolders(account.id,folders);
    std::set<Id> attemptedMoves;
    auto resumeMove=[&](PendingMove pending) {
        attemptedMoves.insert(pending.change.id);
        try {
            if (pending.remote.stage=="retry") {
                auto target=std::find_if(folders.begin(),folders.end(),[&](const RemoteFolder& f){return f.id==pending.remote.destination.folder;});
                if (target==folders.end()) throw NetworkError(NetworkFailure::Unsupported,"The destination folder is unavailable. The move remains pending.");
                // The user explicitly accepted another copy. A fresh baseline
                // excludes copies from the earlier uncertain attempt; the
                // original source epoch must still match before any command.
                pending.remote=provider->planMove(pending.remote.source,*target);
                store.saveMove(pending.change,pending.remote);
            }
            auto destination=provider->resumeMove(pending.remote,store.message(pending.change.message).raw,
                [&](const RemoteMove& remote){store.saveMove(pending.change,remote);});
            store.acknowledgeMove(pending.change,destination);
            ++report.applied;
        } catch (const std::exception& error) {
            store.releaseMove(pending.change,error.what()); throw;
        }
    };
    auto applyChanges=[&] {
        // Finish any earlier copy before applying newer edits to its location.
        for (const auto& pending:store.pendingMoves(account.id)) {
            checkCancelled();
            if (attemptedMoves.count(pending.change.id)) continue;
            try { if (store.claimMove(pending)) resumeMove(pending); }
            catch (const NetworkError& error) {
                if (error.reason==NetworkFailure::Cancelled || error.reason==NetworkFailure::Connection) throw;
                report.errors.emplace_back(error.what());
            }
        }
        // Messages deleted from Trash: remove the server copy, then the cache.
        for (const auto& pending:store.pendingDeletes(account.id)) {
            checkCancelled();
            try {
                if (provider->remove(pending.location)) store.finishDelete(pending);
                else store.flagDelete(pending);
                ++report.applied;
            } catch (const NetworkError& error) {
                if (error.reason==NetworkFailure::Cancelled || error.reason==NetworkFailure::Connection) {
                    store.recordDeleteError(pending,error.what()); throw;
                }
                if (error.reason==NetworkFailure::Rejected || error.reason==NetworkFailure::Unsupported) {
                    // The server keeps the message, so it must not stay hidden.
                    store.cancelDelete(pending);
                    report.errors.emplace_back("“"+pending.subject+"” could not be deleted on the server and is shown again: "+error.what());
                } else {
                    store.recordDeleteError(pending,error.what());
                    report.errors.emplace_back(std::string("Deleting a message: ")+error.what());
                }
            }
        }
        std::set<Id> moving;
        for (const auto& pending:store.pendingMoves(account.id)) moving.insert(pending.change.message);
        auto pending=store.pendingChanges(account.id);
        // Apply flags before moves so this batch's locations remain valid.
        std::stable_sort(pending.begin(),pending.end(),[](const PendingChange& a,const PendingChange& b) {
            return (a.kind=="move") < (b.kind=="move");
        });
        for (const auto& change : pending) {
            checkCancelled();
            if (moving.count(change.message)) continue;
            try {
                if (change.kind=="read" || change.kind=="star") {
                    provider->setFlag(change.location,change.kind=="read",change.value=="1");
                    store.acknowledgeChange(change.id);
                } else if (change.kind=="move") {
                    auto target=std::find_if(folders.begin(),folders.end(),[&](const RemoteFolder& f){return f.localName()==change.value;});
                    if (target==folders.end()) throw NetworkError(NetworkFailure::Unsupported,"The account has no server folder for “"+change.value+"”. The move remains pending.");
                    if (target->id==change.location.folder) store.acknowledgeChange(change.id);
                    else {
                        if (attemptedMoves.count(change.id)) continue;
                        attemptedMoves.insert(change.id);
                        auto remote=provider->planMove(change.location,*target);
                        if (!store.beginMove(change,remote)) continue;
                        PendingMove move; move.change=change; move.remote=remote; resumeMove(move);
                        continue; // resumeMove counts the acknowledgement.
                    }
                } else throw NetworkError(NetworkFailure::Unsupported,"An unsupported message change remains pending.");
                ++report.applied;
            } catch (const NetworkError& error) {
                if (change.kind=="move") store.recordMoveError(change,error.what());
                if (error.reason==NetworkFailure::Cancelled || error.reason==NetworkFailure::Connection) throw;
                report.errors.emplace_back(error.what());
            }
        }
    };
    applyChanges();
    // Do not ingest a second local row for an unresolved destination copy, or
    // hide its original. Unaffected folders and other accounts can still sync.
    std::set<std::string> movingFolders;
    for (const auto& pending:store.pendingMoves(account.id)) {
        movingFolders.insert(pending.remote.source.folder);
        movingFolders.insert(pending.remote.destination.folder);
    }
    std::size_t step=0;
    for (const auto& folder : folders) {
        checkCancelled(); ++step;
        if (movingFolders.count(folder.id)) continue;
        auto label=account.name+" — "+folder.name;
        if (progress) progress({"Checking "+label,step,folders.size()});
        try {
            const auto snapshot=provider->snapshot(folder);
            std::vector<const RemoteMessage*> missing;
            for (const auto& entry : snapshot.messages) if (!store.findRemote(account.id,entry.key)) missing.push_back(&entry);
            std::size_t done=0;
            for (const auto* pending : missing) {
                checkCancelled();
                const auto& entry=*pending;
                if (progress) progress({"Downloading "+label,step,folders.size(),done++,missing.size()});
                auto message=parseMessage(provider->fetch(snapshot,entry));
                message.account=account.id; message.remoteKey=entry.key; message.folder=folder.localName();
                message.unread=entry.unread; message.starred=entry.starred;
                if (!message.received) message.received=static_cast<Timestamp>(std::time(nullptr));
                store.ingestRemote(message,{folder.id,snapshot.epoch,entry.locator});
                ++report.downloaded;
            }
            // Only a complete snapshot can hide disappeared IMAP messages.
            // POP3 caches remain available even when another client deletes mail.
            store.reconcileMailbox(account.id,snapshot,account.provider==Provider::Pop3);
        } catch (const NetworkError& error) {
            if (error.reason==NetworkFailure::Cancelled || error.reason==NetworkFailure::Connection) throw;
            report.errors.push_back(folder.name+": "+error.what());
        } catch (const std::exception& error) {
            report.errors.push_back(folder.name+": "+error.what());
        }
    }
    // Reconcile/upload only after mailbox snapshots, so an older snapshot can
    // never hide a newly appended copy. Delivery has already been committed;
    // errors here only affect the separate copy journal, never SMTP delivery.
    for (const auto& pending:store.sentCopies(account.id)) {
        checkCancelled();
        if (sentCopyMode(account)==SentCopyMode::Local) { store.keepSentCopyLocal(pending.id); continue; }
        auto copy=store.claimSentCopy(pending.id); if (!copy.id) continue;
        bool mayAppend=copy.mayAppend,confirmed=copy.confirmed,uploading=false;
        try {
            std::vector<RemoteFolder> targets;
            for (const auto& folder:folders) {
                bool match=!copy.destination.empty()?folder.id==copy.destination:
                    (!account.sentFolder.empty()?(folder.id==account.sentFolder || folder.name==account.sentFolder):folder.role=="sent");
                if (match) targets.push_back(folder);
            }
            if (targets.size()!=1) throw NetworkError(NetworkFailure::Unsupported,"Choose one server Sent folder in Accounts. The delivered message is kept locally.");
            const auto& folder=targets.front();
            if (progress) progress({"Checking Sent copy — "+account.name});
            auto found=provider->findMessage(folder,copy.messageId);
            if (found) { store.bindSentCopy(copy.id,*found); continue; }
            if (sentCopyMode(account)==SentCopyMode::Server) {
                store.releaseSentCopy(copy.id,"Waiting for the server's Sent copy. Mail will check again without uploading.",mayAppend,confirmed); continue;
            }
            if (!mayAppend || confirmed) {
                store.releaseSentCopy(copy.id,confirmed?"The server accepted the Sent copy. Its identifier is not available yet.":"The server copy is still unconfirmed. Check the server before choosing Retry saving copy.",mayAppend,confirmed); continue;
            }
            const auto message=store.message(copy.id);
            store.beginSentUpload(copy.id,folder.id); uploading=true;
            auto location=provider->append(folder,message.raw,message.received);
            confirmed=true; mayAppend=false;
            // Persist acceptance even if recording the returned identity fails.
            store.releaseSentCopy(copy.id,"The server accepted the Sent copy. Checking its identifier on the next mail check.",false,true);
            if (location) store.bindSentCopy(copy.id,*location);
        } catch (const NetworkError& error) {
            if (error.reason==NetworkFailure::CopyUnknown) mayAppend=false;
            store.releaseSentCopy(copy.id,error.what(),mayAppend,confirmed);
            report.errors.emplace_back(std::string("Sent copy: ")+error.what());
            // A broken/ambiguous stream must not be reused for another command.
            if (error.reason==NetworkFailure::Cancelled || error.reason==NetworkFailure::Connection || error.reason==NetworkFailure::CopyUnknown) throw;
        } catch (const std::exception& error) {
            store.releaseSentCopy(copy.id,error.what(),mayAppend && !uploading,confirmed);
            report.errors.emplace_back(std::string("Sent copy: ")+error.what());
        }
    }
    applyChanges(); // Includes rules and local edits to reconciled Sent copies.
    return report;
}
}
