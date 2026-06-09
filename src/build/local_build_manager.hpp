#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "build/i_build_manager.hpp"
#include "build/i_build_listener.hpp"
#include "workspace/i_workspace_aware.hpp"

namespace rosweb::build {

class LocalBuildManager : public IBuildManager, public workspace::IWorkspaceAware {
public:
    explicit LocalBuildManager(std::string workspace_root);
    ~LocalBuildManager() override;

    LocalBuildManager(const LocalBuildManager&) = delete;
    auto operator=(const LocalBuildManager&) -> LocalBuildManager& = delete;

    void set_workspace_root(const std::string& root) override;

    auto start_build(const models::BuildRequest& request)
        -> std::expected<models::BuildResponse, errors::ErrorCode> override;

    auto get_build_status(const std::string& build_id) const
        -> std::expected<models::BuildStatusResponse, errors::ErrorCode> override;

    auto start_launch(const models::LaunchRequest& request)
        -> std::expected<models::LaunchResponse, errors::ErrorCode> override;

    auto stop_launch(const std::string& launch_id)
        -> std::expected<models::LaunchStopResponse, errors::ErrorCode> override;

    auto discover_launch_files() const
        -> std::expected<models::LaunchFilesResponse, errors::ErrorCode> override;

    auto add_listener(std::shared_ptr<IBuildListener> listener) -> void override;
    auto remove_listener(std::shared_ptr<IBuildListener> listener) -> void override;

    auto shutdown() -> void override;

private:
    struct BuildProcess {
        std::string build_id;
        pid_t pid = -1;
        int stdout_pipe[2] = {-1, -1};
        int stderr_pipe[2] = {-1, -1};
        int shutdown_pipe[2] = {-1, -1};
        std::thread stdout_reader;
        std::thread stderr_reader;
        std::atomic<bool> running{false};
        models::BuildStatus status = models::BuildStatus::running;
        std::vector<models::BuildTargetStatus> targets;
    };

    struct LaunchProcess {
        std::string launch_id;
        pid_t pid = -1;
        int stdout_pipe[2] = {-1, -1};
        int stderr_pipe[2] = {-1, -1};
        int shutdown_pipe[2] = {-1, -1};
        std::thread stdout_reader;
        std::thread stderr_reader;
        std::atomic<bool> running{false};
        models::LaunchStatus status = models::LaunchStatus::running;
        std::optional<int> exit_code;
    };

    auto generate_build_id() -> std::string;
    auto generate_launch_id() -> std::string;
    auto build_colcon_command(const models::BuildRequest& request) const
        -> std::vector<std::string>;
    auto build_launch_command(const models::LaunchRequest& request) const
        -> std::vector<std::string>;

    void spawn_build_process(std::unique_ptr<BuildProcess> proc,
                              const std::vector<std::string>& cmd);
    void spawn_launch_process(std::unique_ptr<LaunchProcess> proc,
                               const std::vector<std::string>& cmd);

    void build_reader_loop(BuildProcess* proc, const std::string& stream, int fd);
    void launch_reader_loop(LaunchProcess* proc, const std::string& stream, int fd);
    void wait_for_build(BuildProcess* proc);
    void wait_for_launch(LaunchProcess* proc);

    void notify_build_output(const std::string& build_id,
                              const std::string& target,
                              const std::string& stream,
                              const std::string& data);
    void notify_build_status(const std::string& build_id,
                              models::BuildStatus status,
                              const std::vector<models::BuildTargetStatus>& targets);
    void notify_launch_output(const std::string& launch_id,
                               const std::string& node,
                               const std::string& stream,
                               const std::string& data);
    void notify_launch_status(const std::string& launch_id,
                               models::LaunchStatus status,
                               int exit_code);

    auto find_package_for_path(const std::string& file_path) const -> std::string;
    auto current_workspace_root() const -> std::string;

    mutable std::mutex workspace_mutex_;
    std::string workspace_root_;

    mutable std::mutex builds_mutex_;
    std::unordered_map<std::string, std::unique_ptr<BuildProcess>> builds_;

    mutable std::mutex launches_mutex_;
    std::unordered_map<std::string, std::unique_ptr<LaunchProcess>> launches_;

    mutable std::mutex listeners_mutex_;
    std::vector<std::shared_ptr<IBuildListener>> listeners_;
};

}  // namespace rosweb::build
