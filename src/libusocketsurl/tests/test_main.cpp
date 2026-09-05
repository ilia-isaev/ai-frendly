#include <cctype>
#include <chrono>
#include <compare>
#include <concepts>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iterator>
#include <list>
#include <memory>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

#include <atomic>
#include <iostream>

#include <uv.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <usurl.hpp>

static int g_failures = 0;

static void check(bool cond, const std::string& msg)
{
    std::cout << (cond ? "[PASS] " : "[FAIL] ") << msg << "\n";
    if (!cond)
        ++g_failures;
}

namespace
{

enum class ServerFraming
{
    ContentLength,
    Chunked,
    CloseDelimited
};

std::string to_hex(std::size_t n)
{
    if (n == 0)
        return "0";
    static const char dgt[] = "0123456789abcdef";
    std::string s;
    while (n > 0)
    {
        s.insert(s.begin(), 1, dgt[n & 0xFu]);
        n >>= 4;
    }
    return s;
}

std::string chunked_encode(const std::string& body)
{
    if (body.empty())
        return "0\r\n\r\n";
    const std::size_t mid = body.size() / 2;
    const std::string c1 = body.substr(0, mid);
    const std::string c2 = body.substr(mid);
    return to_hex(c1.size()) + "\r\n" + c1 + "\r\n"
         + to_hex(c2.size()) + "\r\n" + c2 + "\r\n"
         + "0\r\n\r\n";
}

// Caller-side JWT extraction (prompt: extraction stays with the caller).
// The auth endpoint answers `Set-Cookie: authorization: <JWT>`; the JWT is
// everything after "authorization:" with leading whitespace trimmed.
std::string extract_token(const Finished<std::string>& fin)
{
    static const std::string key = "authorization:";
    for (const auto& h : fin.headers)
    {
        if (h.first != "Set-Cookie")
            continue;
        const std::size_t p = h.second.find(key);
        if (p == std::string::npos)
            continue;
        std::string rest = h.second.substr(p + key.size());
        while (!rest.empty() && (rest.front() == ' ' || rest.front() == '\t'))
            rest.erase(rest.begin());
        return rest;
    }
    return {};
}

std::string build_response(const std::string& body, ServerFraming framing,
                           const std::string& extra_headers, int status)
{
    const std::string reason = (status == 200) ? "OK" : "Unauthorized";
    const std::string status_line =
        "HTTP/1.1 " + std::to_string(status) + " " + reason + "\r\n";
    switch (framing)
    {
    case ServerFraming::ContentLength:
        return status_line
             + "Content-Length: " + std::to_string(body.size()) + "\r\n"
             + "Connection: close\r\n" + extra_headers + "\r\n" + body;
    case ServerFraming::Chunked:
        return status_line
             + "Transfer-Encoding: chunked\r\n"
             + "Connection: close\r\n" + extra_headers + "\r\n" + chunked_encode(body);
    case ServerFraming::CloseDelimited:
        return status_line
             + "Connection: close\r\n" + extra_headers + "\r\n" + body;
    }
    return {};
}

long request_content_length(const std::string& head)
{
    const std::size_t p = head.find("Content-Length:");
    if (p == std::string::npos)
        return 0;
    const std::string rest = head.substr(p + 15);
    const std::size_t b = rest.find_first_not_of(" \t");
    if (b == std::string::npos)
        return 0;
    return std::strtol(rest.c_str() + b, nullptr, 10);
}

struct TcpServer
{
    int listen_fd = -1;
    int port = 0;
    std::string body;
    ServerFraming framing = ServerFraming::ContentLength;
    std::string extra_headers;
    int status = 200;
    std::string received_body;
    std::string received_head;
    std::string require_auth;
    std::atomic<bool> stop{ false };
    std::thread worker;

    ~TcpServer()
    {
        stop = true;
        if (listen_fd >= 0)
            ::close(listen_fd);
        if (worker.joinable())
            worker.join();
    }

    void start(const std::string& body_, ServerFraming framing_,
               const std::string& extra_headers_ = {}, int status_ = 200,
               const std::string& require_auth_ = {})
    {
        body = body_;
        framing = framing_;
        extra_headers = extra_headers_;
        status = status_;
        require_auth = require_auth_;
        received_head.clear();
        listen_fd = ::socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;
        ::bind(listen_fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr);
        ::listen(listen_fd, 16);
        timeval tv{};
        tv.tv_sec = 0;
        tv.tv_usec = 100000;
        ::setsockopt(listen_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
        socklen_t len = sizeof addr;
        ::getsockname(listen_fd, reinterpret_cast<sockaddr*>(&addr), &len);
        port = ntohs(addr.sin_port);
        worker = std::thread([this] { run(); });
    }

    void run()
    {
        for (;;)
        {
            if (stop)
                break;
            int conn = ::accept(listen_fd, nullptr, nullptr);
            if (conn < 0)
            {
                if (!stop)
                    continue;
                break;
            }
            std::string req;
            char buf[8192];
            std::size_t hdr_end = std::string::npos;
            long want = 0;
            for (;;)
            {
                const ssize_t n = ::read(conn, buf, sizeof buf);
                if (n <= 0)
                    break;
                req.append(buf, static_cast<std::size_t>(n));
                if (hdr_end == std::string::npos)
                {
                    hdr_end = req.find("\r\n\r\n");
                    if (hdr_end != std::string::npos)
                        want = request_content_length(req);
                }
                if (hdr_end == std::string::npos)
                    continue;
                if (req.size() >= hdr_end + 4 + static_cast<std::size_t>(want))
                    break;
            }
            int eff_status = status;
            if (hdr_end != std::string::npos)
            {
                received_head = req.substr(0, hdr_end);
                if (want > 0
                    && req.size() >= hdr_end + 4 + static_cast<std::size_t>(want))
                    received_body = req.substr(hdr_end + 4, static_cast<std::size_t>(want));
                if (!require_auth.empty()
                    && received_head.find("Authorization: Bearer " + require_auth)
                           == std::string::npos)
                    eff_status = 401;
            }
            const std::string resp = build_response(body, framing, extra_headers, eff_status);
            std::size_t off = 0;
            while (off < resp.size())
            {
                const ssize_t w = ::write(conn, resp.data() + off, resp.size() - off);
                if (w <= 0)
                    break;
                off += static_cast<std::size_t>(w);
            }
            ::shutdown(conn, SHUT_RDWR);
            ::close(conn);
        }
    }
};

struct StallServer
{
    int listen_fd = -1;
    int port = 0;
    int stall_ms = 0;
    std::atomic<bool> stop{ false };
    std::thread worker;

    ~StallServer()
    {
        stop = true;
        if (listen_fd >= 0)
            ::close(listen_fd);
        if (worker.joinable())
            worker.detach();
    }

    void start(int stall_ms_)
    {
        stall_ms = stall_ms_;
        listen_fd = ::socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;
        ::bind(listen_fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr);
        ::listen(listen_fd, 16);
        socklen_t len = sizeof addr;
        ::getsockname(listen_fd, reinterpret_cast<sockaddr*>(&addr), &len);
        port = ntohs(addr.sin_port);
        worker = std::thread([this] { run(); });
    }

    void run()
    {
        int conn = -1;
        for (;;)
        {
            conn = ::accept(listen_fd, nullptr, nullptr);
            if (conn >= 0 || stop)
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        if (conn < 0)
            return;
        std::this_thread::sleep_for(std::chrono::milliseconds(stall_ms));
        ::close(conn);
    }
};

int find_free_port()
{
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    ::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr);
    socklen_t len = sizeof addr;
    ::getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &len);
    const int port = ntohs(addr.sin_port);
    ::close(fd);
    return port;
}

} // namespace

int main()
{
    uv_loop_t* loop = uv_loop_new();

    auto pump = [loop](auto pred) {
        for (int i = 0; i < 4000; ++i)
        {
            if (pred())
                return true;
            uv_run(loop, UV_RUN_ONCE);
        }
        return pred();
    };

    auto pump_wall = [loop](auto pred, int deadline_s) {
        const auto t0 = std::chrono::steady_clock::now();
        for (;;)
        {
            if (pred())
                return true;
            const auto elapsed_s = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - t0)
                                         .count();
            if (elapsed_s >= deadline_s)
                return pred();
            uv_run(loop, UV_RUN_ONCE);
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    };

    const auto base_of = [](int port) {
        return std::string("http://127.0.0.1:") + std::to_string(port);
    };

    // --- content-length: cases 1-5 (success, remove, invalid, refused, multiple) ---
    {
        TcpServer server;
        server.start("hello", ServerFraming::ContentLength);
        const std::string base = base_of(server.port);

        Client client(loop);

        // Case 1: successful GET
        client.get<int>(base + "/hello", 42);
        const bool done = pump([&] { return !client.finished<int>().empty(); });
        check(done, "GET: request completed");
        {
            const std::vector<Finished<int>> fins = client.finished<int>();
            check(!fins.empty() && fins[0].status == 200, "GET: status is 200");
            check(!fins.empty() && fins[0].body == "hello", "GET: body matches");
            check(!fins.empty() && fins[0].payload == 42, "GET: payload preserved");
            bool conn_close = false;
            if (!fins.empty())
            {
                for (const auto& h : fins[0].headers)
                    if (h.first == "Connection" && h.second == "close")
                        conn_close = true;
            }
            check(conn_close, "GET: response headers parsed");
        }

        // Case 4: remove_finished clears a finished record
        {
            const std::vector<Finished<int>> fins = client.finished<int>();
            if (!fins.empty())
                client.remove_finished(fins[0].handle);
            check(client.finished<int>().empty(), "remove_finished: clears record");
        }

        // Case 2: invalid URL is flagged synchronously
        client.get<int>("not-a-url", 7);
        {
            const std::vector<Finished<int>> bad = client.finished<int>();
            check(
                !bad.empty() && bad[0].error == "invalid URL",
                "invalid URL: flagged immediately");
            if (!bad.empty())
                client.remove_finished(bad[0].handle);
        }

        // Case 3: connection refused reports an error
        const int dead_port = find_free_port();
        client.get<int>("http://127.0.0.1:" + std::to_string(dead_port) + "/x", 1);
        {
            const bool d3 = pump([&] { return !client.finished<int>().empty(); });
            const std::vector<Finished<int>> v3 = client.finished<int>();
            check(
                d3 && !v3.empty() && !v3[0].error.empty(),
                "refused: error reported");
            if (!v3.empty())
                client.remove_finished(v3[0].handle);
        }

        // Case 5: multiple GETs all complete
        client.get<int>(base + "/one", 1);
        client.get<int>(base + "/two", 2);
        {
            const bool d5 = pump([&] { return client.finished<int>().size() == 2; });
            const std::vector<Finished<int>> v5 = client.finished<int>();
            check(d5 && v5.size() == 2, "multiple: two finished records");

            // Selective removal: remove only the chosen record; the other
            // finished record must stay.
            const Finished<int>* chosen = nullptr;
            for (const auto& f : v5)
                if (f.payload == 1)
                    chosen = &f;
            if (d5 && v5.size() == 2 && chosen)
            {
                client.remove_finished(chosen->handle);
                const std::vector<Finished<int>> v5b = client.finished<int>();
                check(v5b.size() == 1, "selective remove: one record remains");
                check(v5b.size() == 1 && v5b[0].payload == 2,
                      "selective remove: unchosen record stays");
            }
        }
    }

    // --- T2: chunked transfer encoding ---
    {
        TcpServer server;
        server.start("hello", ServerFraming::Chunked);
        const std::string base = base_of(server.port);

        Client client(loop);
        client.get<int>(base + "/chunked", 5);
        const bool done = pump([&] { return !client.finished<int>().empty(); });
        check(done, "chunked: request completed");
        const std::vector<Finished<int>> fins = client.finished<int>();
        check(done && !fins.empty() && fins[0].status == 200, "chunked: status is 200");
        check(done && !fins.empty() && fins[0].body == "hello", "chunked: body matches");
        check(done && !fins.empty() && fins[0].payload == 5, "chunked: payload preserved");
    }

    // --- T3: close-delimited body (no Content-Length / chunked) ---
    {
        TcpServer server;
        server.start("close-body", ServerFraming::CloseDelimited);
        const std::string base = base_of(server.port);

        Client client(loop);
        client.get<int>(base + "/close", 9);
        const bool done = pump([&] { return !client.finished<int>().empty(); });
        check(done, "close-delimited: request completed");
        const std::vector<Finished<int>> fins = client.finished<int>();
        check(
            done && !fins.empty() && fins[0].status == 200,
            "close-delimited: status is 200");
        check(
            done && !fins.empty() && fins[0].body == "close-body",
            "close-delimited: body matches");
    }

    // --- T4: mixed payload types on one client (type-erasure) ---
    {
        TcpServer server;
        server.start("hello", ServerFraming::ContentLength);
        const std::string base = base_of(server.port);

        Client client(loop);
        client.get<int>(base + "/i", 7);
        client.get<std::string>(base + "/s", std::string("abc"));
        const bool done = pump([&] {
            return client.finished<int>().size() == 1
                && client.finished<std::string>().size() == 1;
        });
        check(done, "multi-type: both completed");
        const std::vector<Finished<int>> fi = client.finished<int>();
        const std::vector<Finished<std::string>> fs = client.finished<std::string>();
        check(!fi.empty() && fi[0].payload == 7, "multi-type: int payload preserved");
        check(
            !fs.empty() && fs[0].payload == "abc",
            "multi-type: string payload preserved");
    }

    // --- T6: POST with JSON body + Set-Cookie exposure (auth pattern) ---
    {
        TcpServer server;
        const std::string token_body = "{\"status\":\"SUCCESS\"}";
        server.start(token_body, ServerFraming::ContentLength,
                     "Set-Cookie: authorization: jwt-secret-123\r\n");
        const std::string base = base_of(server.port);

        Client client(loop);
        const std::string auth_req =
            "{\"appId\":\"my-app\",\"clientId\":\"cid\",\"secretKey\":\"sk\"}";
        client.post<int>(base + "/token", auth_req, 99);
        const bool done = pump([&] { return !client.finished<int>().empty(); });
        check(done, "POST: request completed");
        const std::vector<Finished<int>> fins = client.finished<int>();
        check(done && !fins.empty() && fins[0].status == 200, "POST: status is 200");
        check(done && !fins.empty() && fins[0].body == token_body,
              "POST: body matches");
        check(done && !fins.empty() && fins[0].payload == 99,
              "POST: payload preserved");
        check(done && !fins.empty() && server.received_body == auth_req,
              "POST: server received body");
        bool cookie = false;
        if (!fins.empty())
        {
            for (const auto& h : fins[0].headers)
                if (h.first == "Set-Cookie"
                    && h.second.find("jwt-secret-123") != std::string::npos)
                    cookie = true;
        }
        check(cookie, "POST: Set-Cookie header exposed");
    }

    // --- T7: POST to an endpoint answering 401 ---
    {
        TcpServer server;
        server.start("unauthorized", ServerFraming::ContentLength, {}, 401);
        const std::string base = base_of(server.port);

        Client client(loop);
        client.post<std::string>(base + "/token", std::string("bad-creds"),
                                 std::string("anon"));
        const bool done = pump([&] {
            return !client.finished<std::string>().empty();
        });
        const std::vector<Finished<std::string>> fins =
            client.finished<std::string>();
        check(done && !fins.empty() && fins[0].status == 401,
              "POST 401: status reported");
        check(done && !fins.empty() && fins[0].body == "unauthorized",
              "POST 401: body matches");
    }

    // --- T8: GET carrying the auth token (Authorization: Bearer) ---
    {
        TcpServer server;
        server.start("secret", ServerFraming::ContentLength, {}, 200,
                     "jwt-e2e-001");
        const std::string base = base_of(server.port);

        Client client(loop);

        // With token: the endpoint accepts.
        client.get<int>(base + "/secret", 1, "jwt-e2e-001");
        {
            const bool done = pump([&] { return !client.finished<int>().empty(); });
            const std::vector<Finished<int>> fins = client.finished<int>();
            check(done && !fins.empty() && fins[0].status == 200,
                  "GET token: accepted (200)");
            check(done && !fins.empty() && fins[0].body == "secret",
                  "GET token: body matches");
            check(server.received_head.find("Authorization: Bearer jwt-e2e-001")
                      != std::string::npos,
                  "GET token: server saw Bearer header");
        }

        // Without token: the same endpoint rejects with 401.
        client.get<int>(base + "/secret", 2);
        {
            const bool done = pump([&] {
                return client.finished<int>().size() == 2;
            });
            const std::vector<Finished<int>> fins = client.finished<int>();
            check(done && fins.size() == 2 && fins[1].status == 401,
                  "GET no token: rejected (401)");
        }
    }

    // --- T9: POST carrying the auth token ---
    {
        TcpServer server;
        server.start("action-ok", ServerFraming::ContentLength, {}, 200,
                     "jwt-e2e-001");
        const std::string base = base_of(server.port);

        Client client(loop);
        client.post<int>(base + "/action", "{\"id\":\"1\"}", 3, "jwt-e2e-001");
        const bool done = pump([&] { return !client.finished<int>().empty(); });
        const std::vector<Finished<int>> fins = client.finished<int>();
        check(done && !fins.empty() && fins[0].status == 200,
              "POST token: accepted (200)");
        check(done && !fins.empty() && fins[0].payload == 3,
              "POST token: payload preserved");
        check(server.received_head.find("Authorization: Bearer jwt-e2e-001")
                  != std::string::npos,
              "POST token: server saw Bearer header");
        check(server.received_body == "{\"id\":\"1\"}",
              "POST token: body intact");
    }

    // --- T10: end-to-end auth flow (POST /token -> JWT -> authenticated GET) ---
    {
        const std::string jwt = "eyJhbGciOiJIUzI1NiJ9.eyJzdWIiOiJhZG1pbiJ9.e2e";

        TcpServer token_server;
        token_server.start("{\"response\":{\"status\":\"Success\"}}",
                           ServerFraming::ContentLength,
                           "Set-Cookie: authorization: " + jwt + "\r\n");
        const std::string token_base = base_of(token_server.port);

        TcpServer api_server;
        api_server.start("mosip-data", ServerFraming::ContentLength, {}, 200, jwt);
        const std::string api_base = base_of(api_server.port);

        Client client(loop);

        // Step 1: obtain the token.
        client.post<std::string>(token_base + "/token",
                                 "{\"appId\":\"admin\",\"clientId\":\"cid\"}",
                                 std::string("auth"));
        const bool auth_done =
            pump([&] { return !client.finished<std::string>().empty(); });
        check(auth_done, "auth flow: token POST completed");

        // Step 2: caller extracts the JWT from Set-Cookie.
        std::string token;
        if (auth_done)
            token = extract_token(client.finished<std::string>()[0]);
        check(token == jwt, "auth flow: JWT extracted from Set-Cookie");

        // Step 3: authenticated GET.
        client.get<std::string>(api_base + "/data", std::string("q"), token);
        const bool get_done = pump([&] {
            return client.finished<std::string>().size() == 2;
        });
        const std::vector<Finished<std::string>> fins =
            client.finished<std::string>();
        check(get_done && fins.size() == 2 && fins[1].status == 200,
              "auth flow: authenticated GET accepted (200)");
        check(get_done && fins.size() == 2 && fins[1].body == "mosip-data",
              "auth flow: GET body matches");
        check(get_done && fins.size() == 2 && fins[1].payload == "q",
              "auth flow: GET payload preserved");
    }

    // --- T11: auth failure (error JSON, no Set-Cookie) -> unauthenticated GET 401 ---
    {
        const std::string error_body =
            "{\"response\":null,\"errors\":[{\"errorCode\":\"KER-ATH-401\","
            "\"message\":\"Authentication Failed : Invalid Token :Token verification "
            "failed\"}]}";

        TcpServer token_server;
        token_server.start(error_body, ServerFraming::ContentLength);
        const std::string token_base = base_of(token_server.port);

        TcpServer api_server;
        api_server.start("mosip-data", ServerFraming::ContentLength, {}, 200,
                         "some-jwt");
        const std::string api_base = base_of(api_server.port);

        Client client(loop);

        // Step 1: the auth endpoint answers 200 + error JSON (MOSIP pattern),
        // and no Set-Cookie.
        client.post<std::string>(token_base + "/token",
                                 "{\"appId\":\"adminXX\",\"clientId\":\"cid\"}",
                                 std::string("auth"));
        const bool auth_done =
            pump([&] { return !client.finished<std::string>().empty(); });
        check(auth_done, "auth failure: token POST completed");

        const std::vector<Finished<std::string>> afins =
            client.finished<std::string>();
        check(auth_done && !afins.empty()
            && afins[0].body.find("KER-ATH-401") != std::string::npos,
              "auth failure: error body exposed to caller");

        // Step 2: no cookie -> extraction yields empty token.
        std::string token =
            (auth_done && !afins.empty()) ? extract_token(afins[0]) : std::string{};
        check(token.empty(), "auth failure: no JWT in Set-Cookie");

        // Step 3: GET without a valid token is rejected with 401.
        client.get<std::string>(api_base + "/data", std::string("q"), token);
        const bool get_done = pump([&] {
            return client.finished<std::string>().size() == 2;
        });
        const std::vector<Finished<std::string>> gfins =
            client.finished<std::string>();
        check(get_done && gfins.size() == 2 && gfins[1].status == 401,
              "auth failure: GET without token rejected (401)");
    }

    // --- T5: read timeout fires on a silent connection ---
    {
        StallServer server;
        server.start(8000);
        const std::string base = base_of(server.port);

        Client client(loop);
        client.get<int>(base + "/stall", 1);
        const bool done = pump_wall(
            [&] { return !client.finished<int>().empty(); },
            12);
        const std::vector<Finished<int>> fins = client.finished<int>();
        check(
            done && !fins.empty() && fins[0].error == "timeout",
            "timeout: reported");
        check(
            done && !fins.empty() && fins[0].status == 0,
            "timeout: no status on timeout");
    }

    // Finalize pending uSockets handle closes before deleting the uv loop.
    uv_run(loop, UV_RUN_DEFAULT);
    uv_loop_delete(loop);

    std::cout << (g_failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED") << " ("
              << g_failures << " failure(s))\n";
    return (g_failures == 0) ? 0 : 1;
}
