#include "terminal/local_pty_manager.hpp"

#include <fcntl.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace rosweb::terminal {

LocalPtyManager::LocalPtyManager(std::string workspace_root)
    : workspace_root_(std::move(workspace_root)) {}

LocalPtyManager::~LocalPtyManager() {
    close_all();
}

auto LocalPtyManager::create(
    const PtyCreateParams& params,
    std::function<void(const std::string&, std::string)> on_output,
    std::function<void(const std::string&, int)> on_exit
) -> std::expected<int, errors::ErrorCode> {
    std::lock_guard lock(mutex_);

    if (sessions_.size() >= MAX_TERMINALS) {
        return std::unexpected(errors::ErrorCode::TERMINAL_LIMIT_REACHED);
    }

    if (sessions_.count(params.terminal_id)) {
        return std::unexpected(errors::ErrorCode::TERMINAL_LIMIT_REACHED);
    }

    // Open PTY master
    int master_fd = posix_openpt(O_RDWR | O_NOCTTY);
    if (master_fd < 0) {
        return std::unexpected(errors::ErrorCode::INTERNAL_ERROR);
    }

    if (grantpt(master_fd) < 0 || unlockpt(master_fd) < 0) {
        ::close(master_fd);
        return std::unexpected(errors::ErrorCode::INTERNAL_ERROR);
    }

    char* slave_name = ptsname(master_fd);
    if (!slave_name) {
        ::close(master_fd);
        return std::unexpected(errors::ErrorCode::INTERNAL_ERROR);
    }

    int slave_fd = ::open(slave_name, O_RDWR | O_NOCTTY);
    if (slave_fd < 0) {
        ::close(master_fd);
        return std::unexpected(errors::ErrorCode::INTERNAL_ERROR);
    }

    // Set initial terminal size
    struct winsize ws {};
    ws.ws_col = static_cast<unsigned short>(std::max(params.cols, 1));
    ws.ws_row = static_cast<unsigned short>(std::max(params.rows, 1));
    ioctl(master_fd, TIOCSWINSZ, &ws);

    // Create shutdown pipe for clean thread termination
    int pipe_fds[2];
    if (pipe(pipe_fds) < 0) {
        ::close(slave_fd);
        ::close(master_fd);
        return std::unexpected(errors::ErrorCode::INTERNAL_ERROR);
    }

    pid_t pid = fork();
    if (pid < 0) {
        ::close(pipe_fds[0]);
        ::close(pipe_fds[1]);
        ::close(slave_fd);
        ::close(master_fd);
        return std::unexpected(errors::ErrorCode::INTERNAL_ERROR);
    }

    if (pid == 0) {
        // Child process
        ::close(pipe_fds[0]);
        ::close(pipe_fds[1]);
        ::close(master_fd);

        // Create new session and set slave as controlling terminal
        setsid();
        ioctl(slave_fd, TIOCSCTTY, 0);

        // Redirect stdin/stdout/stderr to slave PTY
        dup2(slave_fd, STDIN_FILENO);
        dup2(slave_fd, STDOUT_FILENO);
        dup2(slave_fd, STDERR_FILENO);
        if (slave_fd > STDERR_FILENO) {
            ::close(slave_fd);
        }

        // Change directory — use provided cwd or default to workspace root
        std::string effective_cwd = (params.cwd && !params.cwd->empty())
            ? *params.cwd : workspace_root_;
        if (!effective_cwd.empty()) {
            chdir(effective_cwd.c_str());
        }

        // Set environment variables
        if (params.env) {
            for (const auto& [key, value] : *params.env) {
                setenv(key.c_str(), value.c_str(), 1);
            }
        }

        // Determine shell to use
        std::string shell = params.shell.value_or("");
        if (shell.empty()) {
            const char* env_shell = std::getenv("SHELL");
            shell = env_shell ? env_shell : "/bin/sh";
        }

        execl(shell.c_str(), shell.c_str(), nullptr);
        _exit(127);
    }

    // Parent process
    ::close(slave_fd);

    auto session = std::make_unique<PtySession>();
    session->terminal_id = params.terminal_id;
    session->master_fd = master_fd;
    session->child_pid = pid;
    session->shutdown_pipe[0] = pipe_fds[0];
    session->shutdown_pipe[1] = pipe_fds[1];
    session->running = true;
    session->on_output = std::move(on_output);
    session->on_exit = std::move(on_exit);

    auto* raw_session = session.get();
    sessions_.emplace(params.terminal_id, std::move(session));

    // Start reader thread
    raw_session->reader_thread = std::thread(&LocalPtyManager::reader_loop, this, raw_session);

    return pid;
}

auto LocalPtyManager::write(const std::string& terminal_id, std::string_view data)
    -> std::expected<void, errors::ErrorCode> {
    std::lock_guard lock(mutex_);
    auto* session = find_session(terminal_id);
    if (!session) {
        return std::unexpected(errors::ErrorCode::TERMINAL_NOT_FOUND);
    }

    ssize_t written = ::write(session->master_fd, data.data(), data.size());
    if (written < 0) {
        return std::unexpected(errors::ErrorCode::INTERNAL_ERROR);
    }

    return {};
}

auto LocalPtyManager::resize(const std::string& terminal_id, int cols, int rows)
    -> std::expected<void, errors::ErrorCode> {
    std::lock_guard lock(mutex_);
    auto* session = find_session(terminal_id);
    if (!session) {
        return std::unexpected(errors::ErrorCode::TERMINAL_NOT_FOUND);
    }

    struct winsize ws {};
    ws.ws_col = static_cast<unsigned short>(std::max(cols, 1));
    ws.ws_row = static_cast<unsigned short>(std::max(rows, 1));
    ioctl(session->master_fd, TIOCSWINSZ, &ws);

    return {};
}

auto LocalPtyManager::kill(const std::string& terminal_id)
    -> std::expected<void, errors::ErrorCode> {
    std::unique_ptr<PtySession> session;

    {
        std::lock_guard lock(mutex_);
        auto it = sessions_.find(terminal_id);
        if (it == sessions_.end()) {
            return std::unexpected(errors::ErrorCode::TERMINAL_NOT_FOUND);
        }
        session = std::move(it->second);
        sessions_.erase(it);
    }

    shutdown_session(std::move(session));
    return {};
}

auto LocalPtyManager::close_all() -> void {
    std::unordered_map<std::string, std::unique_ptr<PtySession>> to_close;

    {
        std::lock_guard lock(mutex_);
        to_close = std::move(sessions_);
        sessions_.clear();
    }

    for (auto& [id, session] : to_close) {
        shutdown_session(std::move(session));
    }
}

auto LocalPtyManager::active_count() const -> size_t {
    std::lock_guard lock(mutex_);
    return sessions_.size();
}

void LocalPtyManager::reader_loop(PtySession* session) {
    char buf[4096];

    // Make master fd non-blocking
    int flags = fcntl(session->master_fd, F_GETFL, 0);
    fcntl(session->master_fd, F_SETFL, flags | O_NONBLOCK);

    while (session->running) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(session->master_fd, &read_fds);
        FD_SET(session->shutdown_pipe[0], &read_fds);

        int max_fd = std::max(session->master_fd, session->shutdown_pipe[0]) + 1;

        // Use a timeout so we periodically check session->running
        struct timeval tv {};
        tv.tv_usec = 100000;  // 100ms

        int ret = select(max_fd, &read_fds, nullptr, nullptr, &tv);

        if (ret < 0) {
            if (errno == EBADF) break;
            continue;
        }

        // Check if shutdown was signaled
        if (FD_ISSET(session->shutdown_pipe[0], &read_fds)) {
            break;
        }

        // Read PTY output
        if (ret > 0 && FD_ISSET(session->master_fd, &read_fds)) {
            ssize_t n = ::read(session->master_fd, buf, sizeof(buf));
            if (n > 0) {
                if (session->on_output) {
                    session->on_output(session->terminal_id, std::string(buf, static_cast<size_t>(n)));
                }
            } else if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
                break;
            }
        }
    }

    session->running = false;

    // Ensure child is killed
    killpg(session->child_pid, SIGKILL);

    // Wait for child to exit (fast after SIGKILL)
    int status = 0;
    waitpid(session->child_pid, &status, 0);
    int exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : 1;

    if (session->on_exit) {
        session->on_exit(session->terminal_id, exit_code);
    }
}

void LocalPtyManager::shutdown_session(std::unique_ptr<PtySession> session) {
    if (!session) return;

    session->running = false;

    // Signal the reader thread to wake up via the pipe
    if (session->shutdown_pipe[1] >= 0) {
        char c = 0;
        ssize_t ignored [[maybe_unused]] = ::write(session->shutdown_pipe[1], &c, 1);
    }

    // Kill child process — this closes the slave PTY, causing select()/read()
    // on the master to return with EOF, waking the reader thread
    killpg(session->child_pid, SIGKILL);

    // Join reader thread (it will call waitpid and on_exit, then return)
    if (session->reader_thread.joinable()) {
        session->reader_thread.join();
    }

    // Now safe to close all fds (reader thread has exited)
    if (session->master_fd >= 0) {
        ::close(session->master_fd);
    }
    if (session->shutdown_pipe[0] >= 0) {
        ::close(session->shutdown_pipe[0]);
    }
    if (session->shutdown_pipe[1] >= 0) {
        ::close(session->shutdown_pipe[1]);
    }
}

auto LocalPtyManager::find_session(const std::string& terminal_id) -> PtySession* {
    auto it = sessions_.find(terminal_id);
    return it != sessions_.end() ? it->second.get() : nullptr;
}

}  // namespace rosweb::terminal
