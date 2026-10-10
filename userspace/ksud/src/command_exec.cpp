#include "pipe.hpp"
#include "utils.hpp"

#include <fcntl.h>
#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>
#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <climits>
#include <csignal>
#include <cstring>

namespace ksud {
namespace {
[[noreturn]] void exec_failed(int fd, int error) {
    const auto* data = reinterpret_cast<const char*>(&error);
    size_t sent = 0;
    while (sent < sizeof(error)) {
        const ssize_t count = write(fd, data + sent, sizeof(error) - sent);
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0)
            break;
        sent += static_cast<size_t>(count);
    }
    _exit(127);
}
ExecResult exec_command_impl(const std::vector<std::string>& args, const std::string& workdir,
                             std::optional<std::chrono::milliseconds> timeout) {
    ExecResult result{-1, "", ""};
    if (args.empty() || args[0].empty() || (timeout && timeout->count() <= 0)) {
        result.error_number = EINVAL;
        return result;
    }
    for (const auto& arg : args) {
        if (arg.find('\0') != std::string::npos) {
            result.error_number = EINVAL;
            return result;
        }
    }
    if (workdir.find('\0') != std::string::npos) {
        result.error_number = EINVAL;
        return result;
    }
    const auto deadline =
        std::chrono::steady_clock::now() + timeout.value_or(std::chrono::milliseconds(0));
    const auto remaining_ms = [&] {
        return std::chrono::ceil<std::chrono::milliseconds>(deadline -
                                                            std::chrono::steady_clock::now());
    };
    std::vector<char*> argv;
    argv.reserve(args.size() + 1);
    for (const auto& arg : args)
        argv.push_back(const_cast<char*>(arg.c_str()));
    argv.push_back(nullptr);

    std::array<CapturePipe, 3> pipes;
    for (auto& pipe : pipes) {
        if (!pipe.open_pipe()) {
            result.error_number = errno;
            return result;
        }
    }
    const pid_t pid = fork();
    if (pid < 0) {
        result.error_number = errno;
        return result;
    }
    if (pid == 0) {
        for (auto& pipe : pipes)
            pipe.close_end(0);
        // A timed command can launch helpers that inherit its output pipes.
        // Own their process group so cancellation also stops those helpers.
        if (timeout && setpgid(0, 0) != 0)
            exec_failed(pipes[2].fd(1), errno);
        if (dup2(pipes[0].fd(1), STDOUT_FILENO) < 0 || dup2(pipes[1].fd(1), STDERR_FILENO) < 0)
            exec_failed(pipes[2].fd(1), errno);
        pipes[0].close_end(1);
        pipes[1].close_end(1);
        if (!workdir.empty() && chdir(workdir.c_str()) != 0)
            exec_failed(pipes[2].fd(1), errno);
        execvp(argv[0], argv.data());
        exec_failed(pipes[2].fd(1), errno);
    }

    std::array<pollfd, 3> descriptors{};
    size_t remaining = pipes.size();
    for (size_t i = 0; i < pipes.size(); ++i) {
        pipes[i].close_end(1);
        const int fd = pipes[i].fd(0);
        const int flags = fcntl(fd, F_GETFL);
        if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
            result.error_number = errno;
            pipes[i].close_end(0);
            --remaining;
        }
        descriptors[i] = {pipes[i].fd(0), POLLIN, 0};
    }
    std::array<char, 8192> buffer{};
    std::array<char, sizeof(int)> exec_error{};
    size_t error_bytes = 0;
    while (remaining) {
        const auto left = remaining_ms();
        if (timeout && left.count() <= 0) {
            result.error_number = ETIMEDOUT;
            break;
        }
        const int poll_timeout =
            timeout ? static_cast<int>(std::min<int64_t>(left.count(), INT_MAX)) : -1;
        const int ready = poll(descriptors.data(), descriptors.size(), poll_timeout);
        if (ready == 0) {
            result.error_number = ETIMEDOUT;
            break;
        }
        if (ready < 0) {
            if (errno == EINTR)
                continue;
            result.error_number = errno;
            break;
        }
        for (size_t i = 0; i < pipes.size(); ++i) {
            if (!descriptors[i].revents || descriptors[i].fd < 0)
                continue;
            for (unsigned batch = 0; batch < 16; ++batch) {
                const ssize_t count = read(descriptors[i].fd, buffer.data(), buffer.size());
                if (count < 0 && errno == EINTR)
                    continue;
                if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
                    break;
                if (count <= 0) {
                    if (count < 0 && !result.error_number)
                        result.error_number = errno;
                    pipes[i].close_end(0);
                    descriptors[i].fd = -1;
                    --remaining;
                    break;
                }
                const auto size = static_cast<size_t>(count);
                constexpr size_t capture_limit = 1024UL * 1024;
                if (timeout && i < 2 &&
                    (i == 0 ? result.stdout_str.size() : result.stderr_str.size()) + size >
                        capture_limit) {
                    result.error_number = EOVERFLOW;
                    break;
                }
                if (i == 0)
                    result.stdout_str.append(buffer.data(), size);
                else if (i == 1)
                    result.stderr_str.append(buffer.data(), size);
                else if (size <= exec_error.size() - error_bytes) {
                    memcpy(exec_error.data() + error_bytes, buffer.data(), size);
                    error_bytes += size;
                } else
                    result.error_number = EIO;
            }
            if (timeout && result.error_number)
                break;
        }
        if (timeout && result.error_number)
            break;
    }
    for (auto& pipe : pipes)
        pipe.close_end(0);
    int status = 0;
    pid_t waited;
    bool terminate = timeout && result.error_number != 0;
    for (;;) {
        if (terminate) {
            (void)kill(-pid, SIGKILL);
            (void)kill(pid, SIGKILL);
        }
        waited = waitpid(pid, &status, timeout && !terminate ? WNOHANG : 0);
        if (waited < 0 && errno == EINTR)
            continue;
        if (waited != 0)
            break;
        const auto left = remaining_ms();
        if (left.count() <= 0) {
            result.error_number = ETIMEDOUT;
            terminate = true;
            continue;
        }
        (void)poll(nullptr, 0, static_cast<int>(std::min<int64_t>(left.count(), 10)));
    }
    if (waited < 0)
        result.error_number = errno;
    else if (WIFEXITED(status))
        result.exit_code = WEXITSTATUS(status);
    else if (WIFSIGNALED(status))
        result.exit_code = 128 + WTERMSIG(status);
    if (error_bytes == sizeof(int))
        memcpy(&result.error_number, exec_error.data(), sizeof(int));
    else if (error_bytes)
        result.error_number = EIO;
    if (result.error_number && result.stderr_str.empty())
        result.stderr_str = args[0] + ": " + std::strerror(result.error_number) + "\n";
    if (result.error_number && result.exit_code == 0)
        result.exit_code = -1;
    return result;
}
}  // namespace

ExecResult exec_command(const std::vector<std::string>& args) {
    return exec_command_impl(args, "", std::nullopt);
}

ExecResult exec_command(const std::vector<std::string>& args, const std::string& workdir) {
    return exec_command_impl(args, workdir, std::nullopt);
}

ExecResult exec_command(const std::vector<std::string>& args, std::chrono::milliseconds timeout) {
    return exec_command_impl(args, "", timeout);
}
}  // namespace ksud
