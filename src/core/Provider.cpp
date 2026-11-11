#include "mail/Provider.h"
#include "mail/Mime.h"
#include "mail/Certificates.h"
#include <curl/curl.h>
#include <poll.h>
#include <algorithm>
#include <chrono>
#include <ctime>
#include <cstdio>
#include <cstring>
#include <limits>
#include <map>
#include <set>
#include <sstream>

namespace mail {
namespace {
constexpr std::size_t kMaxMessage = 32 * 1024 * 1024;
constexpr std::size_t kMaxResponse = kMaxMessage + 1024 * 1024;
using Clock = std::chrono::steady_clock;
[[noreturn]] void protocol(const std::string& message) { throw NetworkError(NetworkFailure::Protocol, message); }
void require(bool condition, const char* message) { if (!condition) throw std::invalid_argument(message); }
std::string upper(std::string s)
{
    for (char& c : s) if (c >= 'a' && c <= 'z') c -= 'a' - 'A';
    return s;
}
bool hasControl(const std::string& value)
{
    return std::any_of(value.begin(), value.end(), [](unsigned char c) { return c < 32 || c == 127; });
}
std::uint64_t number(const std::string& s)
{
    if (s.empty() || s.size() > 20) protocol("The mail server returned an invalid numeric identifier.");
    std::uint64_t value = 0;
    for (char c : s) {
        if (c < '0' || c > '9' || value > (std::numeric_limits<std::uint64_t>::max() - (c-'0')) / 10)
            protocol("The mail server returned an invalid numeric identifier.");
        value = value * 10 + (c-'0');
    }
    return value;
}
void uid(const std::string& s)
{
    auto n = number(s);
    if (!n || n > 0xffffffffu) protocol("The mail server returned an invalid IMAP UID.");
}
std::string quoted(const std::string& value)
{
    require(!hasControl(value) && value.size() <= 1024, "Invalid mailbox name.");
    std::string out = "\"";
    for (char c : value) { if (c == '\\' || c == '"') out += '\\'; out += c; }
    return out + '"';
}
void curlCheck(CURLcode result)
{
    if (result == CURLE_OK) return;
    NetworkFailure kind = NetworkFailure::Connection;
    if (result == CURLE_LOGIN_DENIED) kind = NetworkFailure::Authentication;
    else if (result == CURLE_PEER_FAILED_VERIFICATION || result == CURLE_SSL_CACERT_BADFILE) kind = NetworkFailure::Certificate;
    else if (result == CURLE_ABORTED_BY_CALLBACK) kind = NetworkFailure::Cancelled;
    else if (result == CURLE_UNSUPPORTED_PROTOCOL || result == CURLE_NOT_BUILT_IN) kind = NetworkFailure::Unsupported;
    // libcurl's constant messages contain no credentials or server-supplied text.
    throw NetworkError(kind, curl_easy_strerror(result));
}
void initializeCurl()
{
    static const CURLcode result = curl_global_init(CURL_GLOBAL_DEFAULT);
    curlCheck(result);
}
int progress(void* user, curl_off_t, curl_off_t, curl_off_t, curl_off_t)
{
    return static_cast<const std::atomic<bool>*>(user)->load() ? 1 : 0;
}
// A file path, or certificate text saved by an earlier version's paste.
void trust(CURL* curl, const std::string& certificate)
{
    if (isPemText(certificate)) {
        auto pem = normalizePem(certificate);
        curl_blob blob{pem.data(), pem.size(), CURL_BLOB_COPY};
        curlCheck(curl_easy_setopt(curl, CURLOPT_CAINFO_BLOB, &blob));
    } else if (!certificate.empty()) curlCheck(curl_easy_setopt(curl, CURLOPT_CAINFO, certificate.c_str()));
}
std::string url(const Endpoint& endpoint, const std::string& scheme)
{
    std::string host = endpoint.host;
    if (host.find(':') != std::string::npos && host.front() != '[') host = '[' + host + ']';
    return scheme + (endpoint.security == Security::TLS ? "s://" : "://") + host + ':' + std::to_string(endpoint.port) + '/';
}
void configure(CURL* curl, const Endpoint& endpoint, const std::string& password,
    const std::string& scheme, const std::atomic<bool>& cancelled)
{
    if (cancelled) throw NetworkError(NetworkFailure::Cancelled, "Connection cancelled.");
    curlCheck(curl_easy_setopt(curl, CURLOPT_URL, url(endpoint, scheme).c_str()));
    curlCheck(curl_easy_setopt(curl, CURLOPT_USERNAME, endpoint.username.c_str()));
    curlCheck(curl_easy_setopt(curl, CURLOPT_PASSWORD, password.c_str()));
    curlCheck(curl_easy_setopt(curl, CURLOPT_USE_SSL, static_cast<long>(CURLUSESSL_ALL)));
    curlCheck(curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L));
    curlCheck(curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L));
    curlCheck(curl_easy_setopt(curl, CURLOPT_SSLVERSION, static_cast<long>(CURL_SSLVERSION_TLSv1_2)));
    trust(curl, endpoint.certificate);
    curlCheck(curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 10000L));
    curlCheck(curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 30000L));
    curlCheck(curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L));
    curlCheck(curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L));
    curlCheck(curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progress));
    curlCheck(curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &cancelled));
    // A user's shell proxy settings must not redirect local Bridge credentials.
    if (endpoint.host == "localhost" || endpoint.host == "127.0.0.1" || endpoint.host == "::1")
        curlCheck(curl_easy_setopt(curl, CURLOPT_PROXY, ""));
}
class Connection {
public:
    Connection(const Endpoint& endpoint, const std::string& password, const std::string& scheme,
        const std::atomic<bool>& cancelled) : cancelled_(cancelled)
    {
        initializeCurl(); curl_.reset(curl_easy_init());
        if (!curl_) throw std::bad_alloc();
        configure(curl_.get(), endpoint, password, scheme, cancelled);
        curlCheck(curl_easy_setopt(curl_.get(), CURLOPT_CONNECT_ONLY, 1L));
        // curl 8.18's mail CONNECT_ONLY path does not initialize the transfer
        // timestamp used by CURLOPT_TIMEOUT_MS. Bound connect and each server
        // response instead; our raw commands also have a 30-second deadline.
        curlCheck(curl_easy_setopt(curl_.get(), CURLOPT_TIMEOUT_MS, 0L));
        curlCheck(curl_easy_setopt(curl_.get(), CURLOPT_SERVER_RESPONSE_TIMEOUT, 30L));
        curlCheck(curl_easy_perform(curl_.get()));
        curlCheck(curl_easy_getinfo(curl_.get(), CURLINFO_ACTIVESOCKET, &socket_));
        if (socket_ == CURL_SOCKET_BAD) protocol("The mail connection did not provide a usable socket.");
    }
    void begin() { deadline_ = Clock::now() + std::chrono::seconds(30); }
    void write(const std::string& data)
    {
        std::size_t sent = 0;
        while (sent < data.size()) {
            check(); std::size_t count = 0;
            auto result = curl_easy_send(curl_.get(), data.data() + sent, data.size() - sent, &count);
            if (result == CURLE_AGAIN) { wait(POLLOUT); continue; }
            curlCheck(result); if (!count) protocol("The mail server closed the connection."); sent += count;
        }
    }
    std::string line()
    {
        for (;;) {
            auto end = buffer_.find("\r\n");
            if (end != std::string::npos) {
                if (end > 65536) protocol("The mail server sent an oversized response line.");
                std::string line = buffer_.substr(0,end); buffer_.erase(0,end+2); return line;
            }
            if (buffer_.size() > 65536) protocol("The mail server sent an oversized response line.");
            receive();
        }
    }
    std::string exact(std::size_t size)
    {
        if (size > kMaxMessage) protocol("This message exceeds the current 32 MiB limit.");
        std::string out; out.reserve(size);
        while (out.size() < size) {
            if (buffer_.empty()) receive();
            auto count = std::min(buffer_.size(), size - out.size());
            out.append(buffer_,0,count); buffer_.erase(0,count);
        }
        return out;
    }
private:
    void check()
    {
        if (cancelled_) throw NetworkError(NetworkFailure::Cancelled, "Connection cancelled.");
        if (Clock::now() >= deadline_) throw NetworkError(NetworkFailure::Connection, "The mail server did not respond in time.");
    }
    void wait(short events)
    {
        check(); pollfd p{socket_,events,0};
        int result = poll(&p,1,100);
        if (result < 0 && errno != EINTR) protocol("Could not wait for the mail connection.");
    }
    void receive()
    {
        for (;;) {
            check(); char bytes[16384]; std::size_t received = 0;
            auto result = curl_easy_recv(curl_.get(),bytes,sizeof(bytes),&received);
            if (result == CURLE_AGAIN) { wait(POLLIN); continue; }
            curlCheck(result);
            if (!received) throw NetworkError(NetworkFailure::Connection, "The mail server closed the connection.");
            buffer_.append(bytes,received); return;
        }
    }
    struct Cleanup { void operator()(CURL* c) const { curl_easy_cleanup(c); } };
    std::unique_ptr<CURL,Cleanup> curl_;
    curl_socket_t socket_ = CURL_SOCKET_BAD;
    const std::atomic<bool>& cancelled_;
    Clock::time_point deadline_;
    std::string buffer_;
};

struct Value {
    std::string text;
    std::vector<Value> children;
    bool list = false;
    bool literal = false;
};
class ResponseParser {
public:
    explicit ResponseParser(const std::string& input) : input_(input) {}
    std::vector<Value> parse(int depth = 0)
    {
        if (depth > 20) protocol("The mail server returned excessively nested data.");
        std::vector<Value> values;
        while (at_ < input_.size()) {
            char c = input_[at_];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') { ++at_; continue; }
            if (c == ')') { if (!depth) protocol("Unexpected closing IMAP list."); ++at_; return values; }
            Value value;
            if (c == '(') { ++at_; value.list = true; value.children = parse(depth+1); }
            else if (c == '"') {
                ++at_; bool closed = false;
                while (at_ < input_.size()) {
                    char byte = input_[at_++];
                    if (byte == '"') { closed = true; break; }
                    if (byte == '\\' && at_ < input_.size()) byte = input_[at_++];
                    value.text += byte;
                }
                if (!closed) protocol("The mail server returned an incomplete quoted string.");
            } else if (c == '{') {
                auto end = input_.find("}\r\n",at_);
                if (end == std::string::npos) protocol("Invalid IMAP literal.");
                auto size = number(input_.substr(at_+1,end-at_-1)); at_ = end+3;
                if (size > input_.size()-at_) protocol("Truncated IMAP literal.");
                value.literal = true; value.text = input_.substr(at_,size); at_ += size;
            } else {
                auto start = at_;
                while (at_ < input_.size() && input_[at_] != ' ' && input_[at_] != '(' && input_[at_] != ')'
                    && input_[at_] != '\r' && input_[at_] != '\n' && input_[at_] != '\t') ++at_;
                value.text = input_.substr(start,at_-start);
            }
            values.push_back(std::move(value));
            if (values.size() > 200000) protocol("The mail server returned too many response fields.");
        }
        if (depth) protocol("The mail server returned an incomplete IMAP list.");
        return values;
    }
private:
    const std::string& input_;
    std::size_t at_ = 0;
};
std::string utf8Point(unsigned cp)
{
    std::string out;
    if (cp < 0x80) out += static_cast<char>(cp);
    else if (cp < 0x800) { out += static_cast<char>(0xc0|(cp>>6)); out += static_cast<char>(0x80|(cp&63)); }
    else if (cp < 0x10000) { out += static_cast<char>(0xe0|(cp>>12)); out += static_cast<char>(0x80|((cp>>6)&63)); out += static_cast<char>(0x80|(cp&63)); }
    else { out += static_cast<char>(0xf0|(cp>>18)); out += static_cast<char>(0x80|((cp>>12)&63)); out += static_cast<char>(0x80|((cp>>6)&63)); out += static_cast<char>(0x80|(cp&63)); }
    return out;
}
std::string mailboxName(const std::string& wire)
{
    std::string out;
    for (std::size_t i=0; i<wire.size(); ++i) {
        if (wire[i] != '&') { out += wire[i]; continue; }
        auto end = wire.find('-',i+1);
        if (end == std::string::npos) return wire;
        if (end == i+1) { out += '&'; i=end; continue; }
        auto encoded = wire.substr(i+1,end-i-1);
        std::replace(encoded.begin(),encoded.end(),',','/');
        while (encoded.size()%4) encoded += '=';
        try {
            auto decoded = base64Decode(encoded);
            if (decoded.size()%2) return wire;
            for (std::size_t p=0; p<decoded.size(); p+=2) {
                unsigned cp = static_cast<unsigned char>(decoded[p])*256 + static_cast<unsigned char>(decoded[p+1]);
                if (cp>=0xd800 && cp<=0xdbff && p+3<decoded.size()) {
                    unsigned low = static_cast<unsigned char>(decoded[p+2])*256 + static_cast<unsigned char>(decoded[p+3]);
                    if (low<0xdc00 || low>0xdfff) return wire;
                    cp=0x10000+((cp-0xd800)<<10)+(low-0xdc00); p+=2;
                } else if (cp>=0xd800 && cp<=0xdfff) return wire;
                out += utf8Point(cp);
            }
        } catch (const std::exception&) { return wire; }
        i=end;
    }
    return out;
}
using Responses = std::vector<std::vector<Value>>;
class ImapProvider : public MailProvider {
public:
    ImapProvider(const Account& account, const std::string& password, const std::atomic<bool>& cancelled)
        : connection_(account.incoming,password,"imap",cancelled)
    {
        for (const auto& response : command("CAPABILITY"))
            if (response.size()>2 && upper(response[1].text)=="CAPABILITY")
                for (std::size_t i=2; i<response.size(); ++i) capabilities_.insert(upper(response[i].text));
    }
    std::vector<RemoteFolder> folders() override
    {
        std::vector<RemoteFolder> folders;
        for (const auto& response : command("LIST \"\" \"*\"")) {
            if (response.size()<5 || upper(response[1].text)!="LIST" || !response[2].list) continue;
            RemoteFolder folder; folder.id=response[4].text;
            if (folder.id.empty() || folder.id.size()>1024 || hasControl(folder.id)) protocol("The server returned an invalid mailbox name.");
            folder.name=mailboxName(folder.id); bool selectable=true;
            if (upper(folder.id)=="INBOX") folder.role="inbox";
            static const std::map<std::string,std::string> roles={{"\\ARCHIVE","archive"},{"\\SENT","sent"},
                {"\\DRAFTS","drafts"},{"\\TRASH","trash"},{"\\JUNK","junk"}};
            for (const auto& flag : response[2].children) {
                auto value=upper(flag.text); if (value=="\\NOSELECT") selectable=false;
                auto role=roles.find(value); if (role!=roles.end()) folder.role=role->second;
            }
            if (selectable) folders.push_back(std::move(folder));
        }
        if (std::none_of(folders.begin(),folders.end(),[](const RemoteFolder& f){return f.role=="inbox";}))
            folders.insert(folders.begin(),{"INBOX","Inbox","inbox"});
        return folders;
    }
    MailboxSnapshot snapshot(const RemoteFolder& folder) override
    {
        MailboxSnapshot snapshot; snapshot.folder=folder; snapshot.epoch=select(folder.id,&snapshot.nextUid);
        std::set<std::string> seen;
        for (const auto& response : command("UID FETCH 1:* (UID FLAGS)")) {
            auto entry=parseEntry(response);
            if (entry.locator.empty()) continue;
            if (!seen.insert(entry.locator).second) protocol("The server returned duplicate message identifiers.");
            entry.key=imapRemoteKey({folder.id,snapshot.epoch,entry.locator});
            snapshot.messages.push_back(std::move(entry));
        }
        return snapshot;
    }
    std::string fetch(const MailboxSnapshot& snapshot, const RemoteMessage& message) override
    {
        ensure({snapshot.folder.id,snapshot.epoch,message.locator});
        for (const auto& response : command("UID FETCH "+message.locator+" (UID BODY.PEEK[])")) {
            auto entry=parseEntry(response);
            if (entry.locator!=message.locator || response.size()<4) continue;
            const auto& fields=response[3].children;
            for (std::size_t i=0; i+1<fields.size(); ++i)
                if (upper(fields[i].text)=="BODY[]" && fields[i+1].literal) return fields[i+1].text;
        }
        protocol("The message disappeared before it could be downloaded.");
    }
    void setFlag(const RemoteLocation& location, bool readFlag, bool value) override
    {
        ensure(location); requirePresent(location);
        command("UID STORE "+location.locator+(value?" +FLAGS.SILENT (":" -FLAGS.SILENT (")+(readFlag?"\\Seen)":"\\Flagged)"));
    }
    bool remove(const RemoteLocation& location) override
    {
        ensure(location);
        if (!present(location)) return true; // Another client already removed it.
        command("UID STORE "+location.locator+" +FLAGS.SILENT (\\Deleted)");
        if (capabilities_.count("UIDPLUS") || capabilities_.count("IMAP4REV2")) {
            // Never EXPUNGE unrelated messages marked by another client.
            try { command("UID EXPUNGE "+location.locator); }
            catch (const NetworkError& error) {
                // Some accounts may mark Deleted without permission to expunge.
                if (error.reason!=NetworkFailure::Rejected) throw;
            }
        }
        return !present(location);
    }
    RemoteLocation move(const RemoteLocation& location, const RemoteFolder& target) override
    {
        auto plan=planMove(location,target);
        return resumeMove(plan,{},[](const RemoteMove&){});
    }
    RemoteMove planMove(const RemoteLocation& location,const RemoteFolder& target) override
    {
        ensure(location); requirePresent(location);
        if (!capabilities_.count("UIDPLUS") && !capabilities_.count("IMAP4REV2"))
            throw NetworkError(NetworkFailure::Unsupported,"This server needs UIDPLUS for safe folder moves. The move remains pending.");
        require(location.folder!=target.id,"A move needs a different destination folder.");
        auto targetState=snapshot(target);
        std::uint64_t last=0;
        for (const auto& entry:targetState.messages) last=std::max(last,number(entry.locator));
        if (last==0xffffffffu) protocol("The destination mailbox has exhausted its message identifiers.");
        return {location,{target.id,targetState.epoch,{}},std::to_string(last+1),"ready",
            capabilities_.count("MOVE")!=0 || capabilities_.count("IMAP4REV2")!=0};
    }
    RemoteLocation resumeMove(RemoteMove& move,const std::string& raw,
        const std::function<void(const RemoteMove&)>& checkpoint) override
    {
        uid(move.firstUid);
        if (move.stage=="ready") {
            ensure(move.source); requirePresent(move.source);
            // Persist before transmission, including the process-crash window.
            move.stage="started"; checkpoint(move);
            Responses responses;
            try {
                responses=command(std::string(move.atomic?"UID MOVE ":"UID COPY ")
                    +move.source.locator+' '+quoted(move.destination.folder));
            } catch (const NetworkError& error) {
                // COPY with tagged NO/BAD is atomic. MOVE can leave a copy even
                // with NO (RFC 6851), so its uncertain journal must remain.
                if (!move.atomic && error.reason==NetworkFailure::Rejected) {
                    move.stage="ready"; checkpoint(move);
                }
                throw;
            }
            for (const auto& response:responses) {
                for (std::size_t i=0;i+3<response.size();++i) if (upper(response[i].text)=="[COPYUID") {
                    auto source=response[i+2].text,destination=response[i+3].text;
                    if (destination.empty() || destination.back()!=']') protocol("The move receipt is incomplete.");
                    destination.pop_back(); uid(response[i+1].text); uid(source); uid(destination);
                    if (source!=move.source.locator || response[i+1].text!=move.destination.epoch
                        || number(destination)<number(move.firstUid)) protocol("The server returned an unexpected move identifier.");
                    if (!move.destination.locator.empty() && move.destination.locator!=destination)
                        protocol("The server returned conflicting move receipts.");
                    move.destination.locator=destination;
                }
            }
            if (!move.destination.locator.empty()) {
                move.stage=move.atomic?"moved":"copied"; checkpoint(move);
            }
        }
        if (move.stage=="started") {
            if (raw.empty()) protocol("The folder move has no receipt. Check the account before retrying.");
            // An existing duplicate must never be mistaken for this operation.
            // Only newer UIDs in the original target epoch can be candidates.
            auto target=snapshot({move.destination.folder,move.destination.folder,{}});
            if (target.epoch!=move.destination.epoch) protocol("The move destination's identifiers changed. The original message is retained.");
            std::vector<RemoteMessage> candidates;
            for (const auto& entry:target.messages)
                if (number(entry.locator)>=number(move.firstUid)) candidates.push_back(entry);
            if (candidates.size()>100) protocol("Too many new destination messages to verify the interrupted move. The move remains pending.");
            std::string match;
            for (const auto& entry:candidates) if (fetch(target,entry)==raw) {
                if (!match.empty()) protocol("Several destination copies match the interrupted move. The original is retained for review.");
                match=entry.locator;
            }
            if (match.empty()) protocol("The folder move is unconfirmed. Mail will check again without creating another copy.");
            move.destination.locator=match; move.stage="copied"; checkpoint(move);
        }
        if (move.stage!="copied" && move.stage!="moved") protocol("The saved folder move has an invalid state.");
        ensure(move.destination); requirePresent(move.destination);
        if (move.stage=="copied") {
            if (!capabilities_.count("UIDPLUS") && !capabilities_.count("IMAP4REV2"))
                throw NetworkError(NetworkFailure::Unsupported,"The destination copy is saved, but this server cannot safely remove its original.");
            ensure(move.source);
            if (present(move.source)) {
                command("UID STORE "+move.source.locator+" +FLAGS.SILENT (\\Deleted)");
                // Never EXPUNGE unrelated messages marked by another client.
                command("UID EXPUNGE "+move.source.locator);
                if (present(move.source)) protocol("The server retained the original message after the move. Mail will retry its removal.");
            }
            move.stage="moved"; checkpoint(move);
        }
        return move.destination;
    }
    std::optional<RemoteLocation> findMessage(const RemoteFolder& folder,const std::string& messageId) override
    {
        require(!messageId.empty(),"The Sent copy has no Message-ID.");
        const auto epoch=select(folder.id);
        std::set<std::string> matches; bool searchReceived=false;
        for (const auto& response:command("UID SEARCH HEADER Message-ID "+quoted(messageId))) {
            if (response.size()<2 || response[0].text!="*" || upper(response[1].text)!="SEARCH") continue;
            searchReceived=true;
            for (std::size_t i=2;i<response.size();++i) { uid(response[i].text); matches.insert(response[i].text); }
        }
        if (!searchReceived || matches.size()>100) protocol("The Sent copy search returned an invalid or oversized result.");
        for (const auto& locator:matches) {
            bool received=false;
            for (const auto& response:command("UID FETCH "+locator+" (UID BODY.PEEK[HEADER.FIELDS (MESSAGE-ID)])")) {
                if (parseEntry(response).locator!=locator) continue;
                const auto& fields=response[3].children;
                for (std::size_t i=0;i+3<fields.size();++i) {
                    if (upper(fields[i].text)!="BODY[HEADER.FIELDS" || !fields[i+1].list
                        || fields[i+1].children.size()!=1 || upper(fields[i+1].children[0].text)!="MESSAGE-ID"
                        || fields[i+2].text!="]" || !fields[i+3].literal) continue;
                    if (fields[i+3].text.size()>65536) protocol("The Sent copy header is too large.");
                    received=true;
                    if (parseMessage(fields[i+3].text).messageId==messageId) return RemoteLocation{folder.id,epoch,locator};
                }
            }
            if (!received) protocol("The Sent search candidate could not be verified. Check again before uploading a copy.");
        }
        return std::nullopt;
    }
    std::optional<RemoteLocation> append(const RemoteFolder& folder,const std::string& raw,Timestamp timestamp) override
    { return appendMessage(folder,raw,timestamp,false); }
    std::optional<RemoteLocation> appendDraft(const RemoteFolder& folder,const std::string& raw,Timestamp timestamp) override
    { return appendMessage(folder,raw,timestamp,true); }
    std::optional<RemoteDraft> fetchDraft(const RemoteLocation& location) override
    {
        ensure(location); RemoteDraft result; result.location=location;
        bool present=false,flags=false,body=false;
        for (const auto& response:command("UID FETCH "+location.locator+" (UID FLAGS BODY.PEEK[])")) {
            const auto entry=parseEntry(response); if (entry.locator!=location.locator) continue;
            present=true;
            if (entry.flagsKnown) { flags=true; result.draft=entry.draft; result.deleted=entry.deleted; }
            const auto& fields=response[3].children;
            for (std::size_t i=0;i+1<fields.size();++i) if (upper(fields[i].text)=="BODY[]" && fields[i+1].literal) {
                if (body && result.raw!=fields[i+1].text) protocol("The server returned conflicting draft contents.");
                result.raw=fields[i+1].text; body=true;
            }
        }
        if (!present) return std::nullopt;
        if (!flags || !body) protocol("The server draft could not be read completely. Its local copy is retained.");
        return result;
    }
    void removeDraft(const RemoteLocation& location,const std::string& expectedRaw) override
    {
        auto current=fetchDraft(location);
        if (!current || current->deleted) return;
        if (!current->draft || current->raw!=expectedRaw)
            protocol("This server draft changed. It has been kept for review.");
        command("UID STORE "+location.locator+" +FLAGS.SILENT (\\Deleted)");
        if (capabilities_.count("UIDPLUS") || capabilities_.count("IMAP4REV2")) {
            try { command("UID EXPUNGE "+location.locator); }
            catch (const NetworkError& error) {
                // Some accounts may mark Deleted without permission to expunge.
                // The base-protocol result is still valid if the flag is confirmed.
                if (error.reason!=NetworkFailure::Rejected) throw;
            }
        }
        for (const auto& response:command("UID FETCH "+location.locator+" (UID FLAGS)")) {
            auto entry=parseEntry(response); if (entry.locator!=location.locator) continue;
            if (!entry.flagsKnown || !entry.deleted) protocol("The server retained this draft. Mail will check its removal again.");
        }
    }
private:
    std::optional<RemoteLocation> appendMessage(const RemoteFolder& folder,const std::string& raw,Timestamp timestamp,bool draft)
    {
        require(!raw.empty() && raw.size()<=kMaxMessage,"The message copy is empty or exceeds 32 MiB.");
        std::time_t time=timestamp; std::tm date{}; require(gmtime_r(&time,&date)!=nullptr,"Invalid message date.");
        static const char* months[]={"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
        char formatted[64]; std::snprintf(formatted,sizeof(formatted),"%02d-%s-%04d %02d:%02d:%02d +0000",date.tm_mday,months[date.tm_mon],date.tm_year+1900,date.tm_hour,date.tm_min,date.tm_sec);
        bool started=false;
        try {
            auto responses=command("APPEND "+quoted(folder.id)+(draft?" (\\Seen \\Draft) ":" (\\Seen) ")+quoted(formatted)+" {"+std::to_string(raw.size())+"}",&raw,&started);
            const auto& response=responses.back(); // Only this command's tagged result is authoritative.
            for (std::size_t i=2;i+2<response.size();++i) if (upper(response[i].text)=="[APPENDUID") {
                auto locator=response[i+2].text;
                if (locator.empty() || locator.back()!=']') protocol("Invalid APPENDUID response.");
                locator.pop_back(); uid(response[i+1].text); uid(locator);
                return RemoteLocation{folder.id,response[i+1].text,locator};
            }
            return std::nullopt;
        } catch (const NetworkError&) {
            if (started) throw NetworkError(NetworkFailure::CopyUnknown,draft
                ?"The server draft save is unconfirmed. Mail will check before another upload."
                :"The connection ended without a usable Sent copy receipt. The server may have saved it; Mail will check before another upload.");
            throw;
        }
    }
    RemoteMessage parseEntry(const std::vector<Value>& response)
    {
        RemoteMessage entry;
        if (response.size()<4 || upper(response[2].text)!="FETCH" || !response[3].list) return entry;
        const auto& fields=response[3].children;
        for (std::size_t i=0; i+1<fields.size(); ++i) {
            auto field=upper(fields[i].text);
            if (field=="UID") { entry.locator=fields[i+1].text; uid(entry.locator); }
            if (field=="FLAGS" && fields[i+1].list) {
                entry.flagsKnown=true;
                for (const auto& flag : fields[i+1].children) {
                    if (upper(flag.text)=="\\SEEN") entry.unread=false;
                    if (upper(flag.text)=="\\FLAGGED") entry.starred=true;
                    if (upper(flag.text)=="\\DRAFT") entry.draft=true;
                    if (upper(flag.text)=="\\DELETED") entry.deleted=true;
                }
            }
        }
        return entry;
    }
    std::string select(const std::string& folder,std::string* nextUid=nullptr)
    {
        auto responses=command("SELECT "+quoted(folder));
        if (nextUid) nextUid->clear();
        for (const auto& response:responses) for (const auto& field:response)
            if (upper(field.text)=="[UIDNOTSTICKY]") throw NetworkError(NetworkFailure::Unsupported,"This mailbox does not keep stable identifiers.");
        std::string epoch;
        for (const auto& response : responses)
            if (response.size()>3 && response[0].text=="*" && upper(response[1].text)=="OK"
                && (upper(response[2].text)=="[UIDVALIDITY" || (nextUid && upper(response[2].text)=="[UIDNEXT"))) {
                auto value=response[3].text;
                if (value.empty() || value.back()!=']') protocol("The mailbox identifier response is incomplete.");
                value.pop_back(); uid(value);
                auto& destination=upper(response[2].text)=="[UIDVALIDITY"?epoch:*nextUid;
                if (!destination.empty() && destination!=value) protocol("The mailbox returned conflicting identifiers.");
                destination=value;
            }
        if (!epoch.empty()) return epoch;
        protocol("The server did not provide a mailbox UIDVALIDITY.");
    }
    void ensure(const RemoteLocation& location)
    {
        uid(location.locator); uid(location.epoch);
        if (select(location.folder)!=location.epoch) protocol("The mailbox identifiers changed. Refresh this account before applying changes.");
    }
    void requirePresent(const RemoteLocation& location)
    {
        if (present(location)) return;
        protocol("This message is no longer in its original server folder. Refresh the account.");
    }
    bool present(const RemoteLocation& location)
    {
        for (const auto& response : command("UID FETCH "+location.locator+" (UID FLAGS)"))
            if (parseEntry(response).locator==location.locator) return true;
        return false;
    }
    Responses command(const std::string& text,const std::string* literal=nullptr,bool* started=nullptr)
    {
        require(!hasControl(text),"Invalid IMAP command.");
        const std::string tag="M"+std::to_string(++sequence_);
        connection_.begin(); connection_.write(tag+' '+text+"\r\n");
        Responses responses; std::size_t total=0; bool continued=false;
        for (;;) {
            std::string data;
            for (;;) {
                auto line=connection_.line();
                total += line.size()+2;
                if (total>kMaxResponse) protocol("The mail server returned too much data.");
                data+=line+"\r\n";
                if (line.empty() || line.back()!='}') break;
                auto start=line.rfind('{');
                if (start==std::string::npos) break;
                auto size=number(line.substr(start+1,line.size()-start-2));
                if (size>kMaxMessage || size>kMaxResponse-total) protocol("This message exceeds the current 32 MiB limit.");
                data+=connection_.exact(static_cast<std::size_t>(size)); total+=size;
            }
            auto response=ResponseParser(data).parse();
            if (!response.empty() && response[0].text=="+") {
                if (!literal || continued) protocol("Unexpected IMAP continuation.");
                continued=true; *started=true; connection_.write(*literal); connection_.write("\r\n"); continue;
            }
            if (response.size()<2) protocol("The mail server returned an incomplete response.");
            const bool complete=response[0].text==tag;
            if (upper(response[1].text)=="BYE") throw NetworkError(NetworkFailure::Connection,"The mail server closed the session.");
            if (complete && upper(response[1].text)!="OK") {
                // Tagged NO/BAD rejects the atomic APPEND, including after its literal.
                if (started && (upper(response[1].text)=="NO" || upper(response[1].text)=="BAD")) *started=false;
                throw NetworkError(NetworkFailure::Rejected,"The mail server rejected the request ("+text.substr(0,text.find(' '))+").");
            }
            if (complete && literal && !continued) protocol("The server accepted APPEND without requesting the message.");
            responses.push_back(std::move(response));
            if (complete) return responses;
        }
    }
    Connection connection_;
    unsigned sequence_=0;
    std::set<std::string> capabilities_;
};

class PopProvider : public MailProvider {
public:
    PopProvider(const Account& account,const std::string& password,const std::atomic<bool>& cancelled)
        : connection_(account.incoming,password,"pop3",cancelled) {}
    std::vector<RemoteFolder> folders() override { return {{"INBOX","Inbox","inbox"}}; }
    MailboxSnapshot snapshot(const RemoteFolder& folder) override
    {
        MailboxSnapshot out; out.folder=folder;
        const auto data=command("UIDL",true); std::istringstream lines(data); std::string line;
        std::set<std::string> locators,keys;
        while (std::getline(lines,line)) {
            std::istringstream fields(line); std::string index,key,extra;
            if (!(fields>>index>>key) || (fields>>extra) || !number(index) || key.size()>70 || hasControl(key))
                protocol("The POP3 server returned an invalid UIDL list.");
            if (!locators.insert(index).second || !keys.insert(key).second) protocol("The POP3 server returned duplicate UIDLs.");
            out.messages.push_back({"pop3:"+key,index,true,false});
        }
        return out;
    }
    std::string fetch(const MailboxSnapshot&,const RemoteMessage& entry) override
    {
        if (!number(entry.locator)) protocol("Invalid POP3 message number.");
        std::istringstream identity(command("UIDL "+entry.locator,false));
        std::string ok,index,key; identity>>ok>>index>>key;
        if (index!=entry.locator || "pop3:"+key!=entry.key) protocol("The POP3 mailbox changed during download. Refresh the account.");
        return command("RETR "+entry.locator,true);
    }
    void setFlag(const RemoteLocation&,bool,bool) override { throw NetworkError(NetworkFailure::Unsupported,"POP3 read and star state is kept on this computer."); }
    RemoteLocation move(const RemoteLocation&,const RemoteFolder&) override { throw NetworkError(NetworkFailure::Unsupported,"POP3 folders are kept on this computer."); }
private:
    std::string command(const std::string& text,bool multiline)
    {
        connection_.begin(); connection_.write(text+"\r\n");
        auto result=connection_.line();
        if (result.compare(0,3,"+OK")!=0) protocol("The POP3 server rejected the request.");
        if (!multiline) return result;
        std::string data;
        for (;;) {
            auto line=connection_.line();
            if (line==".") break;
            if (!line.empty() && line.front()=='.') line.erase(0,1);
            if (line.size()+2>kMaxMessage-data.size()) protocol("This message exceeds the current 32 MiB limit.");
            data+=line+"\r\n";
        }
        return data;
    }
    Connection connection_;
};
void validateServer(const Endpoint&);
void validateEndpoint(const Endpoint& e)
{
    validateServer(e);
    require(!e.username.empty() && !hasControl(e.username),"Enter a valid server username.");
    require(e.security==Security::TLS || e.security==Security::StartTLS,"Choose TLS or STARTTLS.");
    if (isPemText(e.certificate)) normalizePem(e.certificate);
    else require(e.certificate.size()<=4096 && !hasControl(e.certificate),"The certificate path contains an invalid character.");
}
void validateServer(const Endpoint& e)
{
    require(!e.host.empty() && e.host.size()<=253 && !hasControl(e.host) && e.host.find_first_of(" /\\@?#%") == std::string::npos,
        "Enter a server hostname without a URL prefix or path.");
    require(e.port>0 && e.port<=65535,"Enter a port between 1 and 65535.");
}
struct Upload { const std::string& data; std::size_t offset=0; };
size_t upload(char* bytes,size_t size,size_t count,void* user)
{
    auto& state=*static_cast<Upload*>(user); const auto length=std::min(size*count,state.data.size()-state.offset);
    std::memcpy(bytes,state.data.data()+state.offset,length); state.offset+=length; return length;
}
size_t discard(char*,size_t size,size_t count,void*) { return size*count; }
bool envelopeAddress(const std::string& address)
{
    return !address.empty() && address.size()<=320 && address.find('@')!=std::string::npos
        && !hasControl(address) && address.find_first_of(" <>\"\\,;")==std::string::npos;
}
}
SentCopyMode sentCopyMode(const Account& account)
{
    if (account.provider!=Provider::Imap && account.provider!=Provider::ProtonBridge) return SentCopyMode::Local;
    if (account.sentMode!=SentCopyMode::Automatic) return account.sentMode;
    return account.provider==Provider::ProtonBridge?SentCopyMode::Server:SentCopyMode::Upload;
}
bool MailProvider::remove(const RemoteLocation&)
{ throw NetworkError(NetworkFailure::Unsupported,"This account cannot delete messages on the server."); }
RemoteMove MailProvider::planMove(const RemoteLocation&,const RemoteFolder&)
{ throw NetworkError(NetworkFailure::Unsupported,"This account does not support server folder moves."); }
RemoteLocation MailProvider::resumeMove(RemoteMove&,const std::string&,const std::function<void(const RemoteMove&)>&)
{ throw NetworkError(NetworkFailure::Unsupported,"This account does not support server folder moves."); }
std::optional<RemoteLocation> MailProvider::findMessage(const RemoteFolder&,const std::string&)
{ throw NetworkError(NetworkFailure::Unsupported,"This account cannot search for server Sent copies."); }
std::optional<RemoteLocation> MailProvider::append(const RemoteFolder&,const std::string&,Timestamp)
{ throw NetworkError(NetworkFailure::Unsupported,"This account cannot save server Sent copies."); }
std::optional<RemoteLocation> MailProvider::appendDraft(const RemoteFolder&,const std::string&,Timestamp)
{ throw NetworkError(NetworkFailure::Unsupported,"This account cannot save server drafts."); }
std::optional<RemoteDraft> MailProvider::fetchDraft(const RemoteLocation&)
{ throw NetworkError(NetworkFailure::Unsupported,"This account cannot retrieve server drafts."); }
void MailProvider::removeDraft(const RemoteLocation&,const std::string&)
{ throw NetworkError(NetworkFailure::Unsupported,"This account cannot remove server drafts."); }
void validateAccount(const Account& account)
{
    require(account.sentMode>=SentCopyMode::Automatic && account.sentMode<=SentCopyMode::Local,"Choose a valid Sent copy setting.");
    require(account.sentFolder.size()<=1024 && !hasControl(account.sentFolder),"Enter a valid Sent folder name.");
    require(!account.name.empty() && !hasControl(account.name),"Enter an account name.");
    if (account.provider==Provider::Local) return;
    require(envelopeAddress(account.email),"Enter your email address.");
    if (account.provider==Provider::Gmail) return;
    require(account.provider==Provider::Imap || account.provider==Provider::Pop3 || account.provider==Provider::ProtonBridge,"Unknown mail provider.");
    validateEndpoint(account.incoming); validateEndpoint(account.outgoing);
    if (account.provider==Provider::ProtonBridge) for (const auto* e : {&account.incoming,&account.outgoing})
        require(e->host=="127.0.0.1" || e->host=="localhost" || e->host=="::1","Proton Mail Bridge must connect to a local loopback address.");
}
std::string imapRemoteKey(const RemoteLocation& l)
{
    return "imap:"+std::to_string(l.folder.size())+":"+l.folder+":"+l.epoch+":"+l.locator;
}
std::unique_ptr<MailProvider> connectMail(const Account& account,const std::string& password,const std::atomic<bool>& cancelled)
{
    validateAccount(account);
    require(password.find('\0')==std::string::npos,"The password contains a null character.");
    if (account.provider==Provider::Imap || account.provider==Provider::ProtonBridge)
        return std::make_unique<ImapProvider>(account,password,cancelled);
    if (account.provider==Provider::Pop3) return std::make_unique<PopProvider>(account,password,cancelled);
    throw NetworkError(NetworkFailure::Unsupported,"This account needs a different mail provider.");
}
void sendSmtp(const Account& account,const std::string& password,const std::string& from,
    const std::vector<std::string>& recipients,const std::string& raw,const std::atomic<bool>& cancelled)
{
    validateAccount(account); validateEndpoint(account.outgoing);
    require(password.find('\0')==std::string::npos,"The password contains a null character.");
    require(envelopeAddress(from),"Enter a valid sender address.");
    require(!recipients.empty(),"Add at least one recipient.");
    for (const auto& address : recipients) require(envelopeAddress(address),"A recipient address is invalid.");
    require(raw.size()<=kMaxMessage && !raw.empty(),"The outgoing message is empty or exceeds 32 MiB.");
    initializeCurl();
    std::unique_ptr<CURL,decltype(&curl_easy_cleanup)> curl(curl_easy_init(),curl_easy_cleanup);
    if (!curl) throw std::bad_alloc();
    configure(curl.get(),account.outgoing,password,"smtp",cancelled);
    curl_slist* list=nullptr;
    for (const auto& address : recipients) {
        auto* next=curl_slist_append(list,address.c_str());
        if (!next) { curl_slist_free_all(list); throw std::bad_alloc(); } list=next;
    }
    std::unique_ptr<curl_slist,decltype(&curl_slist_free_all)> owned(list,curl_slist_free_all);
    Upload state{raw};
    curlCheck(curl_easy_setopt(curl.get(),CURLOPT_MAIL_FROM,from.c_str()));
    curlCheck(curl_easy_setopt(curl.get(),CURLOPT_MAIL_RCPT,list));
    curlCheck(curl_easy_setopt(curl.get(),CURLOPT_READFUNCTION,upload));
    curlCheck(curl_easy_setopt(curl.get(),CURLOPT_READDATA,&state));
    curlCheck(curl_easy_setopt(curl.get(),CURLOPT_WRITEFUNCTION,discard));
    curlCheck(curl_easy_setopt(curl.get(),CURLOPT_UPLOAD,1L));
    curlCheck(curl_easy_setopt(curl.get(),CURLOPT_INFILESIZE_LARGE,static_cast<curl_off_t>(raw.size())));
    auto result=curl_easy_perform(curl.get());
    if (result!=CURLE_OK && state.offset)
        throw NetworkError(NetworkFailure::DeliveryUnknown,"The connection ended during sending. Delivery is uncertain; check Sent before retrying.");
    curlCheck(result);
}
CertificateDetails fetchServerCertificate(const Endpoint& endpoint,const std::string& scheme,const std::atomic<bool>& cancelled)
{
    validateServer(endpoint);
    require(scheme=="imap" || scheme=="pop3" || scheme=="smtp","Unknown mail protocol.");
    initializeCurl();
    std::unique_ptr<CURL,decltype(&curl_easy_cleanup)> curl(curl_easy_init(),curl_easy_cleanup);
    if (!curl) throw std::bad_alloc();
    if (cancelled) throw NetworkError(NetworkFailure::Cancelled,"Connection cancelled.");
    // No username or password: libcurl ends the connect phase after TLS or
    // STARTTLS, before authentication. Verification is off only so the
    // certificate can be shown for an explicit trust decision.
    curlCheck(curl_easy_setopt(curl.get(),CURLOPT_URL,url(endpoint,scheme).c_str()));
    curlCheck(curl_easy_setopt(curl.get(),CURLOPT_USE_SSL,static_cast<long>(CURLUSESSL_ALL)));
    curlCheck(curl_easy_setopt(curl.get(),CURLOPT_SSL_VERIFYPEER,0L));
    curlCheck(curl_easy_setopt(curl.get(),CURLOPT_SSL_VERIFYHOST,0L));
    curlCheck(curl_easy_setopt(curl.get(),CURLOPT_SSLVERSION,static_cast<long>(CURL_SSLVERSION_TLSv1_2)));
    curlCheck(curl_easy_setopt(curl.get(),CURLOPT_CERTINFO,1L));
    curlCheck(curl_easy_setopt(curl.get(),CURLOPT_CONNECT_ONLY,1L));
    curlCheck(curl_easy_setopt(curl.get(),CURLOPT_CONNECTTIMEOUT_MS,10000L));
    curlCheck(curl_easy_setopt(curl.get(),CURLOPT_SERVER_RESPONSE_TIMEOUT,15L));
    curlCheck(curl_easy_setopt(curl.get(),CURLOPT_NOSIGNAL,1L));
    curlCheck(curl_easy_setopt(curl.get(),CURLOPT_NOPROGRESS,0L));
    curlCheck(curl_easy_setopt(curl.get(),CURLOPT_XFERINFOFUNCTION,progress));
    curlCheck(curl_easy_setopt(curl.get(),CURLOPT_XFERINFODATA,&cancelled));
    if (endpoint.host=="localhost" || endpoint.host=="127.0.0.1" || endpoint.host=="::1")
        curlCheck(curl_easy_setopt(curl.get(),CURLOPT_PROXY,""));
    curlCheck(curl_easy_perform(curl.get()));
    curl_certinfo* chain=nullptr;
    curlCheck(curl_easy_getinfo(curl.get(),CURLINFO_CERTINFO,&chain));
    if (!chain || chain->num_of_certs<1) throw NetworkError(NetworkFailure::Protocol,"The server did not present a certificate.");
    for (auto* entry=chain->certinfo[0];entry;entry=entry->next)
        if (std::strncmp(entry->data,"Cert:",5)==0) return describeCertificate(entry->data+5);
    throw NetworkError(NetworkFailure::Protocol,"The server's certificate could not be read.");
}
}
