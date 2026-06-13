#pragma once

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "errors/error_codes.hpp"
#include "models/tf_models.hpp"
#include "subprocess/subprocess_executor.hpp"
#include "tf/i_tf_listener.hpp"
#include "tf/i_tf_manager.hpp"
#include "workspace/i_workspace_aware.hpp"

namespace rosweb::tf {

class LocalTfManager : public ITfManager, public workspace::IWorkspaceAware {
public:
    explicit LocalTfManager(std::string workspace_root = ".");
    ~LocalTfManager() override;

    LocalTfManager(const LocalTfManager&) = delete;
    auto operator=(const LocalTfManager&) -> LocalTfManager& = delete;

    void set_workspace_root(const std::string& root) override;

    auto subscribe_tf(
        const std::string& subscription_id,
        const std::optional<std::vector<std::string>>& frames,
        const std::optional<int>& throttle_rate)
        -> std::expected<void, errors::ErrorCode> override;

    auto unsubscribe_tf(const std::string& subscription_id)
        -> std::expected<void, errors::ErrorCode> override;

    auto get_tf_tree()
        -> std::expected<models::TfTreePayload, errors::ErrorCode> override;

    auto add_listener(std::shared_ptr<ITfListener> listener) -> void override;
    auto remove_listener(std::shared_ptr<ITfListener> listener) -> void override;

    auto shutdown() -> void override;

private:
    struct TfSubscription {
        std::string subscription_id;
        std::optional<std::vector<std::string>> frames;
        std::optional<int> throttle_rate;
        std::chrono::steady_clock::time_point last_sent;
    };

    void handle_tf_stdout(std::string_view line);
    void start_tf_stream();
    void stop_tf_stream();

    void notify_tf_update(const std::string& subscription_id,
                           const std::vector<models::TfTransform>& transforms);

    // YAML parsing (reuse from LocalRosStreamManager approach)
    static auto yaml_block_to_json(const std::string& block) -> nlohmann::json;
    static auto yaml_value_to_json(const std::string& s) -> nlohmann::json;
    static auto yaml_lines_to_json(const std::vector<std::string>& lines,
                                    std::size_t start, int indent) -> std::pair<nlohmann::json, std::size_t>;
    static auto count_indent(const std::string& line) -> int;
    static auto trim_str(const std::string& s) -> std::string;
    static auto split_lines(const std::string& s) -> std::vector<std::string>;
    auto wrap_ros_command(const std::vector<std::string>& cmd) const
        -> std::vector<std::string>;
    auto current_workspace_root() const -> std::string;

    subprocess::SubprocessExecutor executor_;
    mutable std::mutex workspace_mutex_;
    std::string workspace_root_;

    std::unique_ptr<subprocess::StreamingHandle> tf_stream_handle_;
    std::string tf_buffer_;

    mutable std::mutex subscriptions_mutex_;
    std::unordered_map<std::string, TfSubscription> subscriptions_;

    mutable std::mutex listeners_mutex_;
    std::vector<std::shared_ptr<ITfListener>> listeners_;

    std::atomic<bool> shutting_down_{false};
};

}  // namespace rosweb::tf
