#include "subprocess/subprocess_executor.hpp"

#include <fcntl.h>
#include <signal.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <expected>

namespace rosweb::subprocess {

// --- One-shot execution ---

auto SubprocessExecutor::execute(const std::vector<std::string>& args,
                                  int timeout_ms,
                                  const std::string& cwd)
    -> std::expected<CommandResult, errors::ErrorCode> {
    if (args.empty()) {
        return std::unexpected(errors::ErrorCode::INTERNAL_ERROR);
    }

    int stdout_pipe[2] = {-1, -1};
    int stderr_pipe[2] = {-1, -1};

    if (::pipe(stdout_pipe) < 0) {
        return std::unexpected(errors::ErrorCode::INTERNAL_ERROR);
    }
    if (::pipe(stderr_pipe) < 0) {
        ::close(stdout_pipe[0]);
        ::close(stdout_pipe[1]);
        return std::unexpected(errors::ErrorCode::INTERNAL_ERROR);
    }

    pid_t pid = ::fork();
    if (pid < 0) {
        ::close(stdout_pipe[0]); ::close(stdout_pipe[1]);
        ::close(stderr_pipe[0]); ::close(stderr_pipe[1]);
        return std::unexpected(errors::ErrorCode::INTERNAL_ERROR);
    }

    if (pid == 0) {
        // Child
        ::close(stdout_pipe[0]);
        ::close(stderr_pipe[0]);
        ::dup2(stdout_pipe[1], STDOUT_FILENO);
        ::dup2(stderr_pipe[1], STDERR_FILENO);
        if (stdout_pipe[1] > STDERR_FILENO) ::close(stdout_pipe[1]);
        if (stderr_pipe[1] > STDERR_FILENO) ::close(stderr_pipe[1]);

        if (!cwd.empty()) {
            ::chdir(cwd.c_str());
        }

        auto argv = build_argv(args);
        ::setpgid(0, 0);
        ::execvp(argv[0], argv.data());
        _exit(127);
    }

    // Parent — close write ends
    ::close(stdout_pipe[1]);
    ::close(stderr_pipe[1]);

    // Set non-blocking on read ends for safe polling
    auto set_nonblock = [](int fd) {
        int flags = ::fcntl(fd, F_GETFL, 0);
        if (flags >= 0) ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    };
    set_nonblock(stdout_pipe[0]);
    set_nonblock(stderr_pipe[0]);

    CommandResult result;
    bool stdout_eof = false;
    bool stderr_eof = false;
    std::array<char, 4096> buf{};

    auto start = std::chrono::steady_clock::now();

    while (!stdout_eof || !stderr_eof) {
        // Check timeout
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();
        if (timeout_ms > 0 && elapsed >= timeout_ms) {
            ::killpg(pid, SIGKILL);
            int status = 0;
            ::waitpid(pid, &status, 0);
            ::close(stdout_pipe[0]);
            ::close(stderr_pipe[0]);
            return std::unexpected(errors::ErrorCode::ROS_SERVICE_TIMEOUT);
        }

        fd_set read_fds;
        FD_ZERO(&read_fds);
        int max_fd = 0;
        if (!stdout_eof) { FD_SET(stdout_pipe[0], &read_fds); max_fd = std::max(max_fd, stdout_pipe[0] + 1); }
        if (!stderr_eof) { FD_SET(stderr_pipe[0], &read_fds); max_fd = std::max(max_fd, stderr_pipe[0] + 1); }

        if (max_fd == 0) break;  // both EOF

        // Remaining time for this select
        int remain_ms = 100;  // default poll interval
        if (timeout_ms > 0) {
            remain_ms = std::min(100, static_cast<int>(timeout_ms - elapsed));
            if (remain_ms <= 0) continue;  // will be caught at top of loop
        }

        struct timeval tv{};
        tv.tv_sec = remain_ms / 1000;
        tv.tv_usec = (remain_ms % 1000) * 1000;

        int ret = ::select(max_fd, &read_fds, nullptr, nullptr, &tv);
        if (ret < 0) {
            if (errno == EBADF) break;
            continue;
        }

        if (!stdout_eof && FD_ISSET(stdout_pipe[0], &read_fds)) {
            ssize_t n = ::read(stdout_pipe[0], buf.data(), buf.size());
            if (n > 0) {
                result.stdout_output.append(buf.data(), static_cast<size_t>(n));
            } else {
                stdout_eof = true;
            }
        }
        if (!stderr_eof && FD_ISSET(stderr_pipe[0], &read_fds)) {
            ssize_t n = ::read(stderr_pipe[0], buf.data(), buf.size());
            if (n > 0) {
                result.stderr_output.append(buf.data(), static_cast<size_t>(n));
            } else {
                stderr_eof = true;
            }
        }
    }

    ::close(stdout_pipe[0]);
    ::close(stderr_pipe[0]);

    // Wait for child and get exit code
    int status = 0;
    ::waitpid(pid, &status, 0);
    result.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : 1;
    return result;
}

// --- Streaming execution ---

auto SubprocessExecutor::start_streaming(const std::vector<std::string>& args,
                                          StreamCallbacks callbacks,
                                          const std::string& cwd)
    -> std::expected<std::unique_ptr<StreamingHandle>, errors::ErrorCode> {
    if (args.empty()) {
        return std::unexpected(errors::ErrorCode::INTERNAL_ERROR);
    }

    auto handle = std::make_unique<StreamingHandle>();

    if (::pipe(handle->stdout_pipe) < 0) {
        return std::unexpected(errors::ErrorCode::INTERNAL_ERROR);
    }
    if (::pipe(handle->stderr_pipe) < 0) {
        ::close(handle->stdout_pipe[0]); ::close(handle->stdout_pipe[1]);
        return std::unexpected(errors::ErrorCode::INTERNAL_ERROR);
    }
    if (::pipe(handle->shutdown_pipe) < 0) {
        ::close(handle->stdout_pipe[0]); ::close(handle->stdout_pipe[1]);
        ::close(handle->stderr_pipe[0]); ::close(handle->stderr_pipe[1]);
        return std::unexpected(errors::ErrorCode::INTERNAL_ERROR);
    }

    pid_t pid = ::fork();
    if (pid < 0) {
        ::close(handle->stdout_pipe[0]); ::close(handle->stdout_pipe[1]);
        ::close(handle->stderr_pipe[0]); ::close(handle->stderr_pipe[1]);
        ::close(handle->shutdown_pipe[0]); ::close(handle->shutdown_pipe[1]);
        return std::unexpected(errors::ErrorCode::INTERNAL_ERROR);
    }

    if (pid == 0) {
        // Child
        ::close(handle->stdout_pipe[0]);
        ::close(handle->stderr_pipe[0]);
        ::close(handle->shutdown_pipe[0]);
        ::close(handle->shutdown_pipe[1]);

        ::dup2(handle->stdout_pipe[1], STDOUT_FILENO);
        ::dup2(handle->stderr_pipe[1], STDERR_FILENO);
        if (handle->stdout_pipe[1] > STDERR_FILENO) ::close(handle->stdout_pipe[1]);
        if (handle->stderr_pipe[1] > STDERR_FILENO) ::close(handle->stderr_pipe[1]);

        if (!cwd.empty()) {
            ::chdir(cwd.c_str());
        }

        auto argv = build_argv(args);
        ::setpgid(0, 0);
        ::execvp(argv[0], argv.data());
        _exit(127);
    }

    // Parent
    ::close(handle->stdout_pipe[1]);
    ::close(handle->stderr_pipe[1]);

    handle->pid = pid;
    handle->running = true;

    // Start reader threads
    auto* raw = handle.get();
    if (callbacks.on_stdout) {
        raw->stdout_reader = std::thread(
            &SubprocessExecutor::reader_loop, raw, raw->stdout_pipe[0],
            std::move(callbacks.on_stdout));
    }
    if (callbacks.on_stderr) {
        raw->stderr_reader = std::thread(
            &SubprocessExecutor::reader_loop, raw, raw->stderr_pipe[0],
            std::move(callbacks.on_stderr));
    }

    // Detached waiter for exit notification
    if (callbacks.on_exit) {
        auto on_exit_cb = std::move(callbacks.on_exit);
        std::thread([raw, cb = std::move(on_exit_cb)]() {
            int status = 0;
            ::waitpid(raw->pid, &status, 0);
            int exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : 1;
            raw->running = false;

            // Signal reader threads to stop
            if (raw->shutdown_pipe[1] >= 0) {
                char c = 0;
                ssize_t ignored [[maybe_unused]] = ::write(raw->shutdown_pipe[1], &c, 1);
            }

            if (raw->stdout_reader.joinable()) raw->stdout_reader.join();
            if (raw->stderr_reader.joinable()) raw->stderr_reader.join();

            if (raw->stdout_pipe[0] >= 0) ::close(raw->stdout_pipe[0]);
            if (raw->stderr_pipe[0] >= 0) ::close(raw->stderr_pipe[0]);
            if (raw->shutdown_pipe[0] >= 0) ::close(raw->shutdown_pipe[0]);
            if (raw->shutdown_pipe[1] >= 0) ::close(raw->shutdown_pipe[1]);

            cb(exit_code);
        }).detach();
    }

    return handle;  // NOLINT — implicit conversion to expected via constructor
}

// --- Stop streaming ---

auto SubprocessExecutor::stop_streaming(std::unique_ptr<StreamingHandle> handle) -> void {
    if (!handle) return;

    if (handle->running) {
        handle->running = false;
        ::killpg(handle->pid, SIGTERM);
        ::usleep(500000);  // 500ms grace period
        ::killpg(handle->pid, SIGKILL);
    }

    // Signal reader threads
    if (handle->shutdown_pipe[1] >= 0) {
        char c = 0;
        ssize_t ignored [[maybe_unused]] = ::write(handle->shutdown_pipe[1], &c, 1);
    }

    if (handle->stdout_reader.joinable()) handle->stdout_reader.join();
    if (handle->stderr_reader.joinable()) handle->stderr_reader.join();

    // Close fds
    if (handle->stdout_pipe[0] >= 0) ::close(handle->stdout_pipe[0]);
    if (handle->stderr_pipe[0] >= 0) ::close(handle->stderr_pipe[0]);
    if (handle->shutdown_pipe[0] >= 0) ::close(handle->shutdown_pipe[0]);
    if (handle->shutdown_pipe[1] >= 0) ::close(handle->shutdown_pipe[1]);

    // Reap child
    int status = 0;
    ::waitpid(handle->pid, &status, 0);

    // unique_ptr destructor cleans up
}

// --- Command existence check ---

auto SubprocessExecutor::command_exists(const std::string& cmd) -> bool {
    std::string path_env;
    const char* env = std::getenv("PATH");
    if (!env) return false;
    path_env = env;

    std::string_view remaining = path_env;
    while (!remaining.empty()) {
        auto sep = remaining.find(':');
        auto entry = (sep == std::string_view::npos) ? remaining : remaining.substr(0, sep);
        if (sep == std::string_view::npos) {
            remaining = {};
        } else {
            remaining = remaining.substr(sep + 1);
        }

        auto candidate = std::filesystem::path(entry) / cmd;
        if (std::filesystem::exists(candidate) &&
            access(candidate.c_str(), X_OK) == 0) {
            return true;
        }
    }
    return false;
}

// --- Helpers ---

auto SubprocessExecutor::build_argv(const std::vector<std::string>& args)
    -> std::vector<char*> {
    std::vector<char*> argv;
    argv.reserve(args.size() + 1);
    for (auto& s : args) {
        argv.push_back(const_cast<char*>(s.c_str()));
    }
    argv.push_back(nullptr);
    return argv;
}

void SubprocessExecutor::reader_loop(StreamingHandle* handle, int fd,
                                      std::function<void(std::string_view)> callback) {
    std::string line_buf;
    char buf[4096];

    while (handle->running) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(fd, &read_fds);
        FD_SET(handle->shutdown_pipe[0], &read_fds);

        int max_fd = std::max(fd, handle->shutdown_pipe[0]) + 1;
        struct timeval tv{};
        tv.tv_usec = 100000;  // 100ms

        int ret = ::select(max_fd, &read_fds, nullptr, nullptr, &tv);
        if (ret < 0) {
            if (errno == EBADF) break;
            continue;
        }

        if (FD_ISSET(handle->shutdown_pipe[0], &read_fds)) break;

        if (ret > 0 && FD_ISSET(fd, &read_fds)) {
            ssize_t n = ::read(fd, buf, sizeof(buf));
            if (n > 0) {
                // Deliver line-by-line if possible, whole chunk otherwise
                line_buf.append(buf, static_cast<size_t>(n));
                for (;;) {
                    auto nl = line_buf.find('\n');
                    if (nl == std::string::npos) break;
                    auto line = line_buf.substr(0, nl);
                    if (callback) callback(line);
                    line_buf.erase(0, nl + 1);
                }
            } else if (n == 0) {
                break;  // EOF
            } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
                break;  // Real error
            }
        }
    }

    // Flush remaining buffered data
    if (!line_buf.empty() && callback) {
        callback(line_buf);
    }
}

}  // namespace rosweb::subprocess
