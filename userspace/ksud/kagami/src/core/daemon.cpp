#include "core/daemon.hpp"
#include "terminal.hpp"
#include "utils.hpp"

#include "core/command.hpp"
#include "core/json.hpp"
#include "core/log.hpp"
#include "core/runtime.hpp"
#include "kagami/config.hpp"
#include "kagami/kasumi_client.hpp"
#include "mount/kasumi.hpp"

#include <cerrno>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <fcntl.h>
#include <poll.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

namespace kagami {

namespace fs = std::filesystem;

namespace {
int print_status_json();

void append_log(const std::string& message, logging::Level level = logging::Level::Info) {
    logging::write(level, "daemon", message);
}

std::string join_request(const std::vector<std::string>& args, std::size_t start) {
    std::ostringstream out;
    out << '[';
    for (std::size_t i = start; i < args.size(); ++i) {
        if (i > start) {
            out << ',';
        }
        out << json_quote(args[i]);
    }
    out << "]\n";
    return out.str();
}

std::vector<std::string> split_request(const std::string& request) {
    std::vector<std::string> args;
    JsonValue root;
    std::string error;
    if (!parse_json(request, root, error) || !root.is_array()) {
        return args;
    }
    args.reserve(root.a.size());
    for (const auto& item : root.a) {
        if (!item.is_string() || item.s.find('\0') != std::string::npos) {
            args.clear();
            return args;
        }
        args.push_back(item.s);
    }
    return args;
}

std::string response_json(bool ok, int exit_code, int error_number, const std::string& out,
                          const std::string& err) {
    std::ostringstream json;
    json << "{"
         << "\"ok\":" << (ok ? "true" : "false") << ","
         << "\"exit_code\":" << exit_code << ","
         << "\"errno\":" << error_number << ","
         << "\"stdout\":" << json_quote(out) << ","
         << "\"stderr\":" << json_quote(err) << "}\n";
    return json.str();
}

std::string status_json(bool running) {
    const auto pid_text = [&]() -> std::string {
        std::ifstream in(runtime_pid_file());
        std::string line;
        return std::getline(in, line) ? line : "";
    }();

    std::ostringstream out;
    out << "{"
        << "\"running\":" << (running ? "true" : "false") << ","
        << "\"data_dir\":" << json_quote(runtime_data_dir().string()) << ","
        << "\"config_file\":" << json_quote(runtime_config_file().string()) << ","
        << "\"socket\":" << json_quote(runtime_socket_file().string()) << ","
        << "\"pid_file\":" << json_quote(runtime_pid_file().string()) << ","
        << "\"pid\":" << json_quote(pid_text) << ","
        << "\"log_file\":" << json_quote(runtime_log_file().string()) << "}\n";
    return out.str();
}

bool write_all(int fd, const std::string& data) {
    const char* ptr = data.data();
    std::size_t left = data.size();
    while (left > 0) {
        const ssize_t written = send(fd, ptr, left, MSG_NOSIGNAL);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        if (written == 0) {
            errno = EIO;
            return false;
        }
        ptr += written;
        left -= static_cast<std::size_t>(written);
    }
    return true;
}

std::string read_all(int fd, std::size_t limit) {
    std::string data;
    char buffer[1024] = {};
    for (;;) {
        const ssize_t n = read(fd, buffer, sizeof(buffer));
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        if (n == 0) {
            break;
        }
        data.append(buffer, static_cast<std::size_t>(n));
        if (data.size() > limit) {
            data.clear();
            break;
        }
        if (data.find('\n') != std::string::npos) {
            break;
        }
    }
    return data;
}

int open_client_socket(std::string& error) {
    const auto path = runtime_socket_file().string();
    if (path.size() >= sizeof(sockaddr_un::sun_path)) {
        error = "socket path is too long: " + path;
        return -1;
    }

    const int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        error = std::strerror(errno);
        return -1;
    }

    sockaddr_un addr = {};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
    if (connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        error = std::strerror(errno);
        close(fd);
        return -1;
    }
    return fd;
}

int send_request(const std::vector<std::string>& args, std::size_t start, std::string& response,
                 std::string& error) {
    const int fd = open_client_socket(error);
    if (fd < 0) {
        return 1;
    }
    if (!write_all(fd, join_request(args, start))) {
        error = std::strerror(errno);
        close(fd);
        return 1;
    }
    shutdown(fd, SHUT_WR);
    response = read_all(fd, 1024UL * 1024);
    close(fd);
    return 0;
}

bool daemon_running() {
    const std::vector<std::string> ping = {"daemon", "ping"};
    std::string response;
    std::string error;
    return send_request(ping, 0, response, error) == 0 &&
           response.find("\"ok\":true") != std::string::npos;
}

int serve_foreground(int ready_fd = -1) {
    umask(0077);
    std::string preparation_error;
    if (!prepare_runtime(preparation_error) || !logging::prepare_boot_log(preparation_error)) {
        logging::write(logging::Level::Error, "daemon", preparation_error);
        std::cerr << preparation_error << '\n';
        return 1;
    }
    Config log_config;
    std::string config_error;
    if (read_config_file(log_config, config_error))
        logging::set_debug_enabled(log_config.debug || log_config.verbose);
    std::error_code ec;

    const auto socket_path = runtime_socket_file().string();
    if (socket_path.size() >= sizeof(sockaddr_un::sun_path)) {
        std::cerr << "socket path is too long: " << socket_path << "\n";
        return 1;
    }

    const auto lock_path = runtime_daemon_lock_file();
    const int lock_fd = open(lock_path.c_str(), O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (lock_fd < 0) {
        std::cerr << "open " << lock_path << ": " << std::strerror(errno) << "\n";
        return 1;
    }
    if (flock(lock_fd, LOCK_EX | LOCK_NB) != 0) {
        std::cerr << "kagamid is already running\n";
        close(lock_fd);
        return 1;
    }

    // The lifetime lock proves there is no live owner before replacing a stale
    // socket. It also serializes concurrent first-use auto-start attempts.
    fs::remove(runtime_socket_file(), ec);
    const int server_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (server_fd < 0) {
        std::cerr << "socket: " << std::strerror(errno) << "\n";
        close(lock_fd);
        return 1;
    }

    sockaddr_un addr = {};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, socket_path.c_str(), sizeof(addr.sun_path) - 1);
    if (bind(server_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        std::cerr << "bind " << socket_path << ": " << std::strerror(errno) << "\n";
        close(server_fd);
        close(lock_fd);
        return 1;
    }
    chmod(socket_path.c_str(), 0600);

    if (listen(server_fd, 8) != 0) {
        std::cerr << "listen: " << std::strerror(errno) << "\n";
        close(server_fd);
        close(lock_fd);
        return 1;
    }

    {
        std::ofstream pid(runtime_pid_file(), std::ios::trunc);
        if (pid) {
            pid << getpid() << "\n";
        }
    }
    append_log("ready pid=" + std::to_string(getpid()) + " socket=" + socket_path);
    if (ready_fd >= 0) {
        const char ready = 1;
        (void)send(ready_fd, &ready, 1, MSG_NOSIGNAL);
        close(ready_fd);
    }

    int mounts_fd = open("/proc/1/mountinfo", O_RDONLY | O_CLOEXEC);
    if (mounts_fd < 0)
        append_log("cannot watch storage mounts: " + std::string(std::strerror(errno)),
                   logging::Level::Error);
    if (config_error.empty()) {
        std::string restore_error;
        if (!mount::kasumi::restore_persisted_hide_rules(restore_error))
            append_log(restore_error, logging::Level::Error);
    }
    bool stopping = false;
    int exit_code = 0;
    while (!stopping) {
        const bool pending_hides = mount::kasumi::has_pending_hide_rules();
        pollfd descriptors[] = {{server_fd, POLLIN, 0},
                                {pending_hides ? mounts_fd : -1, POLLPRI, 0}};
        const int ready = poll(descriptors, 2, -1);
        if (ready < 0) {
            if (errno == EINTR)
                continue;
            append_log(std::string("poll failed: ") + std::strerror(errno), logging::Level::Error);
            exit_code = 1;
            break;
        }
        if ((descriptors[0].revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
            exit_code = 1;
            break;
        }
        if ((descriptors[1].revents & (POLLHUP | POLLNVAL)) != 0 ||
            ((descriptors[1].revents & POLLERR) != 0 && (descriptors[1].revents & POLLPRI) == 0)) {
            close(mounts_fd);
            mounts_fd = -1;
            append_log("storage mount watch lost", logging::Level::Error);
        }
        if ((descriptors[1].revents & POLLPRI) != 0)
            mount::kasumi::retry_pending_hide_rules();
        if ((descriptors[0].revents & POLLIN) == 0)
            continue;
        const int client_fd = accept4(server_fd, nullptr, nullptr, SOCK_CLOEXEC);
        if (client_fd < 0) {
            if (errno == EINTR) {
                continue;
            }
            append_log(std::string("accept failed: ") + std::strerror(errno),
                       logging::Level::Error);
            continue;
        }

        timeval timeout = {};
        timeout.tv_sec = 5;
        (void)setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

        const auto request = read_all(client_fd, 64UL * 1024);
        auto request_args = split_request(request);
        if (request_args.empty()) {
            append_log("rejected malformed or empty request", logging::Level::Warning);
            write_all(client_fd, response_json(false, 1, EINVAL, "", "empty daemon request"));
            close(client_fd);
            continue;
        }

        if (request_args[0] == "daemon" && request_args.size() >= 2 && request_args[1] == "stop") {
            write_all(client_fd, response_json(true, 0, 0, "stopping\n", ""));
            stopping = true;
            close(client_fd);
            continue;
        }

        if (request_args[0] == "daemon" && request_args.size() >= 2 && request_args[1] == "ping") {
            write_all(client_fd, response_json(true, 0, 0, "pong\n", ""));
            close(client_fd);
            continue;
        }

        if (request_args[0] == "daemon" && request_args.size() >= 2 &&
            request_args[1] == "status") {
            write_all(client_fd, response_json(true, 0, 0, status_json(true), ""));
            close(client_fd);
            continue;
        }

        if (request_args[0] == "daemon") {
            write_all(client_fd, response_json(false, 1, EINVAL, "",
                                               "daemon control commands cannot be forwarded\n"));
            close(client_fd);
            continue;
        }

        const auto result = run_command_capture(request_args);
        write_all(client_fd,
                  response_json(result.exit_code == 0, result.exit_code, result.error_number,
                                result.stdout_text, result.stderr_text));
        close(client_fd);
    }

    append_log("kagamid stopped");
    if (mounts_fd >= 0)
        close(mounts_fd);
    close(server_fd);
    fs::remove(runtime_socket_file(), ec);
    fs::remove(runtime_pid_file(), ec);
    close(lock_fd);
    return exit_code;
}

int start_background(bool report_status) {
    if (daemon_running()) {
        return report_status ? print_status_json() : 0;
    }

    std::string preparation_error;
    if (!prepare_runtime(preparation_error) || !logging::prepare_boot_log(preparation_error)) {
        std::cerr << preparation_error << '\n';
        return 1;
    }
    int ready_pipe[2];
    if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, ready_pipe) != 0)
        return 1;
    const auto detached = ksud::daemonize_process(false);
    if (detached == ksud::ProcessDaemonizeResult::Error) {
        close(ready_pipe[0]);
        close(ready_pipe[1]);
        std::cerr << "fork: " << std::strerror(errno) << "\n";
        return 1;
    }
    if (detached == ksud::ProcessDaemonizeResult::Daemon) {
        close(ready_pipe[0]);
        const int null_fd = open("/dev/null", O_RDONLY);
        if (null_fd >= 0) {
            dup2(null_fd, STDIN_FILENO);
            close(null_fd);
        }
        const int log_fd = open(runtime_log_file().c_str(),
                                O_CREAT | O_WRONLY | O_APPEND | O_CLOEXEC | O_NOFOLLOW, 0600);
        if (log_fd >= 0) {
            dup2(log_fd, STDOUT_FILENO);
            dup2(log_fd, STDERR_FILENO);
            close(log_fd);
        }
        _exit(serve_foreground(ready_pipe[1]));
    }
    close(ready_pipe[1]);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    for (;;) {
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
                              deadline - std::chrono::steady_clock::now())
                              .count();
        if (left <= 0)
            break;
        pollfd descriptor{ready_pipe[0], POLLIN | POLLHUP, 0};
        const int ret = poll(&descriptor, 1, static_cast<int>(left));
        if (ret < 0 && errno == EINTR)
            continue;
        char ready = 0;
        const bool ok = ret > 0 && read(ready_pipe[0], &ready, 1) == 1 && ready == 1;
        close(ready_pipe[0]);
        if (!ok)
            return 1;
        return report_status ? print_status_json() : 0;
    }
    close(ready_pipe[0]);
    std::cerr << "kagamid did not become ready\n";
    return 1;
}
}  // namespace

int run_via_daemon(const std::vector<std::string>& args, bool display_errors) {
    if (args.empty()) {
        return 1;
    }

    std::string response;
    std::string error;
    if (send_request(args, 0, response, error) != 0) {
        if (start_background(false) != 0 || send_request(args, 0, response, error) != 0) {
            if (display_errors)
                return ksud::terminal::error(
                    "mount daemon unavailable: " + error,
                    "Inspect 'ksud kagami daemon status' and retry with --verbose.");
            std::cerr << "daemon unavailable: " << error << "\n";
            return 1;
        }
    }

    JsonValue envelope;
    if (!parse_json(response, envelope, error) || !envelope.is_object()) {
        if (display_errors)
            return ksud::terminal::error("invalid mount daemon response: " + error);
        std::cerr << "invalid daemon response: " << error << "\n";
        return 1;
    }
    const JsonValue* code = envelope.find("exit_code");
    const JsonValue* out = envelope.find("stdout");
    const JsonValue* err = envelope.find("stderr");
    if (!code || !code->is_number() || !out || !out->is_string() || !err || !err->is_string()) {
        if (display_errors)
            return ksud::terminal::error("invalid mount daemon response fields");
        std::cerr << "invalid daemon response fields\n";
        return 1;
    }
    std::cout << out->s;
    if (display_errors && code->n != 0 && !err->s.empty())
        ksud::terminal::diagnostics(err->s);
    else
        std::cerr << err->s;
    return static_cast<int>(code->n);
}

namespace {
int print_status_json() {
    const bool running = daemon_running();
    std::cout << status_json(running);
    return 0;
}
}  // namespace

int run_daemon_command(const std::vector<std::string>& args) {
    const std::string sub = args.size() > 1 ? args[1] : "";
    if (sub == "status") {
        return print_status_json();
    }

    if (sub == "serve") {
        return serve_foreground();
    }
    if (sub == "start") {
        return start_background(true);
    }
    if (sub == "call") {
        if (args.size() < 3) {
            std::cerr << "daemon call requires a command\n";
            return 1;
        }
        std::string response;
        std::string error;
        const int rc = send_request(args, 2, response, error);
        if (rc != 0) {
            std::cerr << "daemon unavailable: " << error << "\n";
            return rc;
        }
        std::cout << response;
        return 0;
    }
    if (sub == "ping" || sub == "stop") {
        std::string response;
        std::string error;
        const int rc = send_request(args, 0, response, error);
        if (rc != 0) {
            std::cerr << "daemon unavailable: " << error << "\n";
            return rc;
        }
        std::cout << response;
        return 0;
    }

    std::cerr << "usage: ksud kagami daemon status|start|serve|call|ping|stop\n";
    return 1;
}

}  // namespace kagami
