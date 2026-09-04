module;

#include <cctype>
#include <compare>
#include <concepts>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iterator>
#include <list>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

#include <uv.h>
#include <libusockets.h>
#include <openssl/ssl.h>

export module usurl;

// ===========================================================================
//  INTERFACE
// ===========================================================================

export template <typename T>
struct Finished
{
    std::string url;
    int status = 0;
    std::string body;
    std::string error;
    T payload;
    const void* handle = nullptr;
};

export template <typename T>
struct type_tag
{
    static const void* id()
    {
        static char storage;
        return &storage;
    }
};

export enum class Framing
{
    close_delimited,
    content_length,
    chunked
};

export struct ParsedUrl
{
    bool ok = false;
    bool is_ssl = false;
    std::string host;
    int port = 0;
    int default_port = 80;
    std::string path = "/";
    std::string path_and_query = "/";
    std::string host_header;
};

export ParsedUrl parse_url(const std::string& url);
export std::string build_request(const ParsedUrl& p);

export class Client;
export class RecordBase;

export struct SocketState
{
    Client* client = nullptr;
    RecordBase* record = nullptr;
    int ssl = 0;
};

export class RecordBase
{
public:
    Client* client = nullptr;
    const void* type_id = nullptr;
    void* finished_ptr = nullptr;
    std::string url;
    std::string request;
    std::size_t request_written = 0;
    ParsedUrl parsed;
    int ssl = 0;
    struct us_socket_t* socket = nullptr;
    SocketState* state = nullptr;
    bool finished = false;
    std::string buf;
    std::string body;
    int status = 0;
    std::string error;
    std::function<void(RecordBase*)> commit;
    std::function<void(RecordBase*)> destroy;
    Framing framing = Framing::close_delimited;
    long body_start = -1;
    long content_length = -1;

    void on_open(struct us_socket_t* s);
    void on_data(char* data, std::size_t len);
    void on_end();
    void on_writable(struct us_socket_t* s);
    void on_close(struct us_socket_t* s);
    void on_timeout();
    void on_connect_error(struct us_socket_t* s, int err);

    void process(const char* data, std::size_t len);
    void finalize();
    void finish();
    void detect_framing();
    void check_complete();
};

export template <typename T>
struct Record final : RecordBase
{
    Finished<T> fin;

    Record(Client* c, const std::string& url_, T payload, const void* tid)
    {
        client = c;
        type_id = tid;
        url = url_;

        parsed = parse_url(url_);
        if (parsed.ok)
            request = build_request(parsed);

        fin.url = url_;
        fin.payload = std::move(payload);
        fin.handle = &fin;
        finished_ptr = &fin;

        commit = [this](RecordBase* rb) {
            auto* r = static_cast<Record<T>*>(rb);
            fin.url = r->url;
            fin.status = r->status;
            fin.body = std::move(r->body);
            fin.error = std::move(r->error);
        };
        destroy = [this](RecordBase* rb) {
            delete static_cast<Record<T>*>(rb);
        };
    }
};

export class Client
{
public:
    explicit Client(uv_loop_t* loop);
    ~Client();

    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;

    uv_loop_t* loop() const { return loop_; }

    template <typename T>
    void get(const std::string& url, const T& payload)
    {
        start_request(url, type_tag<T>::id(),
            [url, payload](Client* c, const void* tid) -> void* {
                return new Record<T>(c, url, payload, tid);
            });
    }

    template <typename T>
    std::vector<Finished<T>> finished() const
    {
        std::vector<Finished<T>> out;
        for (auto* r : records) {
            if (r->finished && r->type_id == type_tag<T>::id())
                out.push_back(*static_cast<Finished<T>*>(r->finished_ptr));
        }
        return out;
    }

    void remove_finished(const void* handle);

private:
    using MakeFn = std::function<void*(Client*, const void*)>;

    void start_request(const std::string& url, const void* type_id, MakeFn make);
    void connect_record(RecordBase* rec);
    void destroy_record(RecordBase* rec);

    struct us_loop_t* usloop = nullptr;
    struct us_socket_context_t* tcp_ctx = nullptr;
    struct us_socket_context_t* ssl_ctx = nullptr;
    std::list<RecordBase*> records;
    uv_loop_t* loop_ = nullptr;
};

// ===========================================================================
//  IMPLEMENTATION
// ===========================================================================

namespace
{

constexpr char CRLF[] = "\r\n";

bool iequals(const std::string& a, const std::string& b)
{
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i]))
            != std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    return true;
}

std::string trim(const std::string& s)
{
    const std::size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos)
        return {};
    const std::size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

std::string header_value(const std::string& headers, const std::string& name)
{
    std::size_t pos = 0;
    while (pos < headers.size()) {
        const std::size_t eol = headers.find("\r\n", pos);
        const std::size_t end = (eol == std::string::npos) ? headers.size() : eol;
        const std::string line = headers.substr(pos, end - pos);
        const std::size_t colon = line.find(':');
        if (colon != std::string::npos) {
            const std::string n = trim(line.substr(0, colon));
            if (iequals(n, name))
                return trim(line.substr(colon + 1));
        }
        if (eol == std::string::npos)
            break;
        pos = eol + 2;
    }
    return {};
}

int hexval(char c)
{
    const unsigned char uc = static_cast<unsigned char>(c);
    if (uc >= '0' && uc <= '9')
        return uc - '0';
    if (uc >= 'a' && uc <= 'f')
        return uc - 'a' + 10;
    if (uc >= 'A' && uc <= 'F')
        return uc - 'A' + 10;
    return -1;
}

std::size_t chunk_size_at(const std::string& buf, std::size_t pos, bool& ok)
{
    ok = true;
    const std::size_t eol = buf.find("\r\n", pos);
    if (eol == std::string::npos) {
        ok = false;
        return 0;
    }
    std::string tok = buf.substr(pos, eol - pos);
    const std::size_t sc = tok.find(';');
    if (sc != std::string::npos)
        tok = tok.substr(0, sc);
    tok = trim(tok);
    if (tok.empty()) {
        ok = false;
        return 0;
    }
    std::size_t size = 0;
    for (char ch : tok) {
        const int v = hexval(ch);
        if (v < 0) {
            ok = false;
            return 0;
        }
        size = size * 16 + static_cast<std::size_t>(v);
    }
    return size;
}

bool chunked_complete(const std::string& buf)
{
    std::size_t pos = 0;
    while (pos < buf.size()) {
        bool ok = false;
        const std::size_t size = chunk_size_at(buf, pos, ok);
        if (!ok)
            return false;
        const std::size_t data_start = pos + (buf.find("\r\n", pos) - pos) + 2;
        if (size == 0)
            return buf.compare(data_start, 2, "\r\n") == 0;
        if (buf.size() < data_start + size + 2)
            return false;
        pos = data_start + size + 2;
    }
    return false;
}

std::string decode_chunked(const std::string& buf)
{
    std::string out;
    std::size_t pos = 0;
    while (pos < buf.size()) {
        bool ok = false;
        const std::size_t size = chunk_size_at(buf, pos, ok);
        if (!ok)
            break;
        const std::size_t data_start = pos + (buf.find("\r\n", pos) - pos) + 2;
        if (size == 0)
            break;
        if (buf.size() < data_start + size)
            break;
        out.append(buf, data_start, size);
        pos = data_start + size + 2;
    }
    return out;
}

SocketState* state_of(int ssl, struct us_socket_t* s)
{
    void* e = us_socket_ext(ssl, s);
    if (!e)
        return nullptr;
    return *static_cast<SocketState**>(e);
}

} // namespace

// ---------------------------------------------------------------------
//  URL parsing + request construction
// ---------------------------------------------------------------------

ParsedUrl parse_url(const std::string& url)
{
    ParsedUrl p;
    const std::size_t scheme = url.find("://");
    if (scheme == std::string::npos)
        return p;

    const std::string scheme_str = url.substr(0, scheme);
    const std::string rest = url.substr(scheme + 3);
    const std::size_t slash = rest.find('/');
    const std::string authority = (slash == std::string::npos) ? rest : rest.substr(0, slash);
    const std::string target = (slash == std::string::npos) ? "/" : rest.substr(slash);

    p.is_ssl = (scheme_str == "https");
    p.default_port = p.is_ssl ? 443 : 80;
    p.path = target;
    p.path_and_query = target;

    const std::size_t at = authority.find('@');
    const std::string hostport = (at == std::string::npos) ? authority : authority.substr(at + 1);

    std::string host;
    if (!hostport.empty() && hostport.front() == '[') {
        const std::size_t cbracket = hostport.find(']');
        if (cbracket == std::string::npos)
            return p;
        host = hostport.substr(1, cbracket - 1);
        const std::string after = hostport.substr(cbracket + 1);
        if (after.empty()) {
            p.port = p.default_port;
        } else if (after.front() == ':') {
            p.port = std::atoi(after.c_str() + 1);
        } else {
            return p;
        }
    } else {
        const std::size_t colon = hostport.find(':');
        host = (colon == std::string::npos) ? hostport : hostport.substr(0, colon);
        p.port = (colon == std::string::npos)
            ? p.default_port
            : std::atoi(hostport.c_str() + colon + 1);
    }

    if (host.empty())
        return p;
    if (p.port <= 0 || p.port > 65535)
        return p;

    p.host = host;
    p.host_header = host;
    if (p.port != p.default_port)
        p.host_header += ':' + std::to_string(p.port);
    p.ok = true;
    return p;
}

std::string build_request(const ParsedUrl& p)
{
    std::string req;
    req += "GET " + p.path_and_query + " HTTP/1.1\r\n";
    req += "Host: " + p.host_header + "\r\n";
    req += "Connection: close\r\n";
    req += "Accept: */*\r\n";
    req += "\r\n";
    return req;
}

// ---------------------------------------------------------------------
//  uSockets callbacks (TCP variants use ssl=0, SSL variants use ssl=1)
// ---------------------------------------------------------------------

struct us_socket_t* on_open_tcp(struct us_socket_t* s, int, char*, int)
{
    SocketState* st = state_of(0, s);
    if (st && st->record)
        st->record->on_open(s);
    return s;
}
struct us_socket_t* on_data_tcp(struct us_socket_t* s, char* data, int length)
{
    SocketState* st = state_of(0, s);
    if (st && st->record)
        st->record->on_data(data, static_cast<std::size_t>(length));
    return s;
}
struct us_socket_t* on_end_tcp(struct us_socket_t* s)
{
    SocketState* st = state_of(0, s);
    if (st && st->record)
        st->record->on_end();
    return s;
}
struct us_socket_t* on_writable_tcp(struct us_socket_t* s)
{
    SocketState* st = state_of(0, s);
    if (st && st->record)
        st->record->on_writable(s);
    return s;
}
struct us_socket_t* on_timeout_tcp(struct us_socket_t* s)
{
    SocketState* st = state_of(0, s);
    if (st && st->record)
        st->record->on_timeout();
    return s;
}
struct us_socket_t* on_close_tcp(struct us_socket_t* s, int, void*)
{
    SocketState* st = state_of(0, s);
    if (st && st->record)
        st->record->on_close(s);
    return s;
}
struct us_socket_t* on_connect_error_tcp(struct us_socket_t* s, int code)
{
    SocketState* st = state_of(0, s);
    if (st && st->record)
        st->record->on_connect_error(s, code);
    return s;
}

struct us_socket_t* on_open_ssl(struct us_socket_t* s, int, char*, int)
{
    SocketState* st = state_of(1, s);
    if (st && st->record)
        st->record->on_open(s);
    return s;
}
struct us_socket_t* on_data_ssl(struct us_socket_t* s, char* data, int length)
{
    SocketState* st = state_of(1, s);
    if (st && st->record)
        st->record->on_data(data, static_cast<std::size_t>(length));
    return s;
}
struct us_socket_t* on_end_ssl(struct us_socket_t* s)
{
    SocketState* st = state_of(1, s);
    if (st && st->record)
        st->record->on_end();
    return s;
}
struct us_socket_t* on_writable_ssl(struct us_socket_t* s)
{
    SocketState* st = state_of(1, s);
    if (st && st->record)
        st->record->on_writable(s);
    return s;
}
struct us_socket_t* on_timeout_ssl(struct us_socket_t* s)
{
    SocketState* st = state_of(1, s);
    if (st && st->record)
        st->record->on_timeout();
    return s;
}
struct us_socket_t* on_close_ssl(struct us_socket_t* s, int, void*)
{
    SocketState* st = state_of(1, s);
    if (st && st->record)
        st->record->on_close(s);
    return s;
}
struct us_socket_t* on_connect_error_ssl(struct us_socket_t* s, int code)
{
    SocketState* st = state_of(1, s);
    if (st && st->record)
        st->record->on_connect_error(s, code);
    return s;
}

void register_callbacks(struct us_socket_context_t* ctx, int ssl)
{
    if (ssl == 0) {
        us_socket_context_on_open(0, ctx, on_open_tcp);
        us_socket_context_on_data(0, ctx, on_data_tcp);
        us_socket_context_on_end(0, ctx, on_end_tcp);
        us_socket_context_on_writable(0, ctx, on_writable_tcp);
        us_socket_context_on_timeout(0, ctx, on_timeout_tcp);
        us_socket_context_on_close(0, ctx, on_close_tcp);
        us_socket_context_on_connect_error(0, ctx, on_connect_error_tcp);
    } else {
        us_socket_context_on_open(1, ctx, on_open_ssl);
        us_socket_context_on_data(1, ctx, on_data_ssl);
        us_socket_context_on_end(1, ctx, on_end_ssl);
        us_socket_context_on_writable(1, ctx, on_writable_ssl);
        us_socket_context_on_timeout(1, ctx, on_timeout_ssl);
        us_socket_context_on_close(1, ctx, on_close_ssl);
        us_socket_context_on_connect_error(1, ctx, on_connect_error_ssl);
    }
}

// ---------------------------------------------------------------------
//  RecordBase
// ---------------------------------------------------------------------

constexpr int kTimeoutSec = 4;

void RecordBase::on_open(struct us_socket_t* s)
{
    if (finished)
        return;
    socket = s;
    if (ssl) {
        SSL* sh = static_cast<SSL*>(us_socket_get_native_handle(1, s));
        if (sh)
            SSL_set_tlsext_host_name(sh, parsed.host.c_str());
    }
    request_written = static_cast<std::size_t>(
        us_socket_write(ssl, s, request.data(), static_cast<int>(request.size()), 0));
    us_socket_timeout(ssl, s, kTimeoutSec);
}

void RecordBase::on_data(char* data, std::size_t len)
{
    process(data, len);
}

void RecordBase::on_end()
{
    if (finished)
        return;
    if (framing == Framing::close_delimited)
        finalize();
}

void RecordBase::on_writable(struct us_socket_t* s)
{
    if (finished)
        return;
    if (ssl) {
        request_written = request.size();
        us_socket_flush(1, s);
    } else if (request_written < request.size()) {
        const std::size_t remaining = request.size() - request_written;
        const int w = us_socket_write(
            0, s, request.data() + request_written, static_cast<int>(remaining), 0);
        request_written += (w > 0) ? static_cast<std::size_t>(w) : 0;
    }
}

void RecordBase::on_close(struct us_socket_t*)
{
    if (!finished) {
        if (error.empty())
            error = "connection closed";
        finish();
    }
}

void RecordBase::on_timeout()
{
    if (!finished) {
        error = "timeout";
        finish();
    }
}

void RecordBase::on_connect_error(struct us_socket_t*, int)
{
    if (!finished) {
        error = "connect error";
        finish();
    }
}

void RecordBase::process(const char* data, std::size_t len)
{
    if (finished)
        return;
    buf.append(data, len);
    check_complete();
}

void RecordBase::detect_framing()
{
    if (body_start >= 0)
        return;
    const std::size_t hdr_end = buf.find("\r\n\r\n");
    if (hdr_end == std::string::npos)
        return;

    const std::size_t sp1 = buf.find(' ');
    const std::size_t sp2 =
        (sp1 == std::string::npos) ? std::string::npos : buf.find(' ', sp1 + 1);
    if (sp1 != std::string::npos && sp2 != std::string::npos)
        status = std::atoi(buf.substr(sp1 + 1, sp2 - sp1 - 1).c_str());

    body_start = static_cast<long>(hdr_end) + 4;
    const std::string headers = buf.substr(0, hdr_end);

    content_length = -1;
    framing = Framing::close_delimited;
    const std::string te = header_value(headers, "transfer-encoding");
    if (te.find("chunked") != std::string::npos) {
        framing = Framing::chunked;
    } else {
        const std::string cl = header_value(headers, "content-length");
        if (!cl.empty()) {
            content_length = std::strtol(cl.c_str(), nullptr, 10);
            framing = Framing::content_length;
        }
    }
}

void RecordBase::check_complete()
{
    if (finished)
        return;
    if (body_start < 0)
        detect_framing();
    if (body_start < 0)
        return;
    if (framing == Framing::content_length) {
        if (buf.size() >= static_cast<std::size_t>(body_start)
            + static_cast<std::size_t>(content_length))
            finalize();
    } else if (framing == Framing::chunked) {
        if (chunked_complete(buf))
            finalize();
    }
}

void RecordBase::finalize()
{
    if (finished)
        return;
    if (framing == Framing::content_length)
        body = buf.substr(
            static_cast<std::size_t>(body_start),
            static_cast<std::size_t>(content_length));
    else if (framing == Framing::chunked)
        body = decode_chunked(buf);
    else
        body = buf.substr(static_cast<std::size_t>(body_start));
    finish();
}

void RecordBase::finish()
{
    if (finished)
        return;
    finished = true;
    if (commit)
        commit(this);
}

// ---------------------------------------------------------------------
//  Client
// ---------------------------------------------------------------------

static void usurl_noop_cb(struct us_loop_t*)
{
}

Client::Client(uv_loop_t* loop)
{
    loop_ = loop;
    usloop = us_create_loop(loop, usurl_noop_cb, usurl_noop_cb, usurl_noop_cb, 0);

    us_socket_context_options_t opts = {};
    tcp_ctx = us_create_socket_context(0, usloop, sizeof(SocketState), opts);
    ssl_ctx = us_create_socket_context(1, usloop, sizeof(SocketState), opts);

    if (tcp_ctx)
        register_callbacks(tcp_ctx, 0);
    if (ssl_ctx)
        register_callbacks(ssl_ctx, 1);
}

Client::~Client()
{
    if (tcp_ctx)
        us_socket_context_close(0, tcp_ctx);
    if (ssl_ctx)
        us_socket_context_close(1, ssl_ctx);

    for (RecordBase* rec : records)
        destroy_record(rec);
    records.clear();

    if (tcp_ctx)
        us_socket_context_free(0, tcp_ctx);
    if (ssl_ctx)
        us_socket_context_free(1, ssl_ctx);

    us_loop_free(usloop);
}

void Client::start_request(const std::string& url, const void* type_id, MakeFn make)
{
    RecordBase* rec = static_cast<RecordBase*>(make(this, type_id));
    records.push_back(rec);
    connect_record(rec);
}

void Client::connect_record(RecordBase* rec)
{
    if (!rec->parsed.ok) {
        rec->error = "invalid URL";
        rec->finish();
        return;
    }

    rec->state = new SocketState();
    rec->state->client = this;
    rec->state->record = rec;
    rec->state->ssl = rec->ssl;

    struct us_socket_context_t* ctx = rec->ssl ? ssl_ctx : tcp_ctx;
    if (!ctx) {
        rec->error = "connect failed";
        rec->finish();
        delete rec->state;
        rec->state = nullptr;
        return;
    }

    struct us_socket_t* s = us_socket_context_connect(
        rec->ssl, ctx, rec->parsed.host.c_str(), rec->parsed.port, nullptr, 0,
        sizeof(SocketState));
    if (!s) {
        rec->error = "connect failed";
        rec->finish();
        delete rec->state;
        rec->state = nullptr;
        return;
    }

    rec->socket = s;
    void* ext = us_socket_ext(rec->ssl, s);
    *static_cast<SocketState**>(ext) = rec->state;
    us_socket_timeout(rec->ssl, s, kTimeoutSec);
}

void Client::destroy_record(RecordBase* rec)
{
    SocketState* st = rec->state;
    if (rec->destroy)
        rec->destroy(rec);
    delete st;
}

void Client::remove_finished(const void* handle)
{
    RecordBase* target = nullptr;
    for (RecordBase* r : records)
        if (r->finished && r->finished_ptr == handle) {
            target = r;
            break;
        }
    if (!target)
        return;
    std::erase_if(records, [handle](RecordBase* r) {
        return r->finished && r->finished_ptr == handle;
    });
    destroy_record(target);
}
