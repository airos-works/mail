#pragma once
#include "Model.h"
#include "Certificates.h"
#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>

namespace mail {
enum class NetworkFailure { Connection, Authentication, Certificate, Protocol, Cancelled, Unsupported, DeliveryUnknown, CopyUnknown, Rejected };
class NetworkError : public std::runtime_error {
public:
    NetworkError(NetworkFailure reason, const std::string& message) : std::runtime_error(message), reason(reason) {}
    NetworkFailure reason;
};

struct RemoteFolder {
    std::string id;   // Exact provider identifier, including modified UTF-7 for IMAP.
    std::string name; // UTF-8 display name.
    std::string role; // inbox/archive/sent/drafts/trash/junk, or empty.
    std::string localName() const { return role.empty() ? "remote:" + id : role; }
};
struct RemoteMessage {
    std::string key;
    std::string locator; // IMAP UID or POP3 message number, never a UI identifier.
    bool unread = true;
    bool starred = false;
    bool draft = false;
    bool deleted = false;
    bool flagsKnown = false;
};
struct MailboxSnapshot {
    RemoteFolder folder;
    std::string epoch; // IMAP UIDVALIDITY; POP3 uses its stable UIDLs.
    std::vector<RemoteMessage> messages;
    std::string nextUid{}; // IMAP UIDNEXT when advertised; never guessed from row count.
};
struct RemoteLocation {
    std::string folder;
    std::string epoch;
    std::string locator;
};
struct RemoteDraft {
    RemoteLocation location;
    std::string raw;
    bool draft=false,deleted=false;
};
// Persist before sending a command that can create a destination copy. A known
// destination survives reconnects; an uncertain command is reconciled, not replayed.
struct RemoteMove {
    RemoteLocation source, destination;
    std::string firstUid;
    std::string stage = "ready"; // ready, started, copied, moved
    bool atomic = false;
};

SentCopyMode sentCopyMode(const Account&);
void validateAccount(const Account& account);
std::string imapRemoteKey(const RemoteLocation& location);

// Each provider is confined to one sync worker. Authentication and TLS are
// performed by libcurl; IMAP framing then reads literals by length so a body
// can never be mistaken for a command response. Fetch always uses BODY.PEEK.
class MailProvider {
public:
    virtual ~MailProvider() = default;
    virtual std::vector<RemoteFolder> folders() = 0;
    virtual MailboxSnapshot snapshot(const RemoteFolder&) = 0;
    virtual std::string fetch(const MailboxSnapshot&, const RemoteMessage&) = 0;
    virtual void setFlag(const RemoteLocation&, bool readFlag, bool value) = 0;
    virtual RemoteLocation move(const RemoteLocation&, const RemoteFolder&) = 0;
    // Removes one message for good. True when the server no longer has it,
    // false when it could only be marked \Deleted: without UIDPLUS a plain
    // EXPUNGE would also remove other clients' deleted mail, so none is sent.
    virtual bool remove(const RemoteLocation&);
    virtual RemoteMove planMove(const RemoteLocation&, const RemoteFolder&);
    virtual RemoteLocation resumeMove(RemoteMove&, const std::string& raw,
        const std::function<void(const RemoteMove&)>& checkpoint);
    // SEARCH HEADER is a substring match; implementations must verify the
    // returned Message-ID before adopting a server-created Sent copy.
    virtual std::optional<RemoteLocation> findMessage(const RemoteFolder&, const std::string&);
    // A successful APPEND can lack APPENDUID. CopyUnknown means the literal
    // may have been accepted: search on later checks, never blindly reappend.
    virtual std::optional<RemoteLocation> append(const RemoteFolder&, const std::string&, Timestamp);
    virtual std::optional<RemoteLocation> appendDraft(const RemoteFolder&,const std::string&,Timestamp);
    // A missing UID returns no value. Changed UIDVALIDITY or incomplete content
    // is an error, never evidence that a local draft may be discarded.
    virtual std::optional<RemoteDraft> fetchDraft(const RemoteLocation&);
    // Verify original bytes and the Draft flag before removal. UIDPLUS uses
    // targeted expunge; base IMAP leaves only this UID marked Deleted.
    virtual void removeDraft(const RemoteLocation&,const std::string& expectedRaw);

};
std::unique_ptr<MailProvider> connectMail(const Account&, const std::string& password, const std::atomic<bool>& cancelled);

// No automatic send retries: after an interrupted DATA transfer the delivery
// outcome can be unknown. The outbox records that separately and requires an
// explicit user decision before retrying.
void sendSmtp(const Account&, const std::string& password, const std::string& from,
    const std::vector<std::string>& recipients, const std::string& raw, const std::atomic<bool>& cancelled);
// Connects without credentials and without verification to show the server's
// own certificate (the leaf) for an explicit trust decision. scheme is imap,
// pop3 or smtp; the endpoint's security selects implicit TLS or STARTTLS.
CertificateDetails fetchServerCertificate(const Endpoint&, const std::string& scheme, const std::atomic<bool>& cancelled);
}
