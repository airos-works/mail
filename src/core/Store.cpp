#include "mail/Store.h"
#include "mail/Mime.h"
#include "StoreSql.h"

#include <sqlite3.h>
#include <algorithm>
#include <cctype>
#include <set>
#include <stdexcept>

namespace mail {
using namespace storage;
namespace {
std::string lower(std::string s)
{
    for (char& c : s) if (static_cast<unsigned char>(c) < 128) c = static_cast<char>(std::tolower(c));
    return s;
}
bool contains(const std::string& value, const std::string& needle)
{
    return lower(value).find(lower(needle)) != std::string::npos;
}
void require(bool condition, const char* message) { if (!condition) throw std::invalid_argument(message); }
void validateFolder(const std::string& folder)
{
    require(!folder.empty() && folder.size() <= 2048 && folder.find('\0') == std::string::npos, "Choose a valid folder.");
}
void validateTag(const std::string& tag)
{
    require(!tag.empty() && tag.size()<=256 && tag.find_first_not_of(' ')!=std::string::npos && std::none_of(tag.begin(),tag.end(),[](unsigned char c){return c<32 || c==127;}),
        "Enter a name between 1 and 256 bytes without control characters.");
}
void validateScope(sqlite3* db,Id account)
{
    require(account>=0,"Choose an account.");
    if (account) { Statement s(db,"SELECT 1 FROM accounts WHERE id=?"); s.bind(1,account); require(s.step(),"This account was removed. Choose an available account."); }
}
void insertTag(sqlite3* db, Id message, const std::string& tag)
{
    validateTag(tag);
    Statement stmt(db, "INSERT OR IGNORE INTO tags(message_id,name) VALUES(?,?)");
    stmt.bind(1, message); stmt.bind(2, tag); stmt.step();
}
std::string searchExpression(const std::string& input)
{
    // Treat input as words, never as FTS syntax. Quotes/operators remain literal.
    std::string out, word;
    auto flush = [&] {
        if (word.empty()) return;
        if (!out.empty()) out += " AND ";
        out += '"';
        for (char c : word) { out += c; if (c == '"') out += '"'; }
        out += '"'; word.clear();
    };
    for (unsigned char c : input) {
        if (std::isspace(c)) flush(); else word += static_cast<char>(c);
    }
    flush(); return out;
}
void bindQuery(Statement& s, int n, const Query& q)
{
    s.bind(n++, q.account); s.bind(n++, q.folder); s.bind(n++, q.text); s.bind(n++, q.tag);
    s.bind(n++, q.unread); s.bind(n++, q.starred); s.bind(n++, q.attachments);
    s.bind(n++, q.snoozedOnly); s.bind(n, q.includeSnoozed);
}
Query readQuery(Statement& s, int n)
{
    Query q;
    q.account = s.number(n++); q.folder = s.text(n++); q.text = s.text(n++); q.tag = s.text(n++);
    q.unread = s.number(n++); q.starred = s.number(n++); q.attachments = s.number(n++);
    q.snoozedOnly = s.number(n++); q.includeSnoozed = s.number(n);
    return q;
}
}

Store::Store(const std::string& path)
{
    int rc = sqlite3_open_v2(path.c_str(), &db_, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr);
    try {
        check(db_, rc);
        sqlite3_busy_timeout(db_, 5000);
        exec(db_, "PRAGMA foreign_keys=ON; PRAGMA journal_mode=WAL; PRAGMA synchronous=FULL;");
        Id currentVersion=0;
        { Statement version(db_, "PRAGMA user_version"); version.step(); currentVersion=version.number(0); }
        require(currentVersion <= 16, "This mail database needs a newer version of Mail.");
        if (currentVersion == 16) return;
        Transaction transaction(db_);
        if (currentVersion == 0) {
        exec(db_, R"SQL(
CREATE TABLE accounts (
 id INTEGER PRIMARY KEY, name TEXT NOT NULL, email TEXT NOT NULL, provider INTEGER NOT NULL,
 incoming_host TEXT NOT NULL, incoming_port INTEGER NOT NULL, incoming_security INTEGER NOT NULL,
 incoming_user TEXT NOT NULL, incoming_cert TEXT NOT NULL,
 outgoing_host TEXT NOT NULL, outgoing_port INTEGER NOT NULL, outgoing_security INTEGER NOT NULL,
 outgoing_user TEXT NOT NULL, outgoing_cert TEXT NOT NULL, unified INTEGER NOT NULL);
CREATE TABLE threads (id INTEGER PRIMARY KEY);
CREATE TABLE thread_keys (
 account_id INTEGER NOT NULL REFERENCES accounts(id), key TEXT NOT NULL,
 thread_id INTEGER NOT NULL REFERENCES threads(id), PRIMARY KEY(account_id,key));
CREATE INDEX thread_keys_thread ON thread_keys(thread_id);
CREATE TABLE messages (
 id INTEGER PRIMARY KEY, account_id INTEGER NOT NULL REFERENCES accounts(id),
 remote_key TEXT NOT NULL, thread_id INTEGER NOT NULL REFERENCES threads(id),
 provider_thread TEXT NOT NULL, message_id TEXT NOT NULL,
 sender TEXT NOT NULL, recipient TEXT NOT NULL, cc TEXT NOT NULL, subject TEXT NOT NULL,
 body TEXT NOT NULL, html TEXT NOT NULL, raw BLOB NOT NULL, folder TEXT NOT NULL,
 received INTEGER NOT NULL, unread INTEGER NOT NULL, starred INTEGER NOT NULL,
 snoozed_until INTEGER NOT NULL DEFAULT 0,
 UNIQUE(account_id,remote_key));
CREATE INDEX messages_thread ON messages(thread_id,received);
CREATE INDEX messages_folder ON messages(folder,account_id,received);
CREATE TABLE message_refs (
 message_id INTEGER NOT NULL REFERENCES messages(id) ON DELETE CASCADE,
 position INTEGER NOT NULL, reference TEXT NOT NULL, PRIMARY KEY(message_id,position));
CREATE TABLE attachments (
 id INTEGER PRIMARY KEY, message_id INTEGER NOT NULL REFERENCES messages(id) ON DELETE CASCADE,
 name TEXT NOT NULL, media_type TEXT NOT NULL, content_id TEXT NOT NULL, data BLOB NOT NULL, inline_image INTEGER NOT NULL);
CREATE INDEX attachments_message ON attachments(message_id);
CREATE TABLE tags (
 message_id INTEGER NOT NULL REFERENCES messages(id) ON DELETE CASCADE, name TEXT NOT NULL,
 PRIMARY KEY(message_id,name));
CREATE TABLE smart_folders (
 id INTEGER PRIMARY KEY, name TEXT NOT NULL, account_id INTEGER NOT NULL,
 folder TEXT NOT NULL, search TEXT NOT NULL, tag TEXT NOT NULL, unread INTEGER NOT NULL,
 starred INTEGER NOT NULL, attachments INTEGER NOT NULL, snoozed_only INTEGER NOT NULL, include_snoozed INTEGER NOT NULL);
CREATE TABLE filters (
 id INTEGER PRIMARY KEY, account_id INTEGER NOT NULL, name TEXT NOT NULL,
 sender_contains TEXT NOT NULL, subject_contains TEXT NOT NULL, add_tag TEXT NOT NULL,
 move_to TEXT NOT NULL, mark_read INTEGER NOT NULL, star INTEGER NOT NULL,
 stop INTEGER NOT NULL, enabled INTEGER NOT NULL);
CREATE VIRTUAL TABLE message_search USING fts5(subject,sender,recipient,body,content=messages,content_rowid=id,tokenize='unicode61');
CREATE TRIGGER messages_insert AFTER INSERT ON messages BEGIN
 INSERT INTO message_search(rowid,subject,sender,recipient,body) VALUES(new.id,new.subject,new.sender,new.recipient,new.body);
END;
CREATE TRIGGER messages_delete AFTER DELETE ON messages BEGIN
 INSERT INTO message_search(message_search,rowid,subject,sender,recipient,body) VALUES('delete',old.id,old.subject,old.sender,old.recipient,old.body);
END;
CREATE TRIGGER messages_update AFTER UPDATE OF subject,sender,recipient,body ON messages BEGIN
 INSERT INTO message_search(message_search,rowid,subject,sender,recipient,body) VALUES('delete',old.id,old.subject,old.sender,old.recipient,old.body);
 INSERT INTO message_search(rowid,subject,sender,recipient,body) VALUES(new.id,new.subject,new.sender,new.recipient,new.body);
END;
PRAGMA user_version=1;
)SQL");
        }
        if (currentVersion < 2) exec(db_, R"SQL(
CREATE TABLE remote_folders (
 account_id INTEGER NOT NULL REFERENCES accounts(id), remote_id TEXT NOT NULL,
 name TEXT NOT NULL, role TEXT NOT NULL, PRIMARY KEY(account_id,remote_id));
CREATE TABLE remote_messages (
 message_id INTEGER PRIMARY KEY REFERENCES messages(id) ON DELETE CASCADE,
 folder TEXT NOT NULL, epoch TEXT NOT NULL, locator TEXT NOT NULL, present INTEGER NOT NULL DEFAULT 1);
CREATE TABLE pending_changes (
 id INTEGER PRIMARY KEY AUTOINCREMENT, message_id INTEGER NOT NULL REFERENCES messages(id) ON DELETE CASCADE,
 kind TEXT NOT NULL, value TEXT NOT NULL, UNIQUE(message_id,kind));
PRAGMA user_version=2;
)SQL");
        if (currentVersion < 3) exec(db_, R"SQL(
ALTER TABLE accounts ADD COLUMN credential_id TEXT NOT NULL DEFAULT '';
UPDATE accounts SET credential_id=lower(hex(randomblob(16)));
PRAGMA user_version=3;
)SQL");
        if (currentVersion < 4) exec(db_, R"SQL(
CREATE TABLE drafts (id INTEGER PRIMARY KEY AUTOINCREMENT,account_id INTEGER NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
 message_id TEXT NOT NULL,recipient TEXT NOT NULL,cc TEXT NOT NULL,bcc TEXT NOT NULL,subject TEXT NOT NULL,body TEXT NOT NULL,
 provider_thread TEXT NOT NULL,modified INTEGER NOT NULL);
CREATE TABLE draft_runs (draft_id INTEGER NOT NULL REFERENCES drafts(id) ON DELETE CASCADE,offset INTEGER NOT NULL,
 bold INTEGER NOT NULL,italic INTEGER NOT NULL,underline INTEGER NOT NULL,link TEXT NOT NULL,PRIMARY KEY(draft_id,offset));
CREATE TABLE draft_refs (draft_id INTEGER NOT NULL REFERENCES drafts(id) ON DELETE CASCADE,position INTEGER NOT NULL,reference TEXT NOT NULL,PRIMARY KEY(draft_id,position));
CREATE TABLE draft_attachments (id INTEGER PRIMARY KEY,draft_id INTEGER NOT NULL REFERENCES drafts(id) ON DELETE CASCADE,
 name TEXT NOT NULL,media_type TEXT NOT NULL,content_id TEXT NOT NULL,data BLOB NOT NULL,inline_image INTEGER NOT NULL);
CREATE TABLE outbox (id INTEGER PRIMARY KEY AUTOINCREMENT,draft_id INTEGER UNIQUE REFERENCES drafts(id),
 account_id INTEGER NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,state TEXT NOT NULL,error TEXT NOT NULL,
 sender TEXT NOT NULL,raw BLOB NOT NULL,created INTEGER NOT NULL);
CREATE TABLE outbox_recipients (outbox_id INTEGER NOT NULL REFERENCES outbox(id) ON DELETE CASCADE,position INTEGER NOT NULL,address TEXT NOT NULL,PRIMARY KEY(outbox_id,position));
PRAGMA user_version=4;
)SQL");
        if (currentVersion < 5) exec(db_, R"SQL(
CREATE TABLE gmail_state (account_id INTEGER PRIMARY KEY REFERENCES accounts(id) ON DELETE CASCADE,history TEXT NOT NULL);
CREATE TABLE gmail_labels (account_id INTEGER NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
 label_id TEXT NOT NULL,name TEXT NOT NULL,system INTEGER NOT NULL,PRIMARY KEY(account_id,label_id));
CREATE TABLE gmail_message_labels (message_id INTEGER NOT NULL REFERENCES messages(id) ON DELETE CASCADE,
 label_id TEXT NOT NULL,PRIMARY KEY(message_id,label_id));
CREATE INDEX gmail_labels_membership ON gmail_message_labels(label_id,message_id);
CREATE VIEW effective_tags AS
 SELECT message_id,name FROM tags
 UNION
 SELECT g.message_id,l.name FROM gmail_message_labels g JOIN messages m ON m.id=g.message_id
 JOIN gmail_labels l ON l.account_id=m.account_id AND l.label_id=g.label_id WHERE l.system=0;
PRAGMA user_version=5;
)SQL");
        if (currentVersion < 6) exec(db_, R"SQL(
ALTER TABLE accounts ADD COLUMN sent_mode INTEGER NOT NULL DEFAULT 0;
ALTER TABLE accounts ADD COLUMN sent_folder TEXT NOT NULL DEFAULT '';
CREATE TABLE sent_copies (
 message_id INTEGER PRIMARY KEY REFERENCES messages(id) ON DELETE CASCADE,
 outbox_id INTEGER NOT NULL UNIQUE REFERENCES outbox(id) ON DELETE CASCADE,
 state TEXT NOT NULL DEFAULT 'pending',error TEXT NOT NULL DEFAULT '',
 destination TEXT NOT NULL DEFAULT '',may_append INTEGER NOT NULL DEFAULT 1,
 confirmed INTEGER NOT NULL DEFAULT 0);
PRAGMA user_version=6;
)SQL");
        if (currentVersion < 7) exec(db_, R"SQL(
ALTER TABLE filters RENAME TO old_filters;
CREATE TABLE filters (
 id INTEGER PRIMARY KEY AUTOINCREMENT, account_id INTEGER NOT NULL, name TEXT NOT NULL,
 sender_contains TEXT NOT NULL, subject_contains TEXT NOT NULL, add_tag TEXT NOT NULL,
 move_to TEXT NOT NULL, mark_read INTEGER NOT NULL, star INTEGER NOT NULL,
 stop INTEGER NOT NULL, enabled INTEGER NOT NULL, position INTEGER NOT NULL DEFAULT 0);
INSERT INTO filters SELECT *,id FROM old_filters;
DROP TABLE old_filters;
ALTER TABLE smart_folders RENAME TO old_smart_folders;
CREATE TABLE smart_folders (
 id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT NOT NULL, account_id INTEGER NOT NULL,
 folder TEXT NOT NULL, search TEXT NOT NULL, tag TEXT NOT NULL, unread INTEGER NOT NULL,
 starred INTEGER NOT NULL, attachments INTEGER NOT NULL, snoozed_only INTEGER NOT NULL, include_snoozed INTEGER NOT NULL);
INSERT INTO smart_folders SELECT * FROM old_smart_folders;
DROP TABLE old_smart_folders;
CREATE TABLE label_changes (
 id INTEGER PRIMARY KEY AUTOINCREMENT, account_id INTEGER NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
 label_id TEXT NOT NULL,kind TEXT NOT NULL,name TEXT NOT NULL,error TEXT NOT NULL DEFAULT '',
 UNIQUE(account_id,label_id));
PRAGMA user_version=7;
)SQL");
        if (currentVersion < 8) exec(db_, R"SQL(
CREATE TABLE snooze_reminders (
 id INTEGER PRIMARY KEY AUTOINCREMENT,
 message_id INTEGER NOT NULL UNIQUE REFERENCES messages(id) ON DELETE CASCADE,
 due INTEGER NOT NULL,notified INTEGER NOT NULL DEFAULT 0);
CREATE INDEX snooze_reminders_due ON snooze_reminders(due);
INSERT INTO snooze_reminders(message_id,due)
 SELECT id,snoozed_until FROM messages WHERE snoozed_until>CAST(strftime('%s','now') AS INTEGER);
CREATE TRIGGER snooze_updated AFTER UPDATE OF snoozed_until ON messages BEGIN
 DELETE FROM snooze_reminders WHERE message_id=new.id;
 INSERT INTO snooze_reminders(message_id,due) SELECT new.id,new.snoozed_until WHERE new.snoozed_until>0;
END;
CREATE TRIGGER snooze_inserted AFTER INSERT ON messages WHEN new.snoozed_until>0 BEGIN
 INSERT INTO snooze_reminders(message_id,due) VALUES(new.id,new.snoozed_until);
END;
PRAGMA user_version=8;
)SQL");
        if (currentVersion < 9) exec(db_, R"SQL(
CREATE TABLE draft_images (
 draft_id INTEGER NOT NULL REFERENCES drafts(id) ON DELETE CASCADE,
 offset INTEGER NOT NULL,length INTEGER NOT NULL,content_id TEXT NOT NULL,
 PRIMARY KEY(draft_id,offset));
PRAGMA user_version=9;
)SQL");
        if (currentVersion < 10) exec(db_, R"SQL(
CREATE TABLE remote_moves (
 message_id INTEGER PRIMARY KEY REFERENCES messages(id) ON DELETE CASCADE,
 change_id INTEGER NOT NULL,value TEXT NOT NULL,
 source_folder TEXT NOT NULL,source_epoch TEXT NOT NULL,source_uid TEXT NOT NULL,
 target_folder TEXT NOT NULL,target_epoch TEXT NOT NULL,target_uid TEXT NOT NULL,
 first_uid TEXT NOT NULL,stage TEXT NOT NULL,atomic_move INTEGER NOT NULL);
PRAGMA user_version=10;
)SQL");
        if (currentVersion < 11) exec(db_, R"SQL(
ALTER TABLE remote_moves ADD COLUMN active INTEGER NOT NULL DEFAULT 0;
ALTER TABLE remote_moves ADD COLUMN revision INTEGER NOT NULL DEFAULT 0;
ALTER TABLE remote_moves ADD COLUMN error TEXT NOT NULL DEFAULT '';
ALTER TABLE pending_changes ADD COLUMN error TEXT NOT NULL DEFAULT '';
PRAGMA user_version=11;
)SQL");
        if (currentVersion < 12) exec(db_, R"SQL(
ALTER TABLE drafts ADD COLUMN revision INTEGER NOT NULL DEFAULT 1;
CREATE TABLE gmail_drafts (
 id INTEGER PRIMARY KEY AUTOINCREMENT,
 draft_id INTEGER UNIQUE REFERENCES drafts(id) ON DELETE SET NULL,
 account_id INTEGER NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
 revision INTEGER NOT NULL DEFAULT 1,active INTEGER NOT NULL DEFAULT 0,
 remote_id TEXT NOT NULL DEFAULT '',message_id TEXT NOT NULL DEFAULT '',
 thread_id TEXT NOT NULL DEFAULT '',raw BLOB NOT NULL DEFAULT X'',
 saved_revision INTEGER NOT NULL DEFAULT 0,
 attempt TEXT NOT NULL DEFAULT '' CHECK(attempt IN('','create','update','delete')),
 attempt_revision INTEGER NOT NULL DEFAULT 0,attempt_raw BLOB NOT NULL DEFAULT X'',
 attempt_thread TEXT NOT NULL DEFAULT '',error TEXT NOT NULL DEFAULT '');
CREATE INDEX gmail_drafts_account ON gmail_drafts(account_id);
CREATE UNIQUE INDEX gmail_drafts_remote ON gmail_drafts(account_id,remote_id) WHERE remote_id!='';
PRAGMA user_version=12;
)SQL");
        if (currentVersion < 13) exec(db_, R"SQL(
CREATE TABLE draft_editors (draft_id INTEGER PRIMARY KEY REFERENCES drafts(id) ON DELETE CASCADE,owner TEXT NOT NULL);
ALTER TABLE gmail_drafts ADD COLUMN conflict INTEGER NOT NULL DEFAULT 0;
ALTER TABLE gmail_drafts ADD COLUMN missing INTEGER NOT NULL DEFAULT 0;
ALTER TABLE gmail_drafts ADD COLUMN observed_id TEXT NOT NULL DEFAULT '';
ALTER TABLE gmail_drafts ADD COLUMN observed_thread TEXT NOT NULL DEFAULT '';
ALTER TABLE gmail_drafts ADD COLUMN observed_raw BLOB NOT NULL DEFAULT X'';
PRAGMA user_version=13;
)SQL");
        if (currentVersion < 14) exec(db_, R"SQL(
CREATE TABLE gmail_draft_matches (
 journal_id INTEGER NOT NULL REFERENCES gmail_drafts(id) ON DELETE CASCADE,
 remote_id TEXT NOT NULL,message_id TEXT NOT NULL,thread_id TEXT NOT NULL,
 subject TEXT NOT NULL,raw BLOB NOT NULL,PRIMARY KEY(journal_id,remote_id));
PRAGMA user_version=14;
)SQL");
        // The lists filter on columns stored after each message's HTML and raw
        // MIME: without an index holding them, every count read the whole
        // database (minutes for a few gigabytes).
        exec(db_, R"SQL(
CREATE INDEX IF NOT EXISTS messages_list ON messages(thread_id,received,account_id,folder,unread,starred,snoozed_until,subject,sender);
PRAGMA user_version=15;
)SQL");
        // Messages deleted permanently from Trash. The row hides its message
        // at once; the message itself goes when its server copy is gone, so a
        // later check cannot download it again. See DeleteStore.cpp.
        exec(db_, R"SQL(
CREATE TABLE IF NOT EXISTS deleted_messages (
 message_id INTEGER PRIMARY KEY REFERENCES messages(id) ON DELETE CASCADE,
 state TEXT NOT NULL CHECK(state IN('pending','flagged','local')),
 error TEXT NOT NULL DEFAULT '');
PRAGMA user_version=16;
)SQL");
        transaction.commit();
    } catch (...) { sqlite3_close(db_); db_ = nullptr; throw; }
}
Store::~Store() { sqlite3_close(db_); }

Id Store::saveAccount(const Account& a)
{
    std::lock_guard<std::mutex> guard(mutex_);
    validateAccount(a);
    Transaction transaction(db_);
    if (a.id) {
        Statement previous(db_, "SELECT provider,incoming_host,incoming_port,incoming_user,email FROM accounts WHERE id=? AND (EXISTS(SELECT 1 FROM messages m JOIN remote_messages r ON r.message_id=m.id WHERE m.account_id=accounts.id) OR EXISTS(SELECT 1 FROM messages m JOIN sent_copies c ON c.message_id=m.id WHERE m.account_id=accounts.id) OR EXISTS(SELECT 1 FROM gmail_drafts g WHERE g.account_id=accounts.id))");
        previous.bind(1,a.id);
        if (previous.step()) require(previous.number(0)==static_cast<Id>(a.provider) && previous.text(1)==a.incoming.host
            && previous.number(2)==a.incoming.port && previous.text(3)==a.incoming.username
            && (a.provider!=Provider::Gmail || lower(previous.text(4))==lower(a.email)),
            "Add a separate account to change the incoming server, username, or Google identity after downloading mail.");
    }
    Statement s(db_, R"SQL(INSERT INTO accounts VALUES(NULLIF(?1,0),?2,?3,?4,?5,?6,?7,?8,?9,?10,?11,?12,?13,?14,?15,
COALESCE((SELECT credential_id FROM accounts WHERE id=?1),lower(hex(randomblob(16)))),?16,?17)
ON CONFLICT(id) DO UPDATE SET name=excluded.name,email=excluded.email,provider=excluded.provider,
incoming_host=excluded.incoming_host,incoming_port=excluded.incoming_port,incoming_security=excluded.incoming_security,
incoming_user=excluded.incoming_user,incoming_cert=excluded.incoming_cert,
outgoing_host=excluded.outgoing_host,outgoing_port=excluded.outgoing_port,outgoing_security=excluded.outgoing_security,
outgoing_user=excluded.outgoing_user,outgoing_cert=excluded.outgoing_cert,unified=excluded.unified,sent_mode=excluded.sent_mode,sent_folder=excluded.sent_folder)SQL");
    int n = 1;
    s.bind(n++, a.id); s.bind(n++, a.name); s.bind(n++, a.email); s.bind(n++, static_cast<Id>(a.provider));
    for (const Endpoint* e : {&a.incoming, &a.outgoing}) {
        s.bind(n++, e->host); s.bind(n++, e->port); s.bind(n++, static_cast<Id>(e->security));
        s.bind(n++, e->username); s.bind(n++, e->certificate);
    }
    s.bind(n++, a.unified); s.bind(n++, static_cast<Id>(a.sentMode)); s.bind(n,a.sentFolder); s.step();
    const auto id=a.id ? a.id : sqlite3_last_insert_rowid(db_); transaction.commit(); return id;
}

std::vector<Account> Store::accounts()
{
    std::lock_guard<std::mutex> guard(mutex_);
    Statement s(db_, "SELECT * FROM accounts ORDER BY id");
    std::vector<Account> out;
    while (s.step()) {
        Account a; int n = 0;
        a.id = s.number(n++); a.name = s.text(n++); a.email = s.text(n++); a.provider = static_cast<Provider>(s.number(n++));
        for (Endpoint* e : {&a.incoming, &a.outgoing}) {
            e->host = s.text(n++); e->port = static_cast<int>(s.number(n++)); e->security = static_cast<Security>(s.number(n++));
            e->username = s.text(n++); e->certificate = s.text(n++);
        }
        a.unified = s.number(n++); a.credentialId=s.text(n++); a.sentMode=static_cast<SentCopyMode>(s.number(n++)); a.sentFolder=s.text(n); out.push_back(std::move(a));
    }
    return out;
}

void Store::removeAccount(Id account)
{
    std::lock_guard<std::mutex> guard(mutex_);
    Transaction transaction(db_);
    Statement active(db_,"SELECT 1 FROM outbox WHERE account_id=? AND state='sending'"); active.bind(1,account);
    require(!active.step(),"Wait for sending to finish before removing this account.");
    Statement copying(db_,"SELECT 1 FROM sent_copies c JOIN messages m ON m.id=c.message_id WHERE m.account_id=? AND c.state IN('checking','uploading')"); copying.bind(1,account);
    require(!copying.step(),"Wait for the Sent copy check to finish before removing this account.");
    Statement saving(db_,"SELECT 1 FROM gmail_drafts WHERE account_id=? AND active=1"); saving.bind(1,account);
    require(!saving.step(),"Wait for the draft check to finish before removing this account.");
    for (const char* sql:{"DELETE FROM outbox WHERE account_id=?","DELETE FROM drafts WHERE account_id=?"}) {
        Statement remove(db_,sql); remove.bind(1,account); remove.step();
    }
    for (const char* sql : {"DELETE FROM messages WHERE account_id=?", "DELETE FROM thread_keys WHERE account_id=?",
        "DELETE FROM remote_folders WHERE account_id=?", "DELETE FROM filters WHERE account_id=?",
        "DELETE FROM smart_folders WHERE account_id=?", "DELETE FROM accounts WHERE id=?"}) {
        Statement remove(db_,sql); remove.bind(1,account); remove.step();
    }
    exec(db_,"DELETE FROM threads WHERE NOT EXISTS(SELECT 1 FROM messages WHERE thread_id=threads.id) AND NOT EXISTS(SELECT 1 FROM thread_keys WHERE thread_id=threads.id)");
    transaction.commit();
}
Id Store::findRemote(Id account,const std::string& key)
{
    std::lock_guard<std::mutex> guard(mutex_);
    Statement s(db_,"SELECT id FROM messages WHERE account_id=? AND remote_key=?");
    s.bind(1,account); s.bind(2,key); return s.step()?s.number(0):0;
}
void Store::queueChange(Id message,const std::string& kind,const std::string& value)
{
    Statement remote(db_,"SELECT 1 FROM messages m JOIN accounts a ON a.id=m.account_id WHERE m.id=? AND a.provider IN(1,3,4) AND (EXISTS(SELECT 1 FROM remote_messages WHERE message_id=m.id) OR EXISTS(SELECT 1 FROM sent_copies WHERE message_id=m.id AND state NOT IN('complete','local')))");
    remote.bind(1,message); if (!remote.step()) return;
    // A new ID prevents an in-flight acknowledgement from erasing a newer edit.
    Statement remove(db_,"DELETE FROM pending_changes WHERE message_id=? AND kind=?");
    remove.bind(1,message); remove.bind(2,kind); remove.step();
    Statement insert(db_,"INSERT INTO pending_changes(message_id,kind,value) VALUES(?,?,?)");
    insert.bind(1,message); insert.bind(2,kind); insert.bind(3,value); insert.step();
}
Id Store::ingestRemote(const Message& incoming,const RemoteLocation& location)
{
    std::lock_guard<std::mutex> guard(mutex_);
    Transaction transaction(db_);
    // Only reconcile a known locally delivered message, in its Sent mailbox.
    // Ordinary messages with equal Message-IDs remain distinct server entries.
    if (incoming.folder=="sent" && !incoming.messageId.empty()) {
        Statement local(db_,"SELECT c.message_id FROM sent_copies c JOIN messages m ON m.id=c.message_id WHERE m.account_id=? AND m.message_id=? AND c.state NOT IN('complete','local') AND (c.destination='' OR c.destination=?) ORDER BY c.message_id LIMIT 1");
        local.bind(1,incoming.account); local.bind(2,incoming.messageId); local.bind(3,location.folder);
        if (local.step()) {
            auto id=local.number(0); bindSentCopyLocked(id,location);
            transaction.commit(); return id;
        }
    }
    Id id=ingestMessage(incoming,incoming.folder=="inbox");
    Statement remote(db_,R"SQL(INSERT INTO remote_messages VALUES(?,?,?,?,1)
ON CONFLICT(message_id) DO UPDATE SET folder=excluded.folder,epoch=excluded.epoch,locator=excluded.locator,present=1)SQL");
    remote.bind(1,id); remote.bind(2,location.folder); remote.bind(3,location.epoch); remote.bind(4,location.locator); remote.step();
    auto stored=readMessage(id);
    if (stored.unread!=incoming.unread) queueChange(id,"read",stored.unread?"0":"1");
    if (stored.starred!=incoming.starred) queueChange(id,"star",stored.starred?"1":"0");
    if (stored.folder!=incoming.folder) queueChange(id,"move",stored.folder);
    transaction.commit(); return id;
}
void Store::reconcileMailbox(Id account,const MailboxSnapshot& snapshot,bool retainMissing)
{
    std::lock_guard<std::mutex> guard(mutex_);
    Transaction transaction(db_);
    if (!retainMissing) {
        Statement hide(db_,"UPDATE remote_messages SET present=0 WHERE folder=? AND message_id IN(SELECT id FROM messages WHERE account_id=?)");
        hide.bind(1,snapshot.folder.id); hide.bind(2,account); hide.step();
    }
    std::set<Id> listed;
    for (const auto& entry : snapshot.messages) {
        Statement find(db_,"SELECT id FROM messages WHERE account_id=? AND remote_key=?");
        find.bind(1,account); find.bind(2,entry.key); if (!find.step()) continue;
        Id id=find.number(0);
        Statement present(db_,"UPDATE remote_messages SET present=1,locator=? WHERE message_id=?");
        present.bind(1,entry.locator); present.bind(2,id); present.step();
        listed.insert(id);
        if (retainMissing) continue; // POP3 has no shared read state or server folders.
        Statement flags(db_,R"SQL(UPDATE messages SET
 unread=CASE WHEN EXISTS(SELECT 1 FROM pending_changes WHERE message_id=?1 AND kind='read') THEN unread ELSE ?2 END,
 starred=CASE WHEN EXISTS(SELECT 1 FROM pending_changes WHERE message_id=?1 AND kind='star') THEN starred ELSE ?3 END,
 folder=CASE WHEN EXISTS(SELECT 1 FROM pending_changes WHERE message_id=?1 AND kind='move') THEN folder ELSE ?4 END
 WHERE id=?1)SQL");
        flags.bind(1,id); flags.bind(2,entry.unread); flags.bind(3,entry.starred); flags.bind(4,snapshot.folder.localName()); flags.step();
    }
    // A permanently deleted message leaves the cache once a complete listing
    // no longer has it: expunged here or by another client, or dropped by a
    // POP3 server. Until then its hidden row keeps it from being downloaded.
    std::vector<Id> gone;
    {
        Statement deleted(db_,R"SQL(SELECT m.id FROM deleted_messages d JOIN messages m ON m.id=d.message_id
 JOIN remote_messages r ON r.message_id=m.id WHERE m.account_id=? AND r.folder=?
 AND NOT EXISTS(SELECT 1 FROM remote_moves WHERE message_id=m.id))SQL");
        deleted.bind(1,account); deleted.bind(2,snapshot.folder.id);
        while (deleted.step()) if (!listed.count(deleted.number(0))) gone.push_back(deleted.number(0));
    }
    for (Id id : gone) { Statement remove(db_,"DELETE FROM messages WHERE id=?"); remove.bind(1,id); remove.step(); }
    transaction.commit();
}
void Store::saveRemoteFolders(Id account,const std::vector<RemoteFolder>& folders)
{
    std::lock_guard<std::mutex> guard(mutex_);
    Transaction transaction(db_);
    Statement remove(db_,"DELETE FROM remote_folders WHERE account_id=?"); remove.bind(1,account); remove.step();
    for (const auto& folder : folders) {
        Statement insert(db_,"INSERT INTO remote_folders VALUES(?,?,?,?)");
        insert.bind(1,account); insert.bind(2,folder.id); insert.bind(3,folder.name); insert.bind(4,folder.role); insert.step();
    }
    // LIST is authoritative for selectable IMAP folders. Preserve raw cached
    // content, but do not leave deleted folders' messages in unified views.
    Statement hide(db_,R"SQL(UPDATE remote_messages SET present=0
WHERE message_id IN(SELECT m.id FROM messages m JOIN accounts a ON a.id=m.account_id WHERE m.account_id=?1 AND a.provider IN(1,4))
AND NOT EXISTS(SELECT 1 FROM remote_folders f WHERE f.account_id=?1 AND f.remote_id=remote_messages.folder))SQL");
    hide.bind(1,account); hide.step();
    transaction.commit();
}
std::vector<RemoteFolder> Store::remoteFolders(Id account)
{
    std::lock_guard<std::mutex> guard(mutex_);
    Statement s(db_,"SELECT remote_id,name,role FROM remote_folders WHERE account_id=? ORDER BY name COLLATE NOCASE"); s.bind(1,account);
    std::vector<RemoteFolder> folders; while (s.step()) folders.push_back({s.text(0),s.text(1),s.text(2)}); return folders;
}
std::vector<PendingChange> Store::pendingChanges(Id account)
{
    std::lock_guard<std::mutex> guard(mutex_);
    Statement s(db_,R"SQL(SELECT p.id,m.id,m.account_id,p.kind,p.value,r.folder,r.epoch,r.locator
FROM pending_changes p JOIN messages m ON p.message_id=m.id JOIN remote_messages r ON r.message_id=m.id
WHERE m.account_id=?
AND NOT EXISTS(SELECT 1 FROM deleted_messages d WHERE d.message_id=m.id AND d.state!='local')
ORDER BY p.id)SQL"); s.bind(1,account);
    std::vector<PendingChange> changes;
    while (s.step()) changes.push_back({s.number(0),s.number(1),s.number(2),s.text(3),s.text(4),{s.text(5),s.text(6),s.text(7)}});
    return changes;
}
void Store::acknowledgeChange(Id operation)
{
    std::lock_guard<std::mutex> guard(mutex_);
    Statement s(db_,"DELETE FROM pending_changes WHERE id=?"); s.bind(1,operation); s.step();
}
void Store::acknowledgeMove(const PendingChange& operation,const RemoteLocation& destination)
{
    std::lock_guard<std::mutex> guard(mutex_);
    Transaction transaction(db_);
    Statement claimed(db_,"SELECT 1 FROM remote_moves WHERE message_id=? AND change_id=? AND active=1");
    claimed.bind(1,operation.message); claimed.bind(2,operation.id);
    if (!claimed.step()) throw std::runtime_error("This folder move is no longer being checked.");
    Statement location(db_,"UPDATE remote_messages SET folder=?,epoch=?,locator=?,present=1 WHERE message_id=? AND folder=? AND epoch=? AND locator=?");
    location.bind(1,destination.folder); location.bind(2,destination.epoch); location.bind(3,destination.locator); location.bind(4,operation.message);
    location.bind(5,operation.location.folder); location.bind(6,operation.location.epoch); location.bind(7,operation.location.locator); location.step();
    if (sqlite3_changes(db_)!=1) throw std::runtime_error("The message location changed while its move was being saved.");
    Statement key(db_,"UPDATE messages SET remote_key=? WHERE id=?");
    key.bind(1,imapRemoteKey(destination)); key.bind(2,operation.message); key.step();
    Statement done(db_,"DELETE FROM pending_changes WHERE id=?"); done.bind(1,operation.id); done.step();
    Statement journal(db_,"DELETE FROM remote_moves WHERE message_id=? AND change_id=?");
    journal.bind(1,operation.message); journal.bind(2,operation.id); journal.step();
    transaction.commit();
}

Id Store::ingest(const Message& incoming)
{
    std::lock_guard<std::mutex> guard(mutex_);
    Transaction transaction(db_);
    Id id=ingestMessage(incoming);
    transaction.commit();
    return id;
}
Id Store::ingestMessage(const Message& incoming,bool applyRules)
{
    require(incoming.account > 0 && !incoming.remoteKey.empty(), "A message needs an account and a stable source identifier.");
    validateFolder(incoming.folder);
    Message m = incoming;
    Id existingId = 0, existingThread = 0;
    {
        Statement old(db_, "SELECT id,thread_id FROM messages WHERE account_id=? AND remote_key=?");
        old.bind(1, m.account); old.bind(2, m.remoteKey);
        if (old.step()) { existingId = old.number(0); existingThread = old.number(1); }
    }
    std::set<std::string> keys;
    if (!m.providerThread.empty()) {
        keys.insert("provider:" + m.providerThread);
    } else {
        if (!m.messageId.empty()) keys.insert("message:" + m.messageId);
        for (const auto& ref : m.references) if (!ref.empty()) keys.insert("message:" + ref);
    }
    std::set<Id> joined;
    if (existingThread) joined.insert(existingThread);
    for (const auto& key : keys) {
        Statement s(db_, "SELECT thread_id FROM thread_keys WHERE account_id=? AND key=?");
        s.bind(1, m.account); s.bind(2, key);
        if (s.step()) joined.insert(s.number(0));
    }
    Id threadId;
    if (joined.empty()) { exec(db_, "INSERT INTO threads DEFAULT VALUES"); threadId = sqlite3_last_insert_rowid(db_); }
    else threadId = *joined.begin();
    for (Id old : joined) {
        if (old == threadId) continue;
        Statement messages(db_, "UPDATE messages SET thread_id=? WHERE thread_id=?");
        messages.bind(1, threadId); messages.bind(2, old); messages.step();
        Statement anchors(db_, "UPDATE thread_keys SET thread_id=? WHERE thread_id=?");
        anchors.bind(1, threadId); anchors.bind(2, old); anchors.step();
        Statement remove(db_, "DELETE FROM threads WHERE id=?"); remove.bind(1, old); remove.step();
    }
    for (const auto& key : keys) {
        Statement s(db_, "INSERT OR IGNORE INTO thread_keys VALUES(?,?,?)");
        s.bind(1, m.account); s.bind(2, key); s.bind(3, threadId); s.step();
    }
    if (!existingId && applyRules) {
        for (const auto& rule : readFilters()) {
            if (!rule.enabled || (rule.account && rule.account != m.account)
                || !contains(m.sender, rule.senderContains) || !contains(m.subject, rule.subjectContains)) continue;
            if (!rule.addTag.empty()) m.tags.push_back(rule.addTag);
            if (!rule.moveTo.empty()) m.folder = rule.moveTo;
            if (rule.markRead) m.unread = false;
            if (rule.star) m.starred = true;
            if (rule.stop) break;
        }
    }
    Statement s(db_, R"SQL(INSERT INTO messages(account_id,remote_key,thread_id,provider_thread,message_id,
sender,recipient,cc,subject,body,html,raw,folder,received,unread,starred,snoozed_until) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)
ON CONFLICT(account_id,remote_key) DO UPDATE SET thread_id=excluded.thread_id,provider_thread=excluded.provider_thread,
message_id=excluded.message_id,sender=excluded.sender,recipient=excluded.recipient,cc=excluded.cc,subject=excluded.subject,
body=excluded.body,html=excluded.html,raw=excluded.raw,received=excluded.received)SQL");
    s.bind(1, m.account); s.bind(2, m.remoteKey); s.bind(3, threadId); s.bind(4, m.providerThread); s.bind(5, m.messageId);
    s.bind(6, m.sender); s.bind(7, m.to); s.bind(8, m.cc); s.bind(9, m.subject); s.bind(10, m.text); s.bind(11, m.html);
    s.blob(12, m.raw); s.bind(13, m.folder); s.bind(14, m.received); s.bind(15, m.unread); s.bind(16, m.starred);
    s.bind(17, std::max<Timestamp>(0, m.snoozedUntil)); s.step();
    Id id = existingId ? existingId : sqlite3_last_insert_rowid(db_);
    Statement removeAttachments(db_, "DELETE FROM attachments WHERE message_id=?");
    removeAttachments.bind(1, id); removeAttachments.step();
    for (const auto& a : m.attachments) {
        Statement add(db_, "INSERT INTO attachments(message_id,name,media_type,content_id,data,inline_image) VALUES(?,?,?,?,?,?)");
        add.bind(1, id); add.bind(2, a.name); add.bind(3, a.mediaType); add.bind(4, a.contentId); add.blob(5, a.data);
        add.bind(6, a.inlineImage); add.step();
    }
    Statement removeRefs(db_, "DELETE FROM message_refs WHERE message_id=?");
    removeRefs.bind(1, id); removeRefs.step();
    for (std::size_t i = 0; i < m.references.size(); ++i) {
        Statement ref(db_, "INSERT INTO message_refs VALUES(?,?,?)");
        ref.bind(1, id); ref.bind(2, static_cast<Id>(i)); ref.bind(3, m.references[i]); ref.step();
    }
    if (!existingId) for (const auto& tag : m.tags) insertTag(db_, id, tag);
    return id;
}

Message Store::readMessage(Id id)
{
    Statement s(db_, "SELECT * FROM messages WHERE id=?"); s.bind(1, id);
    if (!s.step()) throw std::runtime_error("This message is no longer in the mailbox.");
    Message m; int n = 0;
    m.id = s.number(n++); m.account = s.number(n++); m.remoteKey = s.text(n++); m.thread = s.number(n++);
    m.providerThread = s.text(n++); m.messageId = s.text(n++); m.sender = s.text(n++); m.to = s.text(n++);
    m.cc = s.text(n++); m.subject = s.text(n++); m.text = s.text(n++); m.html = s.text(n++); m.raw = s.text(n++);
    m.folder = s.text(n++); m.received = s.number(n++); m.unread = s.number(n++); m.starred = s.number(n++); m.snoozedUntil = s.number(n);
    Statement tags(db_, "SELECT name FROM effective_tags WHERE message_id=? ORDER BY name"); tags.bind(1, id);
    while (tags.step()) m.tags.push_back(tags.text(0));
    Statement refs(db_, "SELECT reference FROM message_refs WHERE message_id=? ORDER BY position"); refs.bind(1, id);
    while (refs.step()) m.references.push_back(refs.text(0));
    Statement attachments(db_, "SELECT name,media_type,content_id,data,inline_image FROM attachments WHERE message_id=? ORDER BY id");
    attachments.bind(1, id);
    while (attachments.step()) m.attachments.push_back({attachments.text(0), attachments.text(1), attachments.text(2), attachments.text(3), attachments.number(4) != 0});
    return m;
}
Message Store::message(Id id) { std::lock_guard<std::mutex> guard(mutex_); return readMessage(id); }
std::vector<Message> Store::thread(Id id)
{
    std::lock_guard<std::mutex> guard(mutex_);
    Statement s(db_, "SELECT id FROM messages WHERE thread_id=? AND NOT EXISTS(SELECT 1 FROM remote_messages r WHERE r.message_id=messages.id AND r.present=0) AND NOT EXISTS(SELECT 1 FROM deleted_messages d WHERE d.message_id=messages.id) ORDER BY received,id"); s.bind(1, id);
    std::vector<Message> out;
    while (s.step()) out.push_back(readMessage(s.number(0)));
    return out;
}
namespace {
// Messages visible in a view, as a common table expression named "matching".
enum class MatchingMode { List, Count };
std::string matchingSql(bool search, MatchingMode mode)
{
    // Only the columns each consumer needs. The window function feeds every
    // selected column through its sorter: subject/sender/body there double
    // the list cost on large mailboxes. The list joins the winning message
    // back for its preview; counts need no window at all.
    std::string columns;
    switch (mode) {
    case MatchingMode::List:
        columns = "m.id,m.thread_id,m.account_id,m.received,m.unread,m.starred, EXISTS(SELECT 1 FROM attachments a WHERE a.message_id=m.id) AS has_attachment,\n"
                  "ROW_NUMBER() OVER(PARTITION BY thread_id ORDER BY received DESC,m.id DESC) AS rank";
        break;
    case MatchingMode::Count:
        columns = "m.thread_id,m.unread";
        break;
    }
    std::string sql = "WITH matching AS (\nSELECT " + columns + R"SQL(
FROM messages m JOIN accounts a ON a.id=m.account_id
WHERE ((?1=0 AND a.unified=1) OR m.account_id=?1)
AND NOT EXISTS(SELECT 1 FROM remote_messages r WHERE r.message_id=m.id AND r.present=0)
AND NOT EXISTS(SELECT 1 FROM deleted_messages d WHERE d.message_id=m.id)
AND (
 (?2='' AND folder NOT IN('trash','junk'))
 OR ((a.provider!=3 OR NOT EXISTS(SELECT 1 FROM remote_messages r WHERE r.message_id=m.id)) AND folder=?2)
 OR (a.provider=3 AND EXISTS(SELECT 1 FROM remote_messages r WHERE r.message_id=m.id) AND (
   (?2='trash' AND folder='trash') OR (?2='junk' AND folder='junk')
   OR (folder NOT IN('trash','junk') AND (
     (?2='archive' AND NOT EXISTS(SELECT 1 FROM gmail_message_labels g WHERE g.message_id=m.id AND g.label_id IN('INBOX','DRAFT')))
     OR EXISTS(SELECT 1 FROM gmail_message_labels g WHERE g.message_id=m.id AND g.label_id=
       CASE ?2 WHEN 'inbox' THEN 'INBOX' WHEN 'sent' THEN 'SENT' WHEN 'drafts' THEN 'DRAFT'
       ELSE CASE WHEN substr(?2,1,7)='remote:' THEN substr(?2,8) ELSE '' END END)
   ))
 ))
)
AND (?3=0 OR unread=1) AND (?4=0 OR starred=1)
AND (?5=0 OR EXISTS(SELECT 1 FROM attachments att WHERE att.message_id=m.id))
AND (?6='' OR EXISTS(SELECT 1 FROM effective_tags t WHERE t.message_id=m.id AND t.name=?6))
AND ((?7=1 AND snoozed_until>?9) OR (?7=0 AND (?8=1 OR snoozed_until<=?9)))
)SQL";
    if (search) sql += " AND m.id IN(SELECT rowid FROM message_search WHERE message_search MATCH ?10)";
    return sql + ")";
}
void bindQuery(Statement& s, const Query& q, Timestamp now, const std::string& search)
{
    s.bind(1, q.account); s.bind(2, q.folder); s.bind(3, q.unread); s.bind(4, q.starred);
    s.bind(5, q.attachments); s.bind(6, q.tag); s.bind(7, q.snoozedOnly); s.bind(8, q.includeSnoozed); s.bind(9, now);
    if (!search.empty()) s.bind(10, search);
}
}
std::vector<Conversation> Store::conversations(const Query& q, Timestamp now)
{
    std::lock_guard<std::mutex> guard(mutex_);
    std::string search = searchExpression(q.text);
    std::string sql = matchingSql(!search.empty(), MatchingMode::List) + R"SQL(
SELECT s.thread_id,s.account_id,lm.subject,lm.sender,substr(lm.body,1,180),s.latest,s.count,s.unread,s.starred,s.has_attachment
FROM (
SELECT thread_id,MAX(CASE WHEN rank=1 THEN account_id END) AS account_id,MAX(received) AS latest,COUNT(*) AS count,
SUM(unread) AS unread,MAX(starred) AS starred,MAX(has_attachment) AS has_attachment,MAX(CASE WHEN rank=1 THEN id END) AS latest_id
FROM matching GROUP BY thread_id
) s JOIN messages lm ON lm.id=s.latest_id
ORDER BY s.latest DESC,s.thread_id DESC)SQL";
    Statement s(db_, sql.c_str()); bindQuery(s, q, now, search);
    std::vector<Conversation> out;
    while (s.step()) out.push_back({s.number(0),s.number(1),s.text(2),s.text(3),s.text(4),s.number(5),
        static_cast<int>(s.number(6)),static_cast<int>(s.number(7)),s.number(8)!=0,s.number(9)!=0});
    return out;
}
std::size_t Store::unreadConversations(const Query& q, Timestamp now)
{
    std::lock_guard<std::mutex> guard(mutex_);
    std::string search = searchExpression(q.text);
    Statement s(db_, (matchingSql(!search.empty(), MatchingMode::Count) + " SELECT COUNT(DISTINCT thread_id) FROM matching WHERE unread=1").c_str());
    bindQuery(s, q, now, search);
    return s.step() ? static_cast<std::size_t>(s.number(0)) : 0;
}

void Store::setRead(Id id, bool read)
{
    std::lock_guard<std::mutex> guard(mutex_);
    Transaction transaction(db_);
    Statement s(db_, "UPDATE messages SET unread=? WHERE id=?"); s.bind(1, !read); s.bind(2, id); s.step();
    queueChange(id,"read",read?"1":"0"); applyGmailPending(id); transaction.commit();
}
void Store::setStarred(Id id, bool starred)
{
    std::lock_guard<std::mutex> guard(mutex_);
    Transaction transaction(db_);
    Statement s(db_, "UPDATE messages SET starred=? WHERE id=?"); s.bind(1, starred); s.bind(2, id); s.step();
    queueChange(id,"star",starred?"1":"0"); applyGmailPending(id); transaction.commit();
}
void Store::move(Id id, const std::string& folder)
{
    std::lock_guard<std::mutex> guard(mutex_); validateFolder(folder);
    Transaction transaction(db_);
    Statement s(db_, "UPDATE messages SET folder=?,snoozed_until=0 WHERE id=?"); s.bind(1, folder); s.bind(2, id); s.step();
    queueChange(id,"move",folder); applyGmailPending(id); transaction.commit();
}
void Store::snooze(Id id, Timestamp until)
{
    snooze(std::vector<Id>{id},until);
}
void Store::tag(Id id, const std::string& tag, bool add)
{
    std::lock_guard<std::mutex> guard(mutex_); validateTag(tag); Transaction transaction(db_);
    if (add) insertTag(db_, id, tag);
    else { Statement s(db_, "DELETE FROM tags WHERE message_id=? AND name=?"); s.bind(1, id); s.bind(2, tag); s.step(); }
    Statement gmail(db_,"SELECT 1 FROM messages m JOIN accounts a ON a.id=m.account_id WHERE m.id=? AND a.provider=3");
    gmail.bind(1,id);
    if (gmail.step()) { queueChange(id,"tag:"+tag,add?"1":"0"); applyGmailPending(id); }
    transaction.commit();
}
std::vector<std::string> Store::tags()
{
    std::lock_guard<std::mutex> guard(mutex_);
    Statement s(db_, "SELECT DISTINCT name FROM effective_tags ORDER BY name COLLATE NOCASE");
    std::vector<std::string> out; while (s.step()) out.push_back(s.text(0)); return out;
}
Id Store::saveSmartFolder(const SmartFolder& f)
{
    std::lock_guard<std::mutex> guard(mutex_); Transaction transaction(db_); validateTag(f.name); validateScope(db_,f.query.account);
    if (!f.query.folder.empty()) validateFolder(f.query.folder);
    if (!f.query.tag.empty()) validateTag(f.query.tag);
    require(f.query.text.size()<=4096 && f.query.text.find('\0')==std::string::npos,"The search is too long or contains a null character.");
    if (f.id) { Statement old(db_,"SELECT 1 FROM smart_folders WHERE id=?"); old.bind(1,f.id); require(old.step(),"This smart folder was removed. Create a new one instead."); }
    Statement s(db_, R"SQL(INSERT INTO smart_folders VALUES(NULLIF(?,0),?,?,?,?,?,?,?,?,?,?)
ON CONFLICT(id) DO UPDATE SET name=excluded.name,account_id=excluded.account_id,folder=excluded.folder,
search=excluded.search,tag=excluded.tag,unread=excluded.unread,starred=excluded.starred,
attachments=excluded.attachments,snoozed_only=excluded.snoozed_only,include_snoozed=excluded.include_snoozed)SQL");
    s.bind(1, f.id); s.bind(2, f.name); bindQuery(s, 3, f.query); s.step();
    auto id=f.id ? f.id : sqlite3_last_insert_rowid(db_); transaction.commit(); return id;
}
std::vector<SmartFolder> Store::smartFolders()
{
    std::lock_guard<std::mutex> guard(mutex_);
    Statement s(db_, "SELECT * FROM smart_folders ORDER BY name COLLATE NOCASE");
    std::vector<SmartFolder> out; while (s.step()) out.push_back({s.number(0),s.text(1),readQuery(s,2)}); return out;
}
Id Store::saveFilter(const Filter& f)
{
    std::lock_guard<std::mutex> guard(mutex_); Transaction transaction(db_); validateTag(f.name); validateScope(db_,f.account);
    require(f.senderContains.size()<=1024 && f.subjectContains.size()<=1024 && f.senderContains.find('\0')==std::string::npos && f.subjectContains.find('\0')==std::string::npos,"The rule's match text is invalid or too long.");
    if (f.id) { Statement old(db_,"SELECT 1 FROM filters WHERE id=?"); old.bind(1,f.id); require(old.step(),"This rule was removed. Create a new one instead."); }
    require(!f.senderContains.empty() || !f.subjectContains.empty(), "Give the filter a sender or subject to match.");
    require(!f.enabled || !f.addTag.empty() || !f.moveTo.empty() || f.markRead || f.star || f.stop,"Choose a rule action, or disable the rule.");
    if (!f.addTag.empty()) validateTag(f.addTag);
    if (!f.moveTo.empty()) validateFolder(f.moveTo);
    Statement s(db_, R"SQL(INSERT INTO filters(id,account_id,name,sender_contains,subject_contains,add_tag,move_to,mark_read,star,stop,enabled) VALUES(NULLIF(?,0),?,?,?,?,?,?,?,?,?,?)
ON CONFLICT(id) DO UPDATE SET account_id=excluded.account_id,name=excluded.name,sender_contains=excluded.sender_contains,
subject_contains=excluded.subject_contains,add_tag=excluded.add_tag,move_to=excluded.move_to,
mark_read=excluded.mark_read,star=excluded.star,stop=excluded.stop,enabled=excluded.enabled)SQL");
    s.bind(1,f.id); s.bind(2,f.account); s.bind(3,f.name); s.bind(4,f.senderContains); s.bind(5,f.subjectContains);
    s.bind(6,f.addTag); s.bind(7,f.moveTo); s.bind(8,f.markRead); s.bind(9,f.star); s.bind(10,f.stop); s.bind(11,f.enabled); s.step();
    auto id=f.id ? f.id : sqlite3_last_insert_rowid(db_);
    if (!f.id) { Statement position(db_,"UPDATE filters SET position=(SELECT COALESCE(MAX(position),0)+1 FROM filters) WHERE id=?"); position.bind(1,id); position.step(); }
    transaction.commit(); return id;
}
std::vector<Filter> Store::readFilters()
{
    Statement s(db_, "SELECT * FROM filters ORDER BY position,id");
    std::vector<Filter> out;
    while (s.step()) out.push_back({s.number(0),s.number(1),s.text(2),s.text(3),s.text(4),s.text(5),s.text(6),
        s.number(7)!=0,s.number(8)!=0,s.number(9)!=0,s.number(10)!=0});
    return out;
}
std::vector<Filter> Store::filters() { std::lock_guard<std::mutex> guard(mutex_); return readFilters(); }

Outgoing Store::readOutgoing(Id id,bool includeRaw)
{
    Statement s(db_,"SELECT o.id,o.draft_id,o.account_id,o.state,o.error,o.sender,CASE WHEN ? THEN o.raw ELSE '' END,o.created,d.subject,COALESCE(g.remote_id,''),COALESCE(d.provider_thread,'') FROM outbox o LEFT JOIN drafts d ON d.id=o.draft_id LEFT JOIN gmail_drafts g ON g.draft_id=d.id AND g.account_id=o.account_id WHERE o.id=?");
    s.bind(1,includeRaw?1:0); s.bind(2,id); require(s.step(),"This outbox entry no longer exists.");
    Outgoing o; o.id=id; o.draft=s.number(1); o.account=s.number(2); o.state=s.text(3); o.error=s.text(4); o.from=s.text(5); o.raw=s.text(6); o.created=s.number(7);
    o.subject=s.text(8);
    o.gmailDraft=s.text(9); o.providerThread=s.text(10);
    Statement r(db_,"SELECT address FROM outbox_recipients WHERE outbox_id=? ORDER BY position"); r.bind(1,id);
    while (r.step()) o.recipients.push_back(r.text(0));
    return o;
}
std::vector<Outgoing> Store::outbox(bool includeRaw)
{
    std::lock_guard<std::mutex> guard(mutex_); Statement s(db_,"SELECT id FROM outbox WHERE state!='sent' ORDER BY id"); std::vector<Outgoing> out;
    while (s.step()) out.push_back(readOutgoing(s.number(0),includeRaw));
    return out;
}
OutboxSummary Store::outboxSummary()
{
    std::lock_guard<std::mutex> guard(mutex_);
    Statement s(db_,"SELECT state,COUNT(*) FROM outbox WHERE state!='sent' GROUP BY state"); OutboxSummary result;
    while (s.step()) {
        auto state=s.text(0); auto count=s.number(1);
        if (state=="queued") result.queued=count;
        else if (state=="sending") result.sending=count;
        else if (state=="failed") result.failed=count;
        else if (state=="uncertain") result.uncertain=count;
    }
    return result;
}
Outgoing Store::claimOutgoing()
{
    std::lock_guard<std::mutex> guard(mutex_); Transaction transaction(db_);
    Statement s(db_,"SELECT id FROM outbox WHERE state='queued' AND NOT EXISTS(SELECT 1 FROM gmail_drafts g WHERE g.draft_id=outbox.draft_id AND (g.active=1 OR g.attempt!='' OR g.conflict=1)) ORDER BY id LIMIT 1");
    if (!s.step()) return {};
    auto id=s.number(0); Statement update(db_,"UPDATE outbox SET state='sending',error='' WHERE id=?"); update.bind(1,id); update.step();
    auto out=readOutgoing(id); transaction.commit(); return out;
}
void Store::finishOutgoing(Id id,const std::string& state,const std::string& error,Timestamp now,const GmailSent& receipt)
{
    require(state=="sent" || state=="failed" || state=="uncertain","Invalid delivery outcome.");
    std::lock_guard<std::mutex> guard(mutex_); Transaction transaction(db_); auto o=readOutgoing(id);
    require(o.state=="sending","This send attempt is no longer active.");
    if (state=="sent") {
        Statement account(db_,"SELECT provider,sent_mode,sent_folder FROM accounts WHERE id=?"); account.bind(1,o.account); require(account.step(),"The sending account no longer exists.");
        if (account.number(0)==static_cast<Id>(Provider::Gmail)) {
            require(!receipt.id.empty() && !receipt.thread.empty(),"The Gmail delivery receipt is missing.");
            Statement existing(db_,"SELECT id FROM messages WHERE account_id=? AND remote_key=?");
            existing.bind(1,o.account); existing.bind(2,"gmail:"+receipt.id);
            // A receive worker may already have downloaded the accepted message.
            // Keep its authoritative labels/content instead of replacing them.
            if (!existing.step()) ingestGmailMessage(o.account,{receipt.id,receipt.thread,{}, {"SENT"},now,o.raw},0,false);
        } else {
            auto m=parseMessage(o.raw); m.account=o.account; m.remoteKey="outgoing:"+m.messageId; m.folder="sent"; m.received=now; m.unread=false;
            m.providerThread=readDraft(o.draft).providerThread;
            Account a; a.provider=static_cast<Provider>(account.number(0)); a.sentMode=static_cast<SentCopyMode>(account.number(1));
            Id local=0;
            // Receive may have downloaded a server-created Sent copy before
            // SMTP completed. Adopt only this outgoing Message-ID.
            if (sentCopyMode(a)!=SentCopyMode::Local) {
                Statement existing(db_,"SELECT m.id FROM messages m JOIN remote_messages r ON r.message_id=m.id JOIN remote_folders f ON f.account_id=m.account_id AND f.remote_id=r.folder WHERE m.account_id=? AND m.message_id=? AND f.role='sent' AND r.present=1 AND (?='' OR f.remote_id=? OR f.name=?) ORDER BY m.id LIMIT 1");
                existing.bind(1,m.account); existing.bind(2,m.messageId); for (int n=3;n<=5;++n) existing.bind(n,account.text(2)); if (existing.step()) local=existing.number(0);
            }
            bool reconciled=local!=0;
            if (!local) local=ingestMessage(m,false);
            if (sentCopyMode(a)!=SentCopyMode::Local) {
                Statement copy(db_,"INSERT INTO sent_copies(message_id,outbox_id,state) VALUES(?,?,?)");
                copy.bind(1,local); copy.bind(2,o.id); copy.bind(3,reconciled?"complete":"pending"); copy.step();
            }
        }
        // Gmail's confirmed drafts.send consumes its draft. No cleanup request
        // may race with this delivery or recreate the already sent content.
        Statement completed(db_,"DELETE FROM gmail_drafts WHERE draft_id=?"); completed.bind(1,o.draft); completed.step();
        Statement clear(db_,"UPDATE outbox SET draft_id=NULL WHERE id=?"); clear.bind(1,id); clear.step();
        Statement remove(db_,"DELETE FROM drafts WHERE id=?"); remove.bind(1,o.draft); remove.step();
    }
    Statement update(db_,"UPDATE outbox SET state=?,error=? WHERE id=?"); update.bind(1,state); update.bind(2,error); update.bind(3,id); update.step(); transaction.commit();
}
void Store::retryOutgoing(Id id,bool acceptUncertain)
{
    std::lock_guard<std::mutex> guard(mutex_); Transaction transaction(db_); auto o=readOutgoing(id,false);
    require(o.state=="failed" || (acceptUncertain && o.state=="uncertain"),"Check Sent before explicitly retrying an uncertain delivery.");
    Statement s(db_,"UPDATE outbox SET state='queued',error='' WHERE id=?"); s.bind(1,id); s.step(); transaction.commit();
}
Id Store::returnToDraft(Id id,bool acceptUncertain)
{
    std::lock_guard<std::mutex> guard(mutex_); Transaction transaction(db_); auto o=readOutgoing(id,false);
    require(o.state=="queued" || o.state=="failed" || (acceptUncertain && o.state=="uncertain"),"This message is sending, sent, or its delivery is uncertain.");
    Statement s(db_,"DELETE FROM outbox WHERE id=?"); s.bind(1,id); s.step(); transaction.commit(); return o.draft;
}
void Store::recoverOutgoing()
{
    std::lock_guard<std::mutex> guard(mutex_);
    exec(db_,"DELETE FROM draft_editors");
    exec(db_,"UPDATE gmail_drafts SET active=0,revision=revision+1,error='Mail stopped while checking or saving this draft. Check the server before retrying.' WHERE active=1");
    exec(db_,"DELETE FROM gmail_drafts WHERE draft_id IS NULL AND remote_id='' AND attempt='' AND active=0");
    exec(db_,"UPDATE remote_moves SET active=0,revision=revision+1,error='Mail stopped while moving this message. Check the server to recover the saved progress.' WHERE active=1");
    exec(db_,"UPDATE sent_copies SET state=CASE WHEN confirmed THEN 'saved' WHEN may_append THEN 'pending' ELSE 'uncertain' END,error='Mail stopped while checking or saving this Sent copy. Check the server before retrying.' WHERE state IN('checking','uploading')");
    exec(db_,"UPDATE outbox SET state='uncertain',error='Mail stopped during sending. Check Sent before retrying; the recipient may already have this message.' WHERE state='sending'");
}
}
