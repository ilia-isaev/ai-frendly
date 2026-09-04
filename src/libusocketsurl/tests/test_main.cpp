#include <cctype>
#include <chrono>
#include <compare>
#include <concepts>
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

import usurl;

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

std::string build_response(const std::string& body, ServerFraming framing)
{
    switch (framing)
    {
    case ServerFraming::ContentLength:
        return std::string("HTTP/1.1 200 OK\r\n")
             + "Content-Length: " + std::to_string(body.size()) + "\r\n"
             + "Connection: close\r\n" + "\r\n" + body;
    case ServerFraming::Chunked:
        return std::string("HTTP/1.1 200 OK\r\n")
             + "Transfer-Encoding: chunked\r\n"
             + "Connection: close\r\n" + "\r\n" + chunked_encode(body);
    case ServerFraming::CloseDelimited:
        return std::string("HTTP/1.1 200 OK\r\n")
             + "Connection: close\r\n" + "\r\n" + body;
    }
    return {};
}

struct TcpServer
{
    int listen_fd = -1;
    int port = 0;
    std::string body;
    ServerFraming framing = ServerFraming::ContentLength;
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

    void start(const std::string& body_, ServerFraming framing_)
    {
        body = body_;
        framing = framing_;
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
            for (;;)
            {
                const ssize_t n = ::read(conn, buf, sizeof buf);
                if (n <= 0)
                    break;
                req.append(buf, static_cast<std::size_t>(n));
                if (req.find("\r\n\r\n") != std::string::npos)
                    break;
            }
            const std::string resp = build_response(body, framing);
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
