#include "mail/Sync.h"
#include "mail/Mime.h"
#include <filesystem>
#include <iostream>
#include <sqlite3.h>
#include <unistd.h>

// Permanent deletion from Trash: the store rules for every kind of account,
// then IMAP removal against the stateful move fixture over real TLS.
using namespace mail;
namespace {
int checks=0;
void expect(bool value,const char* message) { ++checks; if (!value) throw std::runtime_error(message); }
template<class F> void rejects(F action,const char* message)
{
    bool rejected=false; try { action(); } catch (const std::exception&) { rejected=true; } expect(rejected,message);
}
const Timestamp kNow=2000000000;
std::size_t count(Store& store,Id account,const std::string& folder)
{ Query q; q.account=account; q.folder=folder; q.includeSnoozed=true; return store.conversations(q,kNow).size(); }
Message mail(Id account,const std::string& name,const std::string& folder)
{
    auto message=parseMessage("Message-ID: <"+name+"@example.test>\r\nSubject: Delete "+name+"\r\n\r\nSecret "+name+" text\r\n");
    message.account=account; message.folder=folder; message.remoteKey=name; return message;
}
Account account(Store& store,const std::string& name,Provider provider)
{
    Account a; a.name=name; a.email=name+"@example.test"; a.provider=provider;
    if (provider==Provider::Imap || provider==Provider::Pop3) { a.incoming={"example.test",993,Security::TLS,a.email,""}; a.outgoing=a.incoming; }
    a.id=store.saveAccount(a); return a;
}
void localMail(const std::filesystem::path& directory)
{
    Store store((directory/"local.db").string()); auto a=account(store,"local",Provider::Local);
    auto kept=store.ingest(mail(a.id,"kept","inbox")),gone=store.ingest(mail(a.id,"gone","inbox"));
    expect(store.deletePermanently({kept,gone})==0 && count(store,a.id,"inbox")==2,"mail outside Trash is never deleted");
    store.move(gone,"trash"); store.tag(gone,"Keep",true);
    expect(store.trashed(a.id)==std::vector<Id>{gone} && store.trashed(0)==std::vector<Id>{gone},"Trash lists its visible messages");
    expect(store.deletePermanently({kept,gone,gone})==1,"only the trashed message is deleted, once");
    expect(count(store,a.id,"trash")==0 && count(store,a.id,"inbox")==1 && store.trashed(a.id).empty(),"deleted local mail leaves every view");
    expect(!store.findRemote(a.id,"gone") && store.findRemote(a.id,"kept")==kept,"local mail without a server copy is removed at once");
    Query search; search.account=a.id; search.folder="trash"; search.text="Secret"; search.includeSnoozed=true;
    expect(store.conversations(search,kNow).empty(),"deleted text is no longer searchable");
    expect(store.pendingDeletes(a.id).empty(),"local deletion owes the server nothing");
}
void gmailMail(const std::filesystem::path& directory)
{
    Store store((directory/"gmail.db").string()); auto a=account(store,"gmail",Provider::Gmail);
    store.saveGmailLabels(a.id,{{"INBOX","Inbox",true},{"SENT","Sent",true},{"DRAFT","Drafts",true},{"TRASH","Trash",true},
        {"SPAM","Spam",true},{"UNREAD","Unread",true},{"STARRED","Starred",true}});
    GmailMessage remote{"m1","thread-m1","100",{"INBOX","UNREAD"},1792542600,
        "From: sender@example.test\r\nSubject: Gmail delete\r\nMessage-ID: <m1@example.test>\r\n\r\nHello\r\n"};
    auto id=store.ingestGmail(a.id,remote);
    store.move(id,"trash"); expect(count(store,a.id,"trash")==1,"trashed Gmail message is listed in Trash");
    expect(store.deletePermanently({id})==1 && count(store,a.id,"trash")==0 && count(store,a.id,"inbox")==0,"deleted Gmail message is hidden at once");
    expect(store.thread(store.message(id).thread).empty(),"deleted Gmail message leaves its conversation");
    auto changes=store.pendingChanges(a.id);
    expect(changes.size()==2 || changes.size()==1,"the move to Gmail's Trash is still owed to the server");
    bool move=false; for (const auto& change:changes) move|=change.kind=="move" && change.value=="trash";
    expect(move,"deleting keeps the queued move to Gmail's Trash");
    auto metadata=remote; metadata.raw.clear();
    store.ingestGmail(a.id,metadata); // An older snapshot, before the move reached Gmail.
    expect(count(store,a.id,"trash")==0 && count(store,a.id,"inbox")==0,"an older Gmail snapshot cannot show a deleted message");
    auto trashed=metadata; trashed.labels={"TRASH"};
    for (const auto& change:store.pendingChanges(a.id)) store.ingestGmail(a.id,trashed,change.id);
    expect(store.pendingChanges(a.id).empty() && count(store,a.id,"trash")==0,"the message stays hidden while Gmail keeps it in Trash");
    expect(store.findRemote(a.id,"gmail:m1")==id,"the hidden message keeps Gmail from being downloaded again");
    expect(store.cachedGmailIds(a.id)==std::vector<std::string>{"m1"},"a later check needs no second download of the body");
    { Store reopened((directory/"gmail.db").string()); expect(count(reopened,a.id,"trash")==0,"deletion survives reopening Mail"); }
    auto restored=metadata; restored.labels={"INBOX"};
    store.ingestGmail(a.id,restored);
    expect(count(store,a.id,"inbox")==1 && store.message(id).raw==remote.raw,"a message restored from Trash by another client is shown again, intact");
    store.move(id,"trash"); store.deletePermanently({id}); store.ingestGmail(a.id,trashed,store.pendingChanges(a.id).front().id);
    store.finishGmailSync(a.id,"200",{"m1"});
    expect(!store.findRemote(a.id,"gmail:m1"),"once Gmail empties its Trash the deleted message leaves the cache");
}
void popMail(const std::filesystem::path& directory)
{
    Store store((directory/"pop.db").string()); auto a=account(store,"pop",Provider::Pop3);
    RemoteFolder inbox{"INBOX","Inbox","inbox"};
    auto id=store.ingestRemote(mail(a.id,"pop3:one","inbox"),{"INBOX","","1"});
    auto other=store.ingestRemote(mail(a.id,"pop3:two","inbox"),{"INBOX","","2"});
    store.move(id,"trash");
    expect(store.deletePermanently({id})==1 && count(store,a.id,"trash")==0,"deleted POP3 message is hidden");
    auto blank=store.message(id);
    expect(blank.raw.empty() && blank.text.empty() && blank.subject.empty(),"deleted POP3 mail keeps no content on this computer");
    expect(store.findRemote(a.id,"pop3:one")==id && store.pendingDeletes(a.id).empty() && store.pendingChanges(a.id).empty(),
        "the server's retained copy is remembered, with nothing owed to the server");
    MailboxSnapshot snapshot; snapshot.folder=inbox; snapshot.messages={{"pop3:one","1"},{"pop3:two","2"}};
    store.reconcileMailbox(a.id,snapshot,true);
    expect(count(store,a.id,"trash")==0 && store.findRemote(a.id,"pop3:one")==id,"a check never shows or downloads the retained copy again");
    snapshot.messages={{"pop3:two","1"}}; store.reconcileMailbox(a.id,snapshot,true);
    expect(!store.findRemote(a.id,"pop3:one") && store.findRemote(a.id,"pop3:two")==other && count(store,a.id,"inbox")==1,
        "the record goes when the server drops the message; other cached mail stays");
}
void imapStore(const std::filesystem::path& directory)
{
    auto path=(directory/"imap.db").string(); Store store(path); auto a=account(store,"imap",Provider::Imap);
    store.saveRemoteFolders(a.id,{{"INBOX","Inbox","inbox"},{"Archive","Archive","archive"},{"Trash","Trash","trash"}});
    auto m=mail(a.id,"x","trash"); m.remoteKey=imapRemoteKey({"Trash","88","4"});
    auto id=store.ingestRemote(m,{"Trash","88","4"});
    store.setStarred(id,true);
    expect(store.deletePermanently({id})==1 && count(store,a.id,"trash")==0,"deleted IMAP message is hidden at once");
    expect(store.pendingChanges(a.id).empty(),"flag changes of a deleted message are not sent");
    auto owed=store.pendingDeletes(a.id);
    expect(owed.size()==1 && owed[0].message==id && owed[0].location.folder=="Trash" && owed[0].location.epoch=="88" && owed[0].location.locator=="4"
        && owed[0].subject=="Delete x","the server removal is journaled with the message's exact location");
    store.recordDeleteError(owed[0],"Connection lost");
    { Store reopened(path); auto again=reopened.pendingDeletes(a.id);
      expect(again.size()==1 && again[0].error=="Connection lost" && count(reopened,a.id,"trash")==0,"the journal and its error survive reopening Mail"); }
    MailboxSnapshot snapshot; snapshot.folder={"Trash","Trash","trash"}; snapshot.epoch="88"; snapshot.messages={{m.remoteKey,"4",true,false}};
    store.reconcileMailbox(a.id,snapshot,false);
    expect(count(store,a.id,"trash")==0 && store.findRemote(a.id,m.remoteKey)==id,"a mailbox listing cannot bring the deleted message back");
    store.cancelDelete(owed[0]);
    expect(count(store,a.id,"trash")==1 && store.pendingDeletes(a.id).empty() && store.message(id).raw==m.raw,"a refused removal shows the message again, intact");
    store.deletePermanently({id}); owed=store.pendingDeletes(a.id); store.flagDelete(owed[0]);
    expect(store.pendingDeletes(a.id).empty() && count(store,a.id,"trash")==0,"a message only marked \\Deleted stays hidden without repeating the command");
    store.reconcileMailbox(a.id,snapshot,false);
    expect(store.findRemote(a.id,m.remoteKey)==id,"the marked message is remembered while the server lists it");
    snapshot.messages.clear(); store.reconcileMailbox(a.id,snapshot,false);
    expect(!store.findRemote(a.id,m.remoteKey),"the record goes once another client expunges the message");
    // A move under way finishes before the removal, at the new location.
    auto n=mail(a.id,"y","inbox"); n.remoteKey=imapRemoteKey({"INBOX","41","7"});
    auto moving=store.ingestRemote(n,{"INBOX","41","7"}); store.move(moving,"trash");
    auto change=store.pendingChanges(a.id).front();
    expect(store.beginMove(change,{change.location,{"Trash","88",""},"5","started",false}),"move journal started");
    expect(store.deletePermanently({moving})==1 && store.pendingDeletes(a.id).empty(),"removal waits for a move that already started");
    store.acknowledgeMove(change,{"Trash","88","5"});
    owed=store.pendingDeletes(a.id);
    expect(owed.size()==1 && owed[0].location.folder=="Trash" && owed[0].location.locator=="5" && count(store,a.id,"trash")==0,
        "after the move the removal targets the destination copy, still hidden");
    store.finishDelete(owed[0]);
    expect(!store.findRemote(a.id,imapRemoteKey({"Trash","88","5"})) && store.pendingDeletes(a.id).empty(),"confirmed removal clears the cache");
    store.removeAccount(a.id);
}
void migrate(const std::filesystem::path& directory)
{
    auto path=(directory/"schema15.db").string(); Id id=0,accountId=0;
    { Store store(path); auto a=account(store,"old",Provider::Local); accountId=a.id; id=store.ingest(mail(a.id,"old","trash")); }
    sqlite3* db=nullptr; expect(sqlite3_open(path.c_str(),&db)==SQLITE_OK,"open schema fixture");
    auto result=sqlite3_exec(db,"DROP TABLE deleted_messages; PRAGMA user_version=15",nullptr,nullptr,nullptr);
    sqlite3_close(db); expect(result==SQLITE_OK,"prepare schema-15 database");
    Store store(path);
    expect(count(store,accountId,"trash")==1 && store.message(id).subject=="Delete old","schema migration keeps existing Trash");
    expect(store.deletePermanently({id})==1 && count(store,accountId,"trash")==0,"migrated database deletes from Trash");
}
void check(Store& store,const Account& account,const std::atomic<bool>& cancel,bool success)
{
    try {
        auto report=syncAccount(store,account,"test-password",cancel);
        if (success!=report.errors.empty()) for (const auto& error:report.errors) std::cerr<<account.name<<": "<<error<<'\n';
        expect(success==report.errors.empty(),"unexpected sync report");
    } catch (const NetworkError& error) {
        if (success || error.reason!=NetworkFailure::Connection) std::cerr<<account.name<<": "<<error.what()<<'\n';
        expect(!success && error.reason==NetworkFailure::Connection,"unexpected connection failure");
    }
}
}
int main(int argc,char** argv)
{
    if (argc!=9) return 2; // Host, certificate and the fixture port, as for the other protocol tests.
    char temporary[]="/tmp/mail-delete-XXXXXX";
    if (!mkdtemp(temporary)) return 2;
    const std::filesystem::path directory(temporary);
    try {
        localMail(directory); gmailMail(directory); popMail(directory); imapStore(directory); migrate(directory);
        std::atomic<bool> cancel{false};
        // The fixture account starts with INBOX UID 1 and an unrelated INBOX
        // message (UID 99) another client marked \Deleted.
        for (const std::string scenario:{"delete_trash","delete_queued","delete_no_uidplus","store_disconnect","expunge_disconnect"}) {
            auto path=(directory/(scenario+".db")).string(); Account a; Id id=0;
            const bool viaTrash=scenario=="delete_trash";
            const bool interrupted=scenario=="store_disconnect" || scenario=="expunge_disconnect";
            const auto inbox=imapRemoteKey({"INBOX","41","1"}),trash=imapRemoteKey({"Trash","88","1"});
            {
                Store store(path); a.name=scenario; a.email="user@example.test"; a.provider=Provider::Imap;
                a.incoming={argv[1],std::stoi(argv[3]),Security::TLS,scenario,argv[2]}; a.outgoing=a.incoming; a.id=store.saveAccount(a);
                check(store,a,cancel,true);
                id=store.findRemote(a.id,inbox); expect(id!=0,"source downloaded");
                auto unrelated=store.findRemote(a.id,imapRemoteKey({"INBOX","41","99"})); expect(unrelated!=0,"unrelated message downloaded");
                store.move(id,"trash");
                if (viaTrash) { check(store,a,cancel,true); expect(store.findRemote(a.id,trash)==id,"message reached the server's Trash"); }
                expect(store.deletePermanently({id})==1 && count(store,a.id,"trash")==0,"message leaves Trash at once");
                expect(store.pendingDeletes(a.id).size()==1,"server removal is journaled");
                check(store,a,cancel,!interrupted);
                expect(count(store,a.id,"trash")==0 && count(store,a.id,"inbox")==1,"deleted message stays hidden; unrelated mail stays listed");
                if (interrupted) expect(store.pendingDeletes(a.id).size()==1 && !store.pendingDeletes(a.id).front().error.empty(),
                    "an interrupted removal stays journaled with its error");
            }
            {
                // Reopen SQLite and connect again, retaining only disk state.
                Store store(path); store.recoverOutgoing();
                check(store,a,cancel,true);
                expect(store.pendingDeletes(a.id).empty() && store.pendingChanges(a.id).empty() && store.pendingMoves(a.id).empty(),"nothing remains owed to the server");
                expect(count(store,a.id,"trash")==0 && count(store,a.id,"inbox")==1,"deleted message never returns after another check");
                const auto key=viaTrash?trash:inbox;
                if (scenario=="delete_no_uidplus") {
                    expect(store.findRemote(a.id,key)==id,"without UIDPLUS the marked message is remembered, not downloaded again");
                    check(store,a,cancel,true);
                    expect(count(store,a.id,"inbox")==1 && count(store,a.id,"trash")==0,"the marked message stays hidden on later checks");
                } else expect(!store.findRemote(a.id,key),"the removed message leaves the cache");
                expect(store.findRemote(a.id,imapRemoteKey({"INBOX","41","99"}))!=0,"unrelated \\Deleted mail of another client is kept");
            }
            std::cout<<"Delete scenario passed: "<<scenario<<std::endl;
        }
        // Haiku's directory iteration can skip entries while they are being
        // removed: repeat until nothing is left, and never fail the checks for it.
        std::error_code ignored;
        for (int i=0;i<5 && std::filesystem::exists(directory,ignored);++i) std::filesystem::remove_all(directory,ignored);
        std::cout<<checks<<" permanent deletion checks passed\n"; return 0;
    } catch (const std::exception& error) {
        std::cerr<<"FAIL: "<<error.what()<<" ("<<directory<<")\n";
        return 1;
    }
}
