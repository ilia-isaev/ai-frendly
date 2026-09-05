#pragma once

#include <cstddef>
#include <functional>
#include <list>
#include <string>
#include <utility>
#include <vector>

#include <uv.h>
#include <libusockets.h>

// ===========================================================================
//  INTERFACE
// ===========================================================================

template <typename T>
struct Finished
{
    std::string url;
    int status = 0;
    std::string body;
    std::string error;
    std::vector<std::pair<std::string, std::string>> headers;
    T payload;
    const void* handle = nullptr;
};

template <typename T>
struct type_tag
{
    static const void* id()
    {
        static char storage;
        return &storage;
    }
};

enum class Framing
{
    close_delimited,
    content_length,
    chunked
};

struct ParsedUrl
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

ParsedUrl parse_url(const std::string& url);
std::string build_request(const ParsedUrl& p);
std::string build_request(const ParsedUrl& p, const std::string& body);

class Client;
class RecordBase;

struct SocketState
{
    Client* client = nullptr;
    RecordBase* record = nullptr;
    int ssl = 0;
};

class RecordBase
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
    bool socket_gone = false;
    std::string buf;
    std::string body;
    int status = 0;
    std::string error;
    std::function<void(RecordBase*)> commit;
    std::function<void(RecordBase*)> destroy;
    Framing framing = Framing::close_delimited;
    long body_start = -1;
    long content_length = -1;
    std::vector<std::pair<std::string, std::string>> resp_headers;

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

template <typename T>
struct Record final : RecordBase
{
    Finished<T> fin;

    Record(Client* c, const std::string& url_, T payload, const void* tid,
           const std::string& body = {})
    {
        client = c;
        type_id = tid;
        url = url_;

        parsed = parse_url(url_);
        if (parsed.ok)
            request = body.empty()
                ? build_request(parsed)
                : build_request(parsed, body);

        fin.url = url_;
        fin.payload = std::move(payload);
        fin.handle = &fin;
        finished_ptr = &fin;

        commit = [this](RecordBase* rb) {
            auto* r = static_cast<Record<T>*>(rb);
            fin.url = r->url;
            fin.status = r->status;
            fin.body = std::move(r->body);
            fin.headers = r->resp_headers;
            fin.error = std::move(r->error);
        };
        destroy = [this](RecordBase* rb) {
            delete static_cast<Record<T>*>(rb);
        };
    }
};

class Client
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
    void post(const std::string& url, const std::string& body, const T& payload)
    {
        start_request(url, type_tag<T>::id(),
            [url, body, payload](Client* c, const void* tid) -> void* {
                return new Record<T>(c, url, payload, tid, body);
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
