#include "mail/Store.h"
#include "StoreSql.h"

// Permanent deletion of messages in Trash.
//
// A deleted_messages row hides its message from every view at once. What
// happens to the message itself depends on where the mail lives:
//  - no server copy (local, imported, a Sent copy kept on this computer): the
//    message is removed right away;
//  - IMAP and Proton Mail Bridge ("pending"): the next check removes the
//    server message (STORE \Deleted and a UID EXPUNGE of that UID only), then
//    the cached message. A server without UIDPLUS keeps it marked \Deleted
//    ("flagged"), since a plain EXPUNGE would also remove other clients'
//    deleted mail; the cache drops it once the server no longer lists it;
//  - Gmail and POP3 ("local"): the message stays hidden on this computer.
//    Gmail removes mail from its Trash itself after 30 days (permanent
//    deletion through the API needs wider access than Mail asks for), and
//    POP3 mail is always retained on the server. The hidden row keeps a
//    later check from downloading the message again.
namespace mail {
using namespace storage;

std::size_t Store::deletePermanently(const std::vector<Id>& messages)
{
    std::lock_guard<std::mutex> guard(mutex_);
    Transaction transaction(db_); std::size_t count=0;
    for (Id id:messages) if (deleteLocked(id)) ++count;
    transaction.commit(); return count;
}

std::vector<Id> Store::trashed(Id account)
{
    std::lock_guard<std::mutex> guard(mutex_);
    Statement s(db_,R"SQL(SELECT m.id FROM messages m JOIN accounts a ON a.id=m.account_id
 WHERE m.folder='trash' AND ((?1=0 AND a.unified=1) OR m.account_id=?1)
 AND NOT EXISTS(SELECT 1 FROM remote_messages r WHERE r.message_id=m.id AND r.present=0)
 AND NOT EXISTS(SELECT 1 FROM deleted_messages d WHERE d.message_id=m.id) ORDER BY m.id)SQL");
    s.bind(1,account); std::vector<Id> result;
    while (s.step()) result.push_back(s.number(0));
    return result;
}

bool Store::deleteLocked(Id id)
{
    Statement find(db_,R"SQL(SELECT m.folder,a.provider,
 EXISTS(SELECT 1 FROM remote_messages WHERE message_id=m.id),
 EXISTS(SELECT 1 FROM sent_copies WHERE message_id=m.id AND state IN('checking','uploading')),
 EXISTS(SELECT 1 FROM remote_moves WHERE message_id=m.id),
 EXISTS(SELECT 1 FROM deleted_messages WHERE message_id=m.id)
 FROM messages m JOIN accounts a ON a.id=m.account_id WHERE m.id=?)SQL");
    find.bind(1,id);
    // Only Trash is emptied; a conversation's other messages are kept.
    if (!find.step() || find.text(0)!="trash" || find.number(5)) return false;
    const auto provider=static_cast<Provider>(find.number(1));
    const bool remote=find.number(2)!=0,moving=find.number(4)!=0;
    if (find.number(3)) throw std::runtime_error("Wait for the Sent copy check to finish before deleting this message.");
    const bool imap=provider==Provider::Imap || provider==Provider::ProtonBridge;
    if (!remote || (!imap && provider!=Provider::Gmail && provider!=Provider::Pop3)) {
        Statement remove(db_,"DELETE FROM messages WHERE id=?"); remove.bind(1,id); remove.step();
        return true;
    }
    Statement wake(db_,"UPDATE messages SET snoozed_until=0 WHERE id=?"); wake.bind(1,id); wake.step();
    Statement tags(db_,"DELETE FROM tags WHERE message_id=?"); tags.bind(1,id); tags.step();
    if (imap && !moving) {
        // The message is removed where the server has it now, so a queued
        // move to Trash (or a flag change) no longer needs to reach the server.
        // A move already under way finishes first; see pendingDeletes().
        Statement clear(db_,"DELETE FROM pending_changes WHERE message_id=?"); clear.bind(1,id); clear.step();
    }
    if (provider==Provider::Pop3) {
        // Nothing can bring this message back: keep only its identity.
        Statement blank(db_,"UPDATE messages SET sender='',recipient='',cc='',subject='',body='',html='',raw=X'' WHERE id=?");
        blank.bind(1,id); blank.step();
        for (const char* sql:{"DELETE FROM attachments WHERE message_id=?","DELETE FROM message_refs WHERE message_id=?"}) {
            Statement remove(db_,sql); remove.bind(1,id); remove.step();
        }
    }
    Statement mark(db_,"INSERT INTO deleted_messages(message_id,state) VALUES(?,?)");
    mark.bind(1,id); mark.bind(2,std::string(imap?"pending":"local")); mark.step();
    return true;
}

std::vector<PendingDelete> Store::pendingDeletes(Id account)
{
    std::lock_guard<std::mutex> guard(mutex_);
    Statement s(db_,R"SQL(SELECT m.id,m.account_id,r.folder,r.epoch,r.locator,d.state,d.error,m.subject
 FROM deleted_messages d JOIN messages m ON m.id=d.message_id JOIN remote_messages r ON r.message_id=m.id
 WHERE m.account_id=? AND d.state='pending'
 AND NOT EXISTS(SELECT 1 FROM remote_moves WHERE message_id=m.id) ORDER BY m.id)SQL");
    s.bind(1,account); std::vector<PendingDelete> result;
    while (s.step()) result.push_back({s.number(0),s.number(1),{s.text(2),s.text(3),s.text(4)},s.text(5),s.text(6),s.text(7)});
    return result;
}

void Store::finishDelete(const PendingDelete& deleted)
{
    std::lock_guard<std::mutex> guard(mutex_);
    Statement s(db_,R"SQL(DELETE FROM messages WHERE id=?1
 AND EXISTS(SELECT 1 FROM deleted_messages WHERE message_id=?1)
 AND EXISTS(SELECT 1 FROM remote_messages WHERE message_id=?1 AND folder=?2 AND epoch=?3 AND locator=?4))SQL");
    s.bind(1,deleted.message); s.bind(2,deleted.location.folder); s.bind(3,deleted.location.epoch); s.bind(4,deleted.location.locator); s.step();
}

void Store::flagDelete(const PendingDelete& deleted)
{
    std::lock_guard<std::mutex> guard(mutex_);
    Statement s(db_,"UPDATE deleted_messages SET state='flagged',error='' WHERE message_id=? AND state='pending'");
    s.bind(1,deleted.message); s.step();
}

void Store::cancelDelete(const PendingDelete& deleted)
{
    std::lock_guard<std::mutex> guard(mutex_);
    Statement s(db_,"DELETE FROM deleted_messages WHERE message_id=? AND state='pending'");
    s.bind(1,deleted.message); s.step();
}

void Store::recordDeleteError(const PendingDelete& deleted,const std::string& error)
{
    std::lock_guard<std::mutex> guard(mutex_);
    Statement s(db_,"UPDATE deleted_messages SET error=? WHERE message_id=? AND state='pending'");
    s.bind(1,error); s.bind(2,deleted.message); s.step();
}
}
