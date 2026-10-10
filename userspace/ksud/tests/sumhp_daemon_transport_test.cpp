#include "core/json.hpp"
#include "core/log.hpp"
#include "terminal.hpp"

#include <algorithm>
#include <cassert>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <poll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

namespace fs = std::filesystem;
using namespace std::chrono_literals;

namespace ksud::terminal {
int error(std::string_view text, std::string_view) {
    std::cerr << text << '\n';
    return 1;
}
void diagnostics(std::string_view text) {
    std::cerr << text;
}
}  // namespace ksud::terminal

namespace sumhp {
fs::path socket_path;
int startup_attempts = 0;
fs::path runtime_socket_file() {
    return socket_path;
}
void append_log(const std::string&, logging::Level = logging::Level::Info) {}
int start_background(bool) {
    ++startup_attempts;
    return 1;
}

#include "sumhp_daemon_under_test.inc"
}  // namespace sumhp

namespace {
struct SocketPair {
    int reader = -1;
    int writer = -1;
    SocketPair() {
        int sockets[2];
        assert(socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0, sockets) == 0);
        reader = sockets[0];
        writer = sockets[1];
    }
    ~SocketPair() {
        if (reader >= 0)
            close(reader);
        if (writer >= 0)
            close(writer);
    }
    void close_writer() {
        close(writer);
        writer = -1;
    }
};

void send_bytes(int fd, const std::string& data) {
    assert(sumhp::write_all(fd, data, std::chrono::steady_clock::now() + 2s));
}

void test_request_encoding() {
    const std::vector<std::string> args = {"ignored", "hide", "/path with spaces/\"quotes\"\nline"};
    const auto encoded = sumhp::join_request(args, 1);
    assert(encoded.back() == '\n');
    assert(sumhp::split_request(encoded) == std::vector<std::string>(args.begin() + 1, args.end()));
    for (const auto* malformed : {"[]", "{}", "[12]", "[\"hide\",null]", "[\"hide\\u0000extra\"]"})
        assert(sumhp::split_request(malformed).empty());
}

void test_message_framing() {
    {
        SocketPair sockets;
        std::thread peer([&] {
            send_bytes(sockets.writer, "[\"daemon\"");
            std::this_thread::sleep_for(10ms);
            send_bytes(sockets.writer, ",\"ping\"]\n");
            assert(shutdown(sockets.writer, SHUT_WR) == 0);
        });
        std::string response;
        assert(sumhp::read_message(sockets.reader, 64, std::chrono::steady_clock::now() + 2s,
                                   response));
        peer.join();
        assert(response == "[\"daemon\",\"ping\"]\n");
    }
    for (const auto& input :
         {std::string{}, std::string("unterminated"), std::string("one\ntwo\n")}) {
        SocketPair sockets;
        send_bytes(sockets.writer, input);
        sockets.close_writer();
        std::string response = "stale response";
        assert(!sumhp::read_message(sockets.reader, 64, std::chrono::steady_clock::now() + 2s,
                                    response));
        assert(errno == EPROTO && response.empty());
    }
    for (bool newline : {false, true}) {
        SocketPair sockets;
        std::string input(32, 'x');
        if (newline)
            input.back() = '\n';
        send_bytes(sockets.writer, input);
        std::string response;
        const bool received = sumhp::read_message(sockets.reader, input.size(),
                                                  std::chrono::steady_clock::now() + 2s, response);
        assert(received == newline);
        if (newline)
            assert(response == input);
        else
            assert(errno == EMSGSIZE && response.empty());
    }
}

volatile sig_atomic_t interrupted = 0;
void signal_handler(int) {
    ++interrupted;
}

void test_read_deadline() {
    SocketPair sockets;
    struct sigaction action{};
    action.sa_handler = signal_handler;
    sigemptyset(&action.sa_mask);
    struct sigaction old_action{};
    assert(sigaction(SIGALRM, &action, &old_action) == 0);
    struct itimerval timer{};
    timer.it_value.tv_usec = 5000;
    timer.it_interval.tv_usec = 5000;
    assert(setitimer(ITIMER_REAL, &timer, nullptr) == 0);
    std::string response;
    const auto start = std::chrono::steady_clock::now();
    const bool received = sumhp::read_message(sockets.reader, 64, start + 100ms, response);
    const int read_error = errno;
    timer = {};
    assert(setitimer(ITIMER_REAL, &timer, nullptr) == 0);
    assert(sigaction(SIGALRM, &old_action, nullptr) == 0);
    assert(!received && read_error == ETIMEDOUT && interrupted > 0);
    assert(std::chrono::steady_clock::now() - start < 2s);

    // Progress must not reset the total request deadline.
    std::thread peer([&] {
        for (int i = 0; i < 20; ++i) {
            send_bytes(sockets.writer, "x");
            std::this_thread::sleep_for(10ms);
        }
    });
    const auto slow_start = std::chrono::steady_clock::now();
    const bool slow_received = sumhp::read_message(sockets.reader, 64, slow_start + 70ms, response);
    const int slow_error = errno;
    const auto elapsed = std::chrono::steady_clock::now() - slow_start;
    peer.join();
    assert(!slow_received && slow_error == ETIMEDOUT && response.empty());
    assert(elapsed < 1s);
}

void test_write_failures() {
    {
        SocketPair sockets;
        sockets.close_writer();
        struct sigaction action{};
        action.sa_handler = signal_handler;
        sigemptyset(&action.sa_mask);
        struct sigaction old_action{};
        assert(sigaction(SIGPIPE, &action, &old_action) == 0);
        interrupted = 0;
        const bool written =
            sumhp::write_all(sockets.reader, "reply\n", std::chrono::steady_clock::now() + 1s);
        const int write_error = errno;
        assert(sigaction(SIGPIPE, &old_action, nullptr) == 0);
        assert(!written && write_error == EPIPE && interrupted == 0);
    }
    {
        SocketPair sockets;
        const std::string chunk(4096, 'x');
        while (send(sockets.writer, chunk.data(), chunk.size(), MSG_NOSIGNAL) > 0) {
        }
        assert(errno == EAGAIN || errno == EWOULDBLOCK);
        const auto start = std::chrono::steady_clock::now();
        assert(!sumhp::write_all(sockets.writer, "reply\n", start + 100ms));
        assert(errno == ETIMEDOUT);
        assert(std::chrono::steady_clock::now() - start < 2s);
    }
}

int listen_at(const fs::path& path) {
    fs::remove(path);
    const int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    assert(fd >= 0);
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    const auto name = path.string();
    assert(name.size() < sizeof(address.sun_path));
    std::memcpy(address.sun_path, name.c_str(), name.size() + 1);
    assert(bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);
    assert(listen(fd, 4) == 0);
    return fd;
}

void test_retry_decisions(const fs::path& directory) {
    sumhp::socket_path = directory / "missing.sock";
    sumhp::startup_attempts = 0;
    assert(sumhp::run_via_daemon({"hide", "add", "/somewhere"}, false) == 1);
    assert(sumhp::startup_attempts == 1);

    // A stale, bound endpoint with no listener is also safe to auto-start.
    close(listen_at(sumhp::socket_path));
    sumhp::startup_attempts = 0;
    assert(sumhp::run_via_daemon({"hide", "add", "/somewhere"}, false) == 1);
    assert(sumhp::startup_attempts == 1);
    fs::remove(sumhp::socket_path);

    sumhp::startup_attempts = 0;
    assert(sumhp::run_via_daemon({std::string(sumhp::request_limit, 'x')}, false) == 1);
    assert(sumhp::startup_attempts == 0);
    sumhp::socket_path = directory / std::string(sizeof(sockaddr_un::sun_path), 'x');
    assert(sumhp::run_via_daemon({"status"}, false) == 1);
    assert(sumhp::startup_attempts == 0);

    // The fake daemon consumes the entire mutation then loses its response.
    // Retrying here would apply the mutation a second time.
    for (const auto& reply :
         {std::string{}, std::string("{\"exit_code\":0}"), std::string("not json\n"),
          sumhp::response_json(true, 0, 0, "saved\n", "")}) {
        sumhp::socket_path = directory / "daemon.sock";
        const int listener = listen_at(sumhp::socket_path);
        std::string received;
        std::thread peer([&] {
            const int client = accept4(listener, nullptr, nullptr, SOCK_CLOEXEC | SOCK_NONBLOCK);
            assert(client >= 0);
            assert(sumhp::read_message(client, sumhp::request_limit,
                                       std::chrono::steady_clock::now() + 2s, received));
            send_bytes(client, reply);
            close(client);
        });
        sumhp::startup_attempts = 0;
        const int code = sumhp::run_via_daemon({"hide", "add", "/somewhere"}, false);
        peer.join();
        assert(sumhp::split_request(received) ==
               std::vector<std::string>({"hide", "add", "/somewhere"}));
        assert(sumhp::startup_attempts == 0);
        assert(code == (reply.find("saved") != std::string::npos ? 0 : 1));
        close(listener);
        fs::remove(sumhp::socket_path);
    }
}
}  // namespace

int main() {
    char temporary[] = "/tmp/sumhp-daemon-test-XXXXXX";
    assert(mkdtemp(temporary) != nullptr);
    const fs::path directory(temporary);
    // Expected transport failures must not pollute the test runner output.
    std::ostringstream output;
    std::ostringstream errors;
    auto* old_out = std::cout.rdbuf(output.rdbuf());
    auto* old_err = std::cerr.rdbuf(errors.rdbuf());
    test_request_encoding();
    test_message_framing();
    test_read_deadline();
    test_write_failures();
    test_retry_decisions(directory);
    std::cout.rdbuf(old_out);
    std::cerr.rdbuf(old_err);
    fs::remove_all(directory);
    puts("SUMHP daemon transport regressions passed");
}
