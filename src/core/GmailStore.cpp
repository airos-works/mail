#include "mail/Store.h"
#include "mail/Mime.h"
#include "StoreSql.h"
#include <algorithm>
#include <ctime>
#include <set>

namespace mail {
using namespace storage;
namespace {
void require(bool condition, const char* message) { if (!condition) throw std::invalid_argument(message); }
void identifier(const std::string& id)
{
    require(!id.empty() && id.size() <= 256 && id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") == std::string::npos,
        "Invalid Gmail identifier.");
}
void gmailAccount(sqlite3* db, Id account)
{
    Statement query(db, "SELECT provider FROM accounts WHERE id=?"); query.bind(1, account);
    require(query.step() && query.number(0) == static_cast<Id>(Provider::Gmail), "This is not a Gmail account.");
}
std::string role(const std::string& label)
{
    if (label == "INBOX") return "inbox";
    if (label == "SENT") return "sent";
    if (label == "DRAFT") return "drafts";
    if (label == "TRASH") return "trash";
    if (label == "SPAM") return "junk";
    return {};
}
std::string primaryFolder(const std::set<std::string>& labels)
{
    for (const auto* label : {"TRASH", "SPAM", "INBOX", "DRAFT", "SENT"}) if (labels.count(label)) return role(label);
    return "archive";
}
void replaceLabels(sqlite3* db, Id message, const std::set<std::string>& labels)
{
    Statement clear(db, "DELETE FROM gmail_message_labels WHERE message_id=?"); clear.bind(1, message); clear.step();
    for (const auto& label : labels) {
        identifier(label);
        Statement add(db, "INSERT INTO gmail_message_labels VALUES(?,?)"); add.bind(1, message); add.bind(2, label); add.step();
    }
}
std::string userLabel(sqlite3* db, Id account, const std::string& name)
{
    Statement find(db, "SELECT label_id FROM gmail_labels WHERE account_id=? AND name=? AND system=0");
    find.bind(1, account); find.bind(2, name); return find.step() ? find.text(0) : "";
}
std::string decimal(std::string value)
{ auto start = value.find_first_not_of('0'); return start == std::string::npos ? "0" : value.substr(start); }
}
void Store::saveGmailLabels(Id account, const std::vector<GmailLabel>& labels)
{
    std::lock_guard<std::mutex> guard(mutex_); Transaction transaction(db_); gmailAccount(db_, account);
    require(labels.size() <= 10000, "Too many Gmail labels.");
    for (const auto* sql : {"DELETE FROM gmail_labels WHERE account_id=?", "DELETE FROM remote_folders WHERE account_id=?"}) {
        Statement clear(db_, sql); clear.bind(1, account); clear.step();
    }
    for (const auto& label : labels) {
        identifier(label.id);
        require(!label.name.empty() && label.name.size() <= 1024 && label.name.find('\0') == std::string::npos, "Invalid Gmail label name.");
        Statement insert(db_, "INSERT INTO gmail_labels VALUES(?,?,?,?)");
        insert.bind(1, account); insert.bind(2, label.id); insert.bind(3, label.name); insert.bind(4, label.system); insert.step();
        auto folder = role(label.id);
        if (!label.system || !folder.empty()) {
            Statement add(db_, "INSERT INTO remote_folders VALUES(?,?,?,?)");
            add.bind(1, account); add.bind(2, label.id); add.bind(3, label.name); add.bind(4, folder); add.step();
        }
    }
    overlayLabelChanges(account);
    Statement pending(db_, "SELECT DISTINCT m.id FROM messages m JOIN pending_changes p ON p.message_id=m.id WHERE m.account_id=?");
    pending.bind(1, account); std::vector<Id> affected;
    while (pending.step()) affected.push_back(pending.number(0));
    for (Id id : affected) applyGmailPending(id);
    transaction.commit();
}
std::vector<GmailLabel> Store::gmailLabels(Id account)
{
    std::lock_guard<std::mutex> guard(mutex_);
    Statement query(db_, "SELECT label_id,name,system FROM gmail_labels WHERE account_id=? ORDER BY label_id"); query.bind(1, account);
    std::vector<GmailLabel> result;
    while (query.step()) result.push_back({query.text(0), query.text(1), query.number(2) != 0});
    return result;
}
void Store::applyGmailPending(Id message)
{
    Statement find(db_, "SELECT m.account_id FROM messages m JOIN accounts a ON a.id=m.account_id JOIN remote_messages r ON r.message_id=m.id WHERE m.id=? AND a.provider=3");
    find.bind(1, message); if (!find.step()) return; auto account = find.number(0);
    Statement query(db_, "SELECT label_id FROM gmail_message_labels WHERE message_id=?"); query.bind(1, message);
    std::set<std::string> labels; while (query.step()) labels.insert(query.text(0));
    Statement pending(db_, "SELECT kind,value FROM pending_changes WHERE message_id=? ORDER BY id"); pending.bind(1, message);
    std::string unsupportedFolder;
    auto toggle = [&](const std::string& label, bool enabled) { if (enabled) labels.insert(label); else labels.erase(label); };
    while (pending.step()) {
        const auto kind = pending.text(0), value = pending.text(1);
        if (kind == "read") toggle("UNREAD", value != "1");
        else if (kind == "star") toggle("STARRED", value == "1");
        else if (kind == "move") {
            std::string target;
            if (value == "inbox") target = "INBOX";
            else if (value == "trash") target = "TRASH";
            else if (value == "junk") target = "SPAM";
            else if (value.compare(0, 7, "remote:") == 0) {
                Statement user(db_, "SELECT label_id FROM gmail_labels WHERE account_id=? AND label_id=? AND system=0");
                user.bind(1, account); user.bind(2, value.substr(7)); if (user.step()) target = user.text(0);
            }
            if (value == "archive" || !target.empty()) {
                for (const auto* old : {"INBOX", "TRASH", "SPAM"}) labels.erase(old);
                if (!target.empty()) labels.insert(target);
            } else unsupportedFolder = value; // Keep the journaled intent visible until resolved.
        } else if (kind.compare(0, 4, "tag:") == 0) {
            auto label = userLabel(db_, account, kind.substr(4));
            if (!label.empty()) toggle(label, value == "1");
        }
    }
    Statement deleted(db_,"SELECT label_id FROM label_changes WHERE account_id=? AND kind='delete'"); deleted.bind(1,account);
    while(deleted.step()) labels.erase(deleted.text(0));
    replaceLabels(db_, message, labels);
    Statement update(db_, "UPDATE messages SET unread=?,starred=?,folder=? WHERE id=?");
    update.bind(1, labels.count("UNREAD") != 0); update.bind(2, labels.count("STARRED") != 0);
    update.bind(3, unsupportedFolder.empty() ? primaryFolder(labels) : unsupportedFolder); update.bind(4, message); update.step();
}
Id Store::ingestGmailMessage(Id account, const GmailMessage& remote, Id acknowledgement, bool applyRules)
{
    gmailAccount(db_, account); identifier(remote.id); identifier(remote.thread);
    require(remote.labels.size() <= 10000, "Too many labels on a Gmail message.");
    std::set<std::string> labels(remote.labels.begin(), remote.labels.end());
    for (const auto& label : labels) identifier(label);
    // Downloading historic Sent, Drafts, Spam or Trash is not new inbox mail.
    // Receive rules must not resurrect old trashed messages during a full sync.
    applyRules = applyRules && labels.count("INBOX") && !labels.count("TRASH") && !labels.count("SPAM") && !labels.count("DRAFT");
    auto key = "gmail:" + remote.id; Id id = 0;
    Statement find(db_, "SELECT id FROM messages WHERE account_id=? AND remote_key=?"); find.bind(1, account); find.bind(2, key);
    if (find.step()) id = find.number(0);
    bool created = !id;
    if (created || !remote.raw.empty()) {
        require(!remote.raw.empty(), "A new Gmail message needs its body.");
        auto message = parseMessage(remote.raw); message.account = account; message.remoteKey = key; message.providerThread = remote.thread;
        message.folder = primaryFolder(labels); message.unread = labels.count("UNREAD"); message.starred = labels.count("STARRED");
        if (remote.received) message.received = remote.received;
        if (!message.received) message.received = std::time(nullptr);
        id = ingestMessage(message, applyRules);
    } else {
        Statement thread(db_, "SELECT provider_thread FROM messages WHERE id=?"); thread.bind(1, id); thread.step();
        if (thread.text(0) != remote.thread) {
            auto message = readMessage(id); message.providerThread = remote.thread; ingestMessage(message, false);
        }
    }
    Statement location(db_, "INSERT INTO remote_messages VALUES(?,'gmail','',?,1) ON CONFLICT(message_id) DO UPDATE SET folder='gmail',epoch='',locator=excluded.locator,present=1");
    location.bind(1, id); location.bind(2, remote.id); location.step();
    if (created && applyRules) {
        auto stored = readMessage(id);
        if (stored.unread != (labels.count("UNREAD") != 0)) queueChange(id, "read", stored.unread ? "0" : "1");
        if (stored.starred != (labels.count("STARRED") != 0)) queueChange(id, "star", stored.starred ? "1" : "0");
        if (stored.folder != primaryFolder(labels)) queueChange(id, "move", stored.folder);
        for (const auto& name : stored.tags) {
            auto label = userLabel(db_, account, name);
            if (label.empty() || !labels.count(label)) queueChange(id, "tag:" + name, "1");
            else { Statement clear(db_, "DELETE FROM tags WHERE message_id=? AND name=?"); clear.bind(1, id); clear.bind(2, name); clear.step(); }
        }
    }
    if (acknowledgement) {
        Statement operation(db_, "SELECT kind FROM pending_changes WHERE id=? AND message_id=?"); operation.bind(1, acknowledgement); operation.bind(2, id);
        if (operation.step()) {
            auto kind = operation.text(0);
            if (kind.compare(0, 4, "tag:") == 0) {
                Statement clear(db_, "DELETE FROM tags WHERE message_id=? AND name=?"); clear.bind(1, id); clear.bind(2, kind.substr(4)); clear.step();
            }
            Statement done(db_, "DELETE FROM pending_changes WHERE id=?"); done.bind(1, acknowledgement); done.step();
        }
    }
    replaceLabels(db_, id, labels);
    if (!created) restoreGmailDeleted(id, labels);
    // A just-created message already stored exactly these flags, unless rules
    // queued edits or a label removal needs overlaying; re-ingesting metadata
    // must still reconcile through applyGmailPending below.
    if (created) {
        Statement waiting(db_, "SELECT 1 FROM pending_changes WHERE message_id=? LIMIT 1"); waiting.bind(1, id);
        Statement deleted(db_, "SELECT 1 FROM label_changes WHERE account_id=? AND kind='delete' LIMIT 1"); deleted.bind(1, account);
        if (!waiting.step() && !deleted.step()) return id;
    }
    applyGmailPending(id); return id;
}
Id Store::ingestGmail(Id account, const GmailMessage& message, Id acknowledgement)
{
    std::lock_guard<std::mutex> guard(mutex_); Transaction transaction(db_);
    auto id = ingestGmailMessage(account, message, acknowledgement, true); transaction.commit(); return id;
}
std::vector<Id> Store::ingestGmailBatch(Id account, const std::vector<GmailMessage>& messages)
{
    std::lock_guard<std::mutex> guard(mutex_); gmailAccount(db_, account);
    std::vector<Id> ids(messages.size(), 0);
    if (messages.empty()) return ids;
    // synchronous=NORMAL keeps WAL integrity; a crash at worst loses part of
    // the just-downloaded batch, which the next check downloads again.
    exec(db_, "PRAGMA synchronous=NORMAL");
    try {
        Transaction transaction(db_);
        for (std::size_t i = 0; i < messages.size(); ++i) {
            const auto& remote = messages[i];
            if (std::find(remote.labels.begin(), remote.labels.end(), "DRAFT") != remote.labels.end()) {
                removeGmailMessageLocked(account, remote.id);
                continue;
            }
            ids[i] = ingestGmailMessage(account, remote, 0, true);
        }
        transaction.commit();
    } catch (...) {
        sqlite3_exec(db_, "PRAGMA synchronous=FULL", nullptr, nullptr, nullptr);
        throw;
    }
    exec(db_, "PRAGMA synchronous=FULL");
    return ids;
}
std::vector<std::string> Store::cachedGmailIds(Id account)
{
    std::lock_guard<std::mutex> guard(mutex_);
    Statement query(db_, "SELECT r.locator FROM remote_messages r JOIN messages m ON m.id=r.message_id WHERE m.account_id=? AND r.folder='gmail' AND r.present=1 ORDER BY m.id");
    query.bind(1, account); std::vector<std::string> result; while (query.step()) result.push_back(query.text(0)); return result;
}
std::string Store::gmailHistory(Id account)
{
    std::lock_guard<std::mutex> guard(mutex_); Statement query(db_, "SELECT history FROM gmail_state WHERE account_id=?"); query.bind(1, account);
    return query.step() ? query.text(0) : "";
}
// Gmail keeps a message deleted here in its Trash for up to 30 days. Taken out
// of Trash from another client meanwhile, it is wanted again: show it.
void Store::restoreGmailDeleted(Id message, const std::set<std::string>& labels)
{
    if (labels.count("TRASH")) return;
    Statement trashing(db_, "SELECT 1 FROM pending_changes WHERE message_id=? AND kind='move' AND value='trash'"); trashing.bind(1, message);
    if (trashing.step()) return; // Our own move to Trash has not reached Gmail yet.
    Statement restore(db_, "DELETE FROM deleted_messages WHERE message_id=? AND state='local'"); restore.bind(1, message); restore.step();
}
void Store::removeGmailMessageLocked(Id account, const std::string& remoteId)
{
    identifier(remoteId);
    Statement find(db_, "SELECT m.id FROM messages m JOIN remote_messages r ON r.message_id=m.id WHERE m.account_id=? AND m.remote_key=? AND r.folder='gmail'");
    find.bind(1, account); find.bind(2, "gmail:" + remoteId); if (!find.step()) return;
    auto id = find.number(0);
    Statement deleted(db_, "SELECT 1 FROM deleted_messages WHERE message_id=?"); deleted.bind(1, id);
    if (deleted.step()) {
        // Deleted here and now gone from Gmail too: nothing is left to keep.
        Statement remove(db_, "DELETE FROM messages WHERE id=?"); remove.bind(1, id); remove.step(); return;
    }
    Statement hide(db_, "UPDATE remote_messages SET present=0 WHERE message_id=?"); hide.bind(1, id); hide.step();
    Statement clear(db_, "DELETE FROM pending_changes WHERE message_id=?"); clear.bind(1, id); clear.step();
}
void Store::removeGmailMessage(Id account, const std::string& remoteId)
{
    std::lock_guard<std::mutex> guard(mutex_); Transaction transaction(db_); gmailAccount(db_, account);
    removeGmailMessageLocked(account, remoteId); transaction.commit();
}
void Store::finishGmailSync(Id account, const std::string& history, const std::vector<std::string>& deleted)
{
    require(!history.empty() && history.size() <= 32 && history.find_first_not_of("0123456789") == std::string::npos, "Invalid Gmail history cursor.");
    std::lock_guard<std::mutex> guard(mutex_); Transaction transaction(db_); gmailAccount(db_, account);
    Statement current(db_, "SELECT history FROM gmail_state WHERE account_id=?"); current.bind(1, account);
    if (current.step()) {
        auto old = decimal(current.text(0)), next = decimal(history);
        require(next.size() > old.size() || (next.size() == old.size() && next >= old), "Gmail history advanced during this check. Check mail again.");
    }
    for (const auto& id : deleted) removeGmailMessageLocked(account, id);
    Statement save(db_, "INSERT INTO gmail_state VALUES(?,?) ON CONFLICT(account_id) DO UPDATE SET history=excluded.history");
    save.bind(1, account); save.bind(2, history); save.step(); transaction.commit();
}
}
