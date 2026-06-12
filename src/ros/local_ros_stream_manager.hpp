#pragma once

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <nlohmann/json.hpp>
#include "errors/error_codes.hpp"
#include "ros/i_ros_stream_manager.hpp"
#include "subprocess/subprocess_executor.hpp"
#include "workspace/i_workspace_aware.hpp"

namespace rosweb::ros {

class LocalRosStreamManager : public IRosStreamManager, public workspace::IWorkspaceAware {
public:
    explicit LocalRosStreamManager(std::string workspace_root = ".");
    ~LocalRosStreamManager() override;

    LocalRosStreamManager(const LocalRosStreamManager&) = delete;
    auto operator=(const LocalRosStreamManager&) -> LocalRosStreamManager& = delete;

    void set_workspace_root(const std::string& root) override;

    auto subscribe_topic(
        const std::string& subscription_id,
        const std::string& topic,
        const std::optional<std::string>& type,
        const std::optional<int>& throttle_rate,
        const std::optional<int>& queue_length)
        -> std::expected<void, errors::ErrorCode> override;

    auto unsubscribe_topic(const std::string& subscription_id)
        -> std::expected<void, errors::ErrorCode> override;

    auto publish_topic(
        const std::string& topic,
        const std::string& type,
        const nlohmann::json& message)
        -> std::expected<void, errors::ErrorCode> override;

    auto call_service(
        const std::string& call_id,
        const std::string& service,
        const std::string& type,
        const nlohmann::json& request,
        const std::optional<int>& timeout)
        -> std::expected<void, errors::ErrorCode> override;

    auto call_action(
        const std::string& call_id,
        const std::string& action,
        const std::string& type,
        const nlohmann::json& goal,
        const std::optional<int>& timeout)
        -> std::expected<void, errors::ErrorCode> override;

    auto cancel_action(const std::string& call_id)
        -> std::expected<void, errors::ErrorCode> override;

    auto start_bag(
        const std::string& bag_id,
        const std::optional<std::vector<std::string>>& topics,
        const std::string& path,
        const std::optional<std::string>& format)
        -> std::expected<void, errors::ErrorCode> override;

    auto stop_bag(const std::string& bag_id)
        -> std::expected<void, errors::ErrorCode> override;

    auto start_node_monitor() -> void override;
    auto stop_node_monitor() -> void override;

    auto add_listener(std::shared_ptr<IRosStreamListener> listener) -> void override;
    auto remove_listener(std::shared_ptr<IRosStreamListener> listener) -> void override;

    auto shutdown() -> void override;

private:
    struct TopicSubscription {
        std::string subscription_id;
        std::string topic;
        std::optional<int> throttle_rate;
        std::chrono::steady_clock::time_point last_sent;
    };

    struct TopicStream {
        std::string topic;
        std::unique_ptr<subprocess::StreamingHandle> handle;
        std::vector<std::string> subscription_ids;
        std::string buffer;
    };

    struct ActionCall {
        std::string call_id;
        std::string action;
        std::unique_ptr<subprocess::StreamingHandle> handle;
        std::string buffer;
        bool got_result = false;
    };

    struct BagRecording {
        std::string bag_id;
        std::string path;
        std::unique_ptr<subprocess::StreamingHandle> handle;
        std::chrono::steady_clock::time_point start_time;
    };

    // Listener notification helpers
    void notify_topic_message(const std::string& subscription_id,
                               const std::string& topic,
                               const std::string& timestamp,
                               const nlohmann::json& message);
    void notify_service_result(const std::string& call_id,
                                bool success,
                                const std::optional<nlohmann::json>& result,
                                const std::optional<std::string>& error);
    void notify_action_feedback(const std::string& call_id,
                                 const nlohmann::json& feedback);
    void notify_action_result(const std::string& call_id,
                               const std::string& status,
                               const std::optional<nlohmann::json>& result);
    void notify_node_event(const std::string& event,
                            const std::string& node);
    void notify_bag_status(const std::string& bag_id,
                            const std::string& status,
                            const std::optional<double>& duration,
                            const std::optional<int>& message_count,
                            const std::optional<double>& size_bytes);

    // YAML/JSON parsing helpers
    static auto yaml_block_to_json(const std::string& block) -> nlohmann::json;
    static auto yaml_value_to_json(const std::string& s) -> nlohmann::json;
    static auto yaml_lines_to_json(const std::vector<std::string>& lines,
                                    std::size_t start, int indent) -> std::pair<nlohmann::json, std::size_t>;
    static auto count_indent(const std::string& line) -> int;
    static auto trim_str(const std::string& s) -> std::string;
    static auto split_lines(const std::string& s) -> std::vector<std::string>;

    // Streaming callbacks
    void handle_topic_stdout(const std::string& topic, std::string_view line);
    void handle_action_stdout(const std::string& call_id, std::string_view line);

    // Topic stream management
    void stop_topic_stream(const std::string& topic);
    auto wrap_ros_command(const std::vector<std::string>& cmd) const
        -> std::vector<std::string>;

    subprocess::SubprocessExecutor executor_;
    mutable std::mutex workspace_mutex_;
    std::string workspace_root_;

    // Topic subscriptions and streams
    mutable std::mutex topics_mutex_;
    std::unordered_map<std::string, TopicStream> topic_streams_;
    std::unordered_map<std::string, TopicSubscription> subscriptions_;

    // Service calls (tracked threads for clean shutdown)
    struct TrackedThread {
        std::thread thread;
        std::atomic<bool> completed{false};
    };
    mutable std::mutex services_mutex_;
    std::unordered_set<std::string> active_service_calls_;
    std::mutex service_threads_mutex_;
    std::vector<std::unique_ptr<TrackedThread>> service_threads_;
    void cleanup_service_threads();

    // Action calls
    mutable std::mutex actions_mutex_;
    std::unordered_map<std::string, ActionCall> action_calls_;

    // Bag recordings
    mutable std::mutex bags_mutex_;
    std::unordered_map<std::string, BagRecording> bag_recordings_;

    // Node monitor
    std::thread node_poll_thread_;
    std::atomic<bool> node_monitor_running_{false};
    mutable std::mutex node_mutex_;
    std::unordered_set<std::string> last_known_nodes_;
    void node_poll_loop();

    // Listeners
    mutable std::mutex listeners_mutex_;
    std::vector<std::shared_ptr<IRosStreamListener>> listeners_;

    std::atomic<bool> shutting_down_{false};
};

}  // namespace rosweb::ros
