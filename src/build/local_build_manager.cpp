#include "build/local_build_manager.hpp"

#include <fcntl.h>
#include <signal.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <random>
#include <stdexcept>

#include "errors/error_codes.hpp"

namespace rosweb::build {

LocalBuildManager::LocalBuildManager(std::string workspace_root)
    : workspace_root_(std::move(workspace_root)) {}

LocalBuildManager::~LocalBuildManager() {
    shutdown();
}

auto LocalBuildManager::generate_build_id() -> std::string {
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count() % 1000000;
    std::random_device rd;
    auto rand_val = rd() % 10000;
    return "b_" + std::to_string(ms) + "_" + std::to_string(rand_val);
}

auto LocalBuildManager::generate_launch_id() -> std::string {
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count() % 1000000;
    std::random_device rd;
    auto rand_val = rd() % 10000;
    return "l_" + std::to_string(ms) + "_" + std::to_string(rand_val);
}

auto LocalBuildManager::build_colcon_command(const models::BuildRequest& request) const
    -> std::vector<std::string> {
    std::vector<std::string> cmd = {"colcon", "build"};
    if (request.clean) {
        cmd.push_back("--cmake-clean-cache");
    }
    if (request.targets && !request.targets->empty()) {
        cmd.push_back("--packages-select");
        for (const auto& t : *request.targets) {
            cmd.push_back(t);
        }
    }
    if (request.args) {
        for (const auto& a : *request.args) {
            cmd.push_back(a);
        }
    }
    return cmd;
}

auto LocalBuildManager::build_launch_command(const models::LaunchRequest& request) const
    -> std::vector<std::string> {
    std::vector<std::string> cmd = {"ros2", "launch", request.package, request.file};
    if (request.arguments) {
        for (const auto& [key, value] : *request.arguments) {
            cmd.push_back(key + ":=" + value);
        }
    }
    return cmd;
}

auto LocalBuildManager::start_build(const models::BuildRequest& request)
    -> std::expected<models::BuildResponse, errors::ErrorCode> {
    std::lock_guard lock(builds_mutex_);

    // Only one build at a time
    for (const auto& [id, proc] : builds_) {
        if (proc->running) {
            return std::unexpected(errors::ErrorCode::BUILD_IN_PROGRESS);
        }
    }

    auto proc = std::make_unique<BuildProcess>();
    proc->build_id = generate_build_id();
    proc->status = models::BuildStatus::running;

    auto cmd = build_colcon_command(request);

    models::BuildResponse response{
        .build_id = proc->build_id,
        .status = models::BuildStatus::running,
    };

    spawn_build_process(std::move(proc), cmd);
    return response;
}

auto LocalBuildManager::get_build_status(const std::string& build_id) const
    -> std::expected<models::BuildStatusResponse, errors::ErrorCode> {
    std::lock_guard lock(builds_mutex_);
    auto it = builds_.find(build_id);
    if (it == builds_.end()) {
        return std::unexpected(errors::ErrorCode::BUILD_NOT_FOUND);
    }
    const auto& proc = it->second;
    return models::BuildStatusResponse{
        .build_id = proc->build_id,
        .status = proc->status,
        .targets = proc->targets,
    };
}

auto LocalBuildManager::start_launch(const models::LaunchRequest& request)
    -> std::expected<models::LaunchResponse, errors::ErrorCode> {
    std::lock_guard lock(launches_mutex_);

    auto proc = std::make_unique<LaunchProcess>();
    proc->launch_id = generate_launch_id();
    proc->status = models::LaunchStatus::running;

    auto cmd = build_launch_command(request);

    models::LaunchResponse response{
        .launch_id = proc->launch_id,
        .status = models::LaunchStatus::running,
        .pid = 0,  // will be filled by spawn
    };

    spawn_launch_process(std::move(proc), cmd);

    // Get the pid back from the stored process
    auto it = launches_.find(response.launch_id);
    if (it != launches_.end()) {
        response.pid = it->second->pid;
    }

    return response;
}

auto LocalBuildManager::stop_launch(const std::string& launch_id)
    -> std::expected<models::LaunchStopResponse, errors::ErrorCode> {
    std::unique_ptr<LaunchProcess> proc;

    {
        std::lock_guard lock(launches_mutex_);
        auto it = launches_.find(launch_id);
        if (it == launches_.end()) {
            return std::unexpected(errors::ErrorCode::LAUNCH_NOT_FOUND);
        }
        proc = std::move(it->second);
        launches_.erase(it);
    }

    if (proc->running) {
        proc->running = false;
        killpg(proc->pid, SIGTERM);
        // Allow a brief grace period then force kill
        usleep(500000);  // 500ms
        killpg(proc->pid, SIGKILL);
    }

    // Signal reader threads to stop
    if (proc->shutdown_pipe[1] >= 0) {
        char c = 0;
        ssize_t ignored [[maybe_unused]] = ::write(proc->shutdown_pipe[1], &c, 1);
    }

    if (proc->stdout_reader.joinable()) proc->stdout_reader.join();
    if (proc->stderr_reader.joinable()) proc->stderr_reader.join();

    // Close fds
    if (proc->stdout_pipe[0] >= 0) ::close(proc->stdout_pipe[0]);
    if (proc->stderr_pipe[0] >= 0) ::close(proc->stderr_pipe[0]);
    if (proc->shutdown_pipe[0] >= 0) ::close(proc->shutdown_pipe[0]);
    if (proc->shutdown_pipe[1] >= 0) ::close(proc->shutdown_pipe[1]);

    // Wait for child to prevent zombies
    int status = 0;
    waitpid(proc->pid, &status, 0);

    auto exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : 1;
    notify_launch_status(launch_id, models::LaunchStatus::stopped, exit_code);

    return models::LaunchStopResponse{
        .launch_id = launch_id,
        .status = models::LaunchStatus::stopped,
    };
}

auto LocalBuildManager::discover_launch_files() const
    -> std::expected<models::LaunchFilesResponse, errors::ErrorCode> {
    models::LaunchFilesResponse response;
    namespace fs = std::filesystem;
    std::error_code ec;

    for (auto it = fs::recursive_directory_iterator(
            workspace_root_, fs::directory_options::skip_permission_denied, ec);
         it != fs::recursive_directory_iterator(); ) {
        if (ec) {
            ec.clear();
            it.increment(ec);
            continue;
        }

        const auto& entry = *it;
        if (entry.is_directory()) {
            auto name = entry.path().filename().string();
            if (name == ".git" || name == "build" || name == "install" ||
                name == "log" || name == "node_modules") {
                it.disable_recursion_pending();
            }
            it.increment(ec);
            continue;
        }

        if (entry.is_regular_file()) {
            auto fname = entry.path().filename().string();
            if (fname.find(".launch.") != std::string::npos) {
                models::LaunchFileInfo info;
                info.path = entry.path().string();
                info.package = find_package_for_path(info.path);
                response.files.push_back(std::move(info));
            }
        }

        it.increment(ec);
    }

    return response;
}

auto LocalBuildManager::add_listener(std::shared_ptr<IBuildListener> listener) -> void {
    std::lock_guard lock(listeners_mutex_);
    listeners_.push_back(std::move(listener));
}

auto LocalBuildManager::remove_listener(std::shared_ptr<IBuildListener> listener) -> void {
    std::lock_guard lock(listeners_mutex_);
    auto it = std::find(listeners_.begin(), listeners_.end(), listener);
    if (it != listeners_.end()) {
        listeners_.erase(it);
    }
}

auto LocalBuildManager::shutdown() -> void {
    // Stop all launches
    {
        std::lock_guard lock(launches_mutex_);
        for (auto& [id, proc] : launches_) {
            if (proc->running) {
                proc->running = false;
                killpg(proc->pid, SIGKILL);
            }
            if (proc->shutdown_pipe[1] >= 0) {
                char c = 0;
                ssize_t ignored [[maybe_unused]] = ::write(proc->shutdown_pipe[1], &c, 1);
            }
        }
    }

    // Stop all builds
    {
        std::lock_guard lock(builds_mutex_);
        for (auto& [id, proc] : builds_) {
            if (proc->running) {
                proc->running = false;
                killpg(proc->pid, SIGKILL);
            }
            if (proc->shutdown_pipe[1] >= 0) {
                char c = 0;
                ssize_t ignored [[maybe_unused]] = ::write(proc->shutdown_pipe[1], &c, 1);
            }
        }
    }

    // Join all threads
    {
        std::lock_guard lock(launches_mutex_);
        for (auto& [id, proc] : launches_) {
            if (proc->stdout_reader.joinable()) proc->stdout_reader.join();
            if (proc->stderr_reader.joinable()) proc->stderr_reader.join();
        }
    }
    {
        std::lock_guard lock(builds_mutex_);
        for (auto& [id, proc] : builds_) {
            if (proc->stdout_reader.joinable()) proc->stdout_reader.join();
            if (proc->stderr_reader.joinable()) proc->stderr_reader.join();
        }
    }

    // Close fds and waitpid
    {
        std::lock_guard lock(builds_mutex_);
        for (auto& [id, proc] : builds_) {
            if (proc->stdout_pipe[0] >= 0) ::close(proc->stdout_pipe[0]);
            if (proc->stderr_pipe[0] >= 0) ::close(proc->stderr_pipe[0]);
            if (proc->shutdown_pipe[0] >= 0) ::close(proc->shutdown_pipe[0]);
            if (proc->shutdown_pipe[1] >= 0) ::close(proc->shutdown_pipe[1]);
            int status = 0;
            waitpid(proc->pid, &status, 0);
        }
        builds_.clear();
    }
    {
        std::lock_guard lock(launches_mutex_);
        for (auto& [id, proc] : launches_) {
            if (proc->stdout_pipe[0] >= 0) ::close(proc->stdout_pipe[0]);
            if (proc->stderr_pipe[0] >= 0) ::close(proc->stderr_pipe[0]);
            if (proc->shutdown_pipe[0] >= 0) ::close(proc->shutdown_pipe[0]);
            if (proc->shutdown_pipe[1] >= 0) ::close(proc->shutdown_pipe[1]);
            int status = 0;
            waitpid(proc->pid, &status, 0);
        }
        launches_.clear();
    }
}

// --- Subprocess spawning ---

void LocalBuildManager::spawn_build_process(std::unique_ptr<BuildProcess> proc,
                                              const std::vector<std::string>& cmd) {
    if (pipe(proc->stdout_pipe) < 0) return;
    if (pipe(proc->stderr_pipe) < 0) {
        ::close(proc->stdout_pipe[0]); ::close(proc->stdout_pipe[1]);
        return;
    }
    if (pipe(proc->shutdown_pipe) < 0) {
        ::close(proc->stdout_pipe[0]); ::close(proc->stdout_pipe[1]);
        ::close(proc->stderr_pipe[0]); ::close(proc->stderr_pipe[1]);
        return;
    }

    pid_t pid = fork();
    if (pid < 0) {
        ::close(proc->stdout_pipe[0]); ::close(proc->stdout_pipe[1]);
        ::close(proc->stderr_pipe[0]); ::close(proc->stderr_pipe[1]);
        ::close(proc->shutdown_pipe[0]); ::close(proc->shutdown_pipe[1]);
        return;
    }

    if (pid == 0) {
        // Child
        ::close(proc->stdout_pipe[0]);
        ::close(proc->stderr_pipe[0]);
        ::close(proc->shutdown_pipe[0]);
        ::close(proc->shutdown_pipe[1]);

        dup2(proc->stdout_pipe[1], STDOUT_FILENO);
        dup2(proc->stderr_pipe[1], STDERR_FILENO);
        if (proc->stdout_pipe[1] > STDERR_FILENO) ::close(proc->stdout_pipe[1]);
        if (proc->stderr_pipe[1] > STDERR_FILENO) ::close(proc->stderr_pipe[1]);

        chdir(workspace_root_.c_str());

        // Build argv for execvp
        std::vector<char*> argv;
        for (auto& s : cmd) argv.push_back(const_cast<char*>(s.c_str()));
        argv.push_back(nullptr);

        setpgid(0, 0);
        execvp(argv[0], argv.data());
        _exit(127);
    }

    // Parent
    ::close(proc->stdout_pipe[1]);
    ::close(proc->stderr_pipe[1]);

    proc->pid = pid;
    proc->running = true;

    auto* raw = proc.get();
    auto build_id = proc->build_id;
    builds_.emplace(build_id, std::move(proc));

    raw->stdout_reader = std::thread(
        &LocalBuildManager::build_reader_loop, this, raw, "stdout", raw->stdout_pipe[0]);
    raw->stderr_reader = std::thread(
        &LocalBuildManager::build_reader_loop, this, raw, "stderr", raw->stderr_pipe[0]);

    // Detached waiter for exit status
    std::thread(&LocalBuildManager::wait_for_build, this, raw).detach();
}

void LocalBuildManager::spawn_launch_process(std::unique_ptr<LaunchProcess> proc,
                                               const std::vector<std::string>& cmd) {
    if (pipe(proc->stdout_pipe) < 0) return;
    if (pipe(proc->stderr_pipe) < 0) {
        ::close(proc->stdout_pipe[0]); ::close(proc->stdout_pipe[1]);
        return;
    }
    if (pipe(proc->shutdown_pipe) < 0) {
        ::close(proc->stdout_pipe[0]); ::close(proc->stdout_pipe[1]);
        ::close(proc->stderr_pipe[0]); ::close(proc->stderr_pipe[1]);
        return;
    }

    pid_t pid = fork();
    if (pid < 0) {
        ::close(proc->stdout_pipe[0]); ::close(proc->stdout_pipe[1]);
        ::close(proc->stderr_pipe[0]); ::close(proc->stderr_pipe[1]);
        ::close(proc->shutdown_pipe[0]); ::close(proc->shutdown_pipe[1]);
        return;
    }

    if (pid == 0) {
        // Child
        ::close(proc->stdout_pipe[0]);
        ::close(proc->stderr_pipe[0]);
        ::close(proc->shutdown_pipe[0]);
        ::close(proc->shutdown_pipe[1]);

        dup2(proc->stdout_pipe[1], STDOUT_FILENO);
        dup2(proc->stderr_pipe[1], STDERR_FILENO);
        if (proc->stdout_pipe[1] > STDERR_FILENO) ::close(proc->stdout_pipe[1]);
        if (proc->stderr_pipe[1] > STDERR_FILENO) ::close(proc->stderr_pipe[1]);

        chdir(workspace_root_.c_str());

        std::vector<char*> argv;
        for (auto& s : cmd) argv.push_back(const_cast<char*>(s.c_str()));
        argv.push_back(nullptr);

        setpgid(0, 0);
        execvp(argv[0], argv.data());
        _exit(127);
    }

    // Parent
    ::close(proc->stdout_pipe[1]);
    ::close(proc->stderr_pipe[1]);

    proc->pid = pid;
    proc->running = true;

    auto* raw = proc.get();
    auto launch_id = proc->launch_id;
    launches_.emplace(launch_id, std::move(proc));

    raw->stdout_reader = std::thread(
        &LocalBuildManager::launch_reader_loop, this, raw, "stdout", raw->stdout_pipe[0]);
    raw->stderr_reader = std::thread(
        &LocalBuildManager::launch_reader_loop, this, raw, "stderr", raw->stderr_pipe[0]);

    std::thread(&LocalBuildManager::wait_for_launch, this, raw).detach();
}

// --- Reader loops ---

void LocalBuildManager::build_reader_loop(BuildProcess* proc,
                                            const std::string& stream, int fd) {
    char buf[4096];
    while (proc->running) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(fd, &read_fds);
        FD_SET(proc->shutdown_pipe[0], &read_fds);

        int max_fd = std::max(fd, proc->shutdown_pipe[0]) + 1;
        struct timeval tv{};
        tv.tv_usec = 100000;  // 100ms

        int ret = select(max_fd, &read_fds, nullptr, nullptr, &tv);
        if (ret < 0) {
            if (errno == EBADF) break;
            continue;
        }

        if (FD_ISSET(proc->shutdown_pipe[0], &read_fds)) break;

        if (ret > 0 && FD_ISSET(fd, &read_fds)) {
            ssize_t n = ::read(fd, buf, sizeof(buf));
            if (n > 0) {
                notify_build_output(proc->build_id, "", stream, std::string(buf, static_cast<size_t>(n)));
            } else if (n <= 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                break;
            }
        }
    }
}

void LocalBuildManager::launch_reader_loop(LaunchProcess* proc,
                                             const std::string& stream, int fd) {
    char buf[4096];
    while (proc->running) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(fd, &read_fds);
        FD_SET(proc->shutdown_pipe[0], &read_fds);

        int max_fd = std::max(fd, proc->shutdown_pipe[0]) + 1;
        struct timeval tv{};
        tv.tv_usec = 100000;  // 100ms

        int ret = select(max_fd, &read_fds, nullptr, nullptr, &tv);
        if (ret < 0) {
            if (errno == EBADF) break;
            continue;
        }

        if (FD_ISSET(proc->shutdown_pipe[0], &read_fds)) break;

        if (ret > 0 && FD_ISSET(fd, &read_fds)) {
            ssize_t n = ::read(fd, buf, sizeof(buf));
            if (n > 0) {
                notify_launch_output(proc->launch_id, "", stream, std::string(buf, static_cast<size_t>(n)));
            } else if (n <= 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                break;
            }
        }
    }
}

// --- Wait for process exit ---

void LocalBuildManager::wait_for_build(BuildProcess* proc) {
    int status = 0;
    waitpid(proc->pid, &status, 0);
    int exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : 1;

    proc->running = false;

    // Signal readers to stop
    if (proc->shutdown_pipe[1] >= 0) {
        char c = 0;
        ssize_t ignored [[maybe_unused]] = ::write(proc->shutdown_pipe[1], &c, 1);
    }

    if (proc->stdout_reader.joinable()) proc->stdout_reader.join();
    if (proc->stderr_reader.joinable()) proc->stderr_reader.join();

    // Close fds
    if (proc->stdout_pipe[0] >= 0) ::close(proc->stdout_pipe[0]);
    if (proc->stderr_pipe[0] >= 0) ::close(proc->stderr_pipe[0]);
    if (proc->shutdown_pipe[0] >= 0) ::close(proc->shutdown_pipe[0]);
    if (proc->shutdown_pipe[1] >= 0) ::close(proc->shutdown_pipe[1]);

    auto new_status = (exit_code == 0)
        ? models::BuildStatus::completed
        : models::BuildStatus::failed;
    proc->status = new_status;

    notify_build_status(proc->build_id, new_status, proc->targets);
}

void LocalBuildManager::wait_for_launch(LaunchProcess* proc) {
    int status = 0;
    waitpid(proc->pid, &status, 0);
    int exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : 1;

    proc->running = false;

    if (proc->shutdown_pipe[1] >= 0) {
        char c = 0;
        ssize_t ignored [[maybe_unused]] = ::write(proc->shutdown_pipe[1], &c, 1);
    }

    if (proc->stdout_reader.joinable()) proc->stdout_reader.join();
    if (proc->stderr_reader.joinable()) proc->stderr_reader.join();

    if (proc->stdout_pipe[0] >= 0) ::close(proc->stdout_pipe[0]);
    if (proc->stderr_pipe[0] >= 0) ::close(proc->stderr_pipe[0]);
    if (proc->shutdown_pipe[0] >= 0) ::close(proc->shutdown_pipe[0]);
    if (proc->shutdown_pipe[1] >= 0) ::close(proc->shutdown_pipe[1]);

    proc->status = models::LaunchStatus::stopped;
    proc->exit_code = exit_code;

    notify_launch_status(proc->launch_id, models::LaunchStatus::stopped, exit_code);
}

// --- Listener notification ---

void LocalBuildManager::notify_build_output(const std::string& build_id,
                                              const std::string& target,
                                              const std::string& stream,
                                              const std::string& data) {
    std::vector<std::shared_ptr<IBuildListener>> listeners_copy;
    {
        std::lock_guard lock(listeners_mutex_);
        listeners_copy = listeners_;
    }
    for (auto& l : listeners_copy) {
        l->on_build_output(build_id, target, stream, data);
    }
}

void LocalBuildManager::notify_build_status(const std::string& build_id,
                                              models::BuildStatus status,
                                              const std::vector<models::BuildTargetStatus>& targets) {
    std::vector<std::shared_ptr<IBuildListener>> listeners_copy;
    {
        std::lock_guard lock(listeners_mutex_);
        listeners_copy = listeners_;
    }
    for (auto& l : listeners_copy) {
        l->on_build_status_changed(build_id, status, targets);
    }
}

void LocalBuildManager::notify_launch_output(const std::string& launch_id,
                                               const std::string& node,
                                               const std::string& stream,
                                               const std::string& data) {
    std::vector<std::shared_ptr<IBuildListener>> listeners_copy;
    {
        std::lock_guard lock(listeners_mutex_);
        listeners_copy = listeners_;
    }
    for (auto& l : listeners_copy) {
        l->on_launch_output(launch_id, node, stream, data);
    }
}

void LocalBuildManager::notify_launch_status(const std::string& launch_id,
                                               models::LaunchStatus status,
                                               int exit_code) {
    std::vector<std::shared_ptr<IBuildListener>> listeners_copy;
    {
        std::lock_guard lock(listeners_mutex_);
        listeners_copy = listeners_;
    }
    for (auto& l : listeners_copy) {
        l->on_launch_status_changed(launch_id, status, exit_code);
    }
}

auto LocalBuildManager::find_package_for_path(const std::string& file_path) const -> std::string {
    namespace fs = std::filesystem;
    auto dir = fs::path(file_path).parent_path();
    while (!dir.empty()) {
        auto pkg_xml = dir / "package.xml";
        if (fs::exists(pkg_xml)) {
            return dir.filename().string();
        }
        auto parent = dir.parent_path();
        if (parent == dir) break;
        dir = parent;
    }
    return "";
}

}  // namespace rosweb::build
