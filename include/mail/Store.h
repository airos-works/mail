#pragma once

#include "Model.h"
#include "Provider.h"
#include "Compose.h"
#include "Gmail.h"
#include <mutex>
#include <set>

struct sqlite3;
namespace mail {
struct PendingChange {
    Id id = 0;
    Id message = 0;
    Id account = 0;
    std::string kind; // read, star, move, or Gmail tag:<name>
    std::string value;
    RemoteLocation location;
};
struct PendingMove {
    PendingChange change;
    RemoteMove remote;
    bool active=false;
    Id revision=0;
    std::string error,subject,accountName;
};
// A message deleted from Trash whose server copy is still to be removed.
struct PendingDelete {
    Id message=0,account=0;
    RemoteLocation location;
    std::string state,error,subject;
};
struct LabelChange {
    Id id=0,account=0;
    std::string label,kind,name,error;
};
struct TagInfo {
    std::string name,label; // Empty label means local tags, not a Gmail definition.
    Id messages=0;
    std::string pending,error;
};
struct SentCopy {
    Id id=0,account=0; // id is the local message ID, stable after reconciliation.
    std::string state,error,subject,messageId,destination;
    bool mayAppend=true,confirmed=false;
};
struct SnoozeReminder {
    Id id=0,message=0,thread=0,account=0;
    Timestamp due=0;
    std::string subject,accountName;
    bool notified=false;
};
struct GmailDraftState {
    Id id=0,draft=0,account=0,revision=0,savedRevision=0,attemptRevision=0;
    bool active=false;
    std::string remoteId,messageId,thread,raw;
    std::string attempt,attemptRaw,attemptThread,error; // create, update, delete, or empty
    bool conflict=false,missing=false;
    std::string observedId,observedThread,observedRaw;
};
struct GmailDraftMatch { std::string id,subject; };
// One connection, serialized across the UI and sync worker. Each ingest is atomic.
class Store {
public:
    explicit Store(const std::string& path);
    ~Store();
    Store(const Store&) = delete;
    Store& operator=(const Store&) = delete;

    Id saveAccount(const Account&);
    std::vector<Account> accounts();
    void removeAccount(Id); // Removes only the local account/cache, never server mail.
    // A provider supplies an immutable remoteKey (including IMAP UIDVALIDITY).
    // Repeated ingestion updates content but preserves the user's local actions.
    Id ingest(const Message&);
    Id findRemote(Id account, const std::string& key);
    Id ingestRemote(const Message&, const RemoteLocation&);
    void reconcileMailbox(Id account, const MailboxSnapshot&, bool retainMissing);
    void saveRemoteFolders(Id account, const std::vector<RemoteFolder>&);
    std::vector<RemoteFolder> remoteFolders(Id account);
    std::vector<PendingChange> pendingChanges(Id account);
    void acknowledgeChange(Id operation);
    void acknowledgeMove(const PendingChange&, const RemoteLocation&);
    bool beginMove(const PendingChange&, const RemoteMove&);
    std::vector<PendingMove> pendingMoves(Id account=0);
    std::vector<PendingMove> moveReviews(); // Includes queued moves, metadata only.
    bool claimMove(const PendingMove&);
    void saveMove(const PendingChange&, const RemoteMove&);
    void releaseMove(const PendingChange&, const std::string& error);
    void recordMoveError(const PendingChange&, const std::string& error);
    void retryMove(const PendingMove&,bool acceptUncertain=false);
    void keepMoveLocal(const PendingMove&);
    void cancelQueuedMove(const PendingChange&);
    void saveGmailLabels(Id account, const std::vector<GmailLabel>&);
    std::vector<GmailLabel> gmailLabels(Id account);
    Id ingestGmail(Id account, const GmailMessage&, Id acknowledgedOperation=0);
    // One lock and one transaction for a whole fetched page. Uses relaxed
    // durability: bulk sync redownloads after an interrupted commit, so this
    // never risks user edits. Messages labeled DRAFT are hidden rather than
    // cached (editable drafts sync through the draft journal). The result
    // holds the local ID per input, 0 for a hidden draft.
    std::vector<Id> ingestGmailBatch(Id account, const std::vector<GmailMessage>&);
    std::vector<std::string> cachedGmailIds(Id account);
    std::string gmailHistory(Id account);
    // Call only after all pages and message updates succeed. Deleted IDs must
    // have an authoritative 404, not merely be absent from a moving page list.
    void finishGmailSync(Id account, const std::string& history, const std::vector<std::string>& deleted);
    void removeGmailMessage(Id account, const std::string& remoteId);
    Message message(Id);
    std::vector<Message> thread(Id);
    std::vector<Conversation> conversations(const Query&, Timestamp now);
    std::size_t unreadConversations(const Query&, Timestamp now); // Conversations with unread mail in a view.
    void setRead(Id message, bool read);
    void setStarred(Id message, bool starred);
    void move(Id message, const std::string& folder);
    // Deletes messages in Trash for good and returns how many; messages
    // anywhere else are left alone. They disappear at once. A server copy is
    // removed by the next check (IMAP), or stays hidden here while the server
    // keeps it (Gmail, POP3).
    std::size_t deletePermanently(const std::vector<Id>& messages);
    std::vector<Id> trashed(Id account); // Visible messages in Trash; 0 = unified accounts.
    std::vector<PendingDelete> pendingDeletes(Id account); // IMAP removals still owed.
    void finishDelete(const PendingDelete&);  // The server no longer has the message.
    void flagDelete(const PendingDelete&);    // Marked \Deleted; another client or the server expunges it.
    void cancelDelete(const PendingDelete&);  // The server refused: the message is shown again.
    void recordDeleteError(const PendingDelete&,const std::string& error);
    void snooze(Id message, Timestamp until);
    void snooze(const std::vector<Id>& messages, Timestamp until);
    std::vector<SnoozeReminder> reminders(Timestamp now);
    void markRemindersNotified(const std::vector<Id>&);
    void dismissReminders(const std::vector<Id>&);
    void tag(Id message, const std::string& tag, bool add);
    std::vector<std::string> tags();
    std::vector<TagInfo> accountTags(Id);
    void renameTag(Id account,const std::string& oldName,const std::string& newName);
    void removeTag(Id account,const std::string& name);
    std::vector<LabelChange> labelChanges(Id account);
    void finishLabelChange(Id operation,const std::string& error={});

    Id saveSmartFolder(const SmartFolder&);
    std::vector<SmartFolder> smartFolders();
    void removeSmartFolder(Id);
    Id saveFilter(const Filter&);
    std::vector<Filter> filters();
    void removeFilter(Id);
    void moveFilter(Id,int direction); // One position up (-1) or down (+1).
    // Updates the caller's revision and generated Message-ID after committing.
    // The returned ID remains explicit so callers can reuse unsaved templates.
    Id saveDraft(Draft&);
    Draft openDraft(Id,const std::string& editor);
    void closeDraft(Id,const std::string& editor);
    Draft draft(Id);
    std::vector<Draft> drafts(Id account=0);
    void removeDraft(Id,Id expectedRevision=0);
    void prepareGmailDrafts(Id account); // Track unqueued local drafts; no network activity.
    std::vector<GmailDraftState> gmailDraftStates(Id account=0,bool includeRaw=false);
    GmailDraftState gmailDraftState(Id,bool includeRaw=true);
    Id importGmailDraft(Id account,const GmailDraft&); // Keeps original MIME without re-uploading it.
    bool observeGmailDraft(const GmailDraftState&,const GmailDraft&); // True if safe to upload local changes.
    void missingGmailDraft(const GmailDraftState&); // Authoritative GET 404; preserves local edits.
    void keepBothGmailDrafts(const GmailDraftState&); // Closed, unqueued editor; preserves both versions.
    void recordGmailDraftMatches(const GmailDraftState&,const std::vector<GmailDraft>&);
    std::vector<GmailDraftMatch> gmailDraftMatches(Id);
    GmailDraft gmailDraftMatch(Id,const std::string& remoteId);
    void useGmailDraftMatch(const GmailDraftState&,const std::string& remoteId);
    void retryGmailDraftCreate(const GmailDraftState&); // Explicit duplicate-risk decision; fresh Message-ID.
    void forgetDiscardedGmailDraft(const GmailDraftState&); // Leaves all server copies intact.
    std::string draftStatus(Id);
    GmailDraftState claimGmailDraft(const GmailDraftState&); // Empty when superseded/busy.
    // Before updating, the sync worker must verify the remote baseline and
    // resolve conflicts. This method persists exact MIME before the HTTP write.
    GmailDraftState beginGmailDraftWrite(const GmailDraftState&);
    GmailDraftState beginGmailDraftDelete(const GmailDraftState&);
    // Supply the write receipt, or a uniquely reconciled draft whose raw MIME
    // matches the saved attempt. A metadata fetch alone cannot confirm a write.
    void finishGmailDraftWrite(const GmailDraftState&,const GmailDraft&);
    void finishGmailDraftDelete(const GmailDraftState&); // Only after DELETE success or GET 404.
    // Clear an attempt only after an authoritative rejection before acceptance.
    // Interrupted requests retain their immutable snapshot for reconciliation.
    void releaseGmailDraft(const GmailDraftState&,const std::string& error,bool clearAttempt=false);
    Id enqueueDraft(Id,Timestamp,Id expectedRevision=0);
    std::vector<Outgoing> outbox(bool includeRaw=true);
    OutboxSummary outboxSummary(); // Counts without reading message bodies/attachments.
    bool readyOutgoing(); // Excludes drafts with an active or unresolved server save.
    Outgoing claimOutgoing(); // Empty ID when no queued message is available.
    void finishOutgoing(Id,const std::string& state,const std::string& error,Timestamp,const GmailSent& receipt={});
    void retryOutgoing(Id,bool acceptUncertain=false);
    Id returnToDraft(Id,bool acceptUncertain=false);
    std::vector<SentCopy> sentCopies(Id account=0); // Metadata only; excludes completed/local copies.
    SentCopy claimSentCopy(Id); // Empty if another worker claimed/finished it.
    void beginSentUpload(Id,const std::string& destination); // Durable before any literal bytes.
    void releaseSentCopy(Id,const std::string& error,bool mayAppend,bool confirmed);
    void bindSentCopy(Id,const RemoteLocation&);
    void retrySentCopy(Id,bool acceptUncertain=false);
    void keepSentCopyLocal(Id);
    void recoverOutgoing(); // Startup only: recover send/copy/move worker claims.

private:
    Id ingestMessage(const Message&,bool applyRules=true);
    Id ingestGmailMessage(Id,const GmailMessage&,Id acknowledgedOperation,bool applyRules);
    void applyGmailPending(Id message);
    void overlayLabelChanges(Id account);
    void removeGmailMessageLocked(Id,const std::string&);
    bool deleteLocked(Id message);
    void restoreGmailDeleted(Id message,const std::set<std::string>& labels);
    Draft readDraft(Id);
    Id saveDraftLocked(Draft&);
    Outgoing readOutgoing(Id,bool includeRaw=true);
    SentCopy readSentCopy(Id);
    void bindSentCopyLocked(Id,const RemoteLocation&);
    void queueChange(Id, const std::string& kind, const std::string& value);
    Message readMessage(Id);
    std::vector<Filter> readFilters();
    sqlite3* db_ = nullptr;
    std::mutex mutex_;
};
}
