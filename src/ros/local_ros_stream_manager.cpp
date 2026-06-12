#include "ros/local_ros_stream_manager.hpp"

#include "subprocess/ros_setup.hpp"

#include <algorithm>
#include <chrono>
#include <sstream>

namespace rosweb::ros {

LocalRosStreamManager::LocalRosStreamManager(std::string workspace_root)
    : workspace_root_(std::move(workspace_root)) {}

void LocalRosStreamManager::set_workspace_root(const std::string& root) {
    std::lock_guard lock(workspace_mutex_);
    workspace_root_ = root;
}

auto LocalRosStreamManager::wrap_ros_command(const std::vector<std::string>& cmd) const
    -> std::vector<std::string> {
    std::string root;
    {
        std::lock_guard lock(workspace_mutex_);
        root = workspace_root_;
    }
    return subprocess::wrap_with_ros_setup(root, cmd);
}

LocalRosStreamManager::~LocalRosStreamManager() {
    shutdown();
}

// --- Subscribe/unsubscribe topic ---

auto LocalRosStreamManager::subscribe_topic(
    const std::string& subscription_id,
    const std::string& topic,
    const std::optional<std::string>& /*type*/,
    const std::optional<int>& throttle_rate,
    const std::optional<int>& /*queue_length*/)
    -> std::expected<void, errors::ErrorCode> {
    std::lock_guard lock(topics_mutex_);

    if (subscriptions_.count(subscription_id)) {
        return std::unexpected(errors::ErrorCode::INVALID_PAYLOAD);
    }

    TopicSubscription sub{
        .subscription_id = subscription_id,
        .topic = topic,
        .throttle_rate = throttle_rate,
        .last_sent = {},
    };

    auto it = topic_streams_.find(topic);
    if (it != topic_streams_.end()) {
        it->second.subscription_ids.push_back(subscription_id);
        subscriptions_.emplace(subscription_id, std::move(sub));
        return {};
    }

    // Start a new streaming subprocess for this topic
    auto* self = this;
    std::string topic_copy = topic;

    subprocess::StreamCallbacks callbacks;
    callbacks.on_stdout = [self, topic_copy](std::string_view line) {
        self->handle_topic_stdout(topic_copy, line);
    };
    callbacks.on_stderr = [](std::string_view) {};
    callbacks.on_exit = [self, topic_copy](int) {
        // Stream ended unexpectedly — just clean up maps
        // (handle cleanup is done by the exit-waiter or stop_streaming)
        std::lock_guard lock(self->topics_mutex_);
        auto it = self->topic_streams_.find(topic_copy);
        if (it == self->topic_streams_.end()) return;
        for (const auto& sub_id : it->second.subscription_ids) {
            self->subscriptions_.erase(sub_id);
        }
        self->topic_streams_.erase(it);
    };

    auto handle = executor_.start_streaming(
        wrap_ros_command({"ros2", "topic", "echo", topic}), callbacks);
    if (!handle.has_value()) {
        return std::unexpected(handle.error());
    }

    TopicStream stream;
    stream.topic = topic;
    stream.handle = std::move(handle.value());
    stream.subscription_ids.push_back(subscription_id);
    topic_streams_.emplace(topic, std::move(stream));
    subscriptions_.emplace(subscription_id, std::move(sub));
    return {};
}

void LocalRosStreamManager::handle_topic_stdout(const std::string& topic,
                                                  std::string_view line) {
    std::lock_guard lock(topics_mutex_);

    auto it = topic_streams_.find(topic);
    if (it == topic_streams_.end()) return;

    auto& stream = it->second;

    // ros2 topic echo separates messages with "---"
    std::string line_str(line);
    if (line_str == "---" || line_str == "---\n") {
        // End of a message block — parse and deliver
        if (!stream.buffer.empty()) {
            auto msg_json = yaml_block_to_json(stream.buffer);
            auto now = std::chrono::system_clock::now();
            auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                now.time_since_epoch()).count();

            for (const auto& sub_id : stream.subscription_ids) {
                auto sub_it = subscriptions_.find(sub_id);
                if (sub_it == subscriptions_.end()) continue;

                auto& sub = sub_it->second;

                // Apply throttle
                if (sub.throttle_rate.has_value() && sub.throttle_rate.value() > 0) {
                    auto elapsed = std::chrono::steady_clock::now() - sub.last_sent;
                    auto min_interval = std::chrono::milliseconds(1000 / sub.throttle_rate.value());
                    if (elapsed < min_interval) continue;
                }
                sub.last_sent = std::chrono::steady_clock::now();

                notify_topic_message(sub_id, topic,
                    std::to_string(ns), msg_json);
            }
        }
        stream.buffer.clear();
    } else {
        stream.buffer += line_str;
        if (!line_str.empty() && line_str.back() != '\n') {
            stream.buffer += '\n';
        }
    }
}

auto LocalRosStreamManager::unsubscribe_topic(const std::string& subscription_id)
    -> std::expected<void, errors::ErrorCode> {
    std::unique_ptr<subprocess::StreamingHandle> handle_to_stop;

    {
        std::lock_guard lock(topics_mutex_);

        auto sub_it = subscriptions_.find(subscription_id);
        if (sub_it == subscriptions_.end()) {
            return std::unexpected(errors::ErrorCode::SUBSCRIPTION_NOT_FOUND);
        }

        std::string topic = sub_it->second.topic;
        subscriptions_.erase(sub_it);

        auto stream_it = topic_streams_.find(topic);
        if (stream_it != topic_streams_.end()) {
            auto& ids = stream_it->second.subscription_ids;
            ids.erase(std::remove(ids.begin(), ids.end(), subscription_id), ids.end());
            if (ids.empty()) {
                handle_to_stop = std::move(stream_it->second.handle);
                topic_streams_.erase(stream_it);
            }
        }
    }

    if (handle_to_stop) {
        subprocess::SubprocessExecutor::stop_streaming(std::move(handle_to_stop));
    }

    return {};
}

void LocalRosStreamManager::stop_topic_stream(const std::string& topic) {
    std::unique_ptr<subprocess::StreamingHandle> handle;

    {
        std::lock_guard lock(topics_mutex_);
        auto it = topic_streams_.find(topic);
        if (it == topic_streams_.end()) return;

        for (const auto& sub_id : it->second.subscription_ids) {
            subscriptions_.erase(sub_id);
        }
        handle = std::move(it->second.handle);
        topic_streams_.erase(it);
    }

    if (handle) {
        subprocess::SubprocessExecutor::stop_streaming(std::move(handle));
    }
}

// --- Publish topic ---

auto LocalRosStreamManager::publish_topic(
    const std::string& topic,
    const std::string& type,
    const nlohmann::json& message)
    -> std::expected<void, errors::ErrorCode> {
    auto result = executor_.execute(
        wrap_ros_command({"ros2", "topic", "pub", topic, type, message.dump(), "--once"}),
        10000);
    if (!result.has_value()) {
        return std::unexpected(result.error());
    }
    if (result->exit_code != 0) {
        return std::unexpected(errors::ErrorCode::ROS_INVALID_MESSAGE);
    }
    return {};
}

// --- Call service ---

auto LocalRosStreamManager::call_service(
    const std::string& call_id,
    const std::string& service,
    const std::string& type,
    const nlohmann::json& request,
    const std::optional<int>& timeout)
    -> std::expected<void, errors::ErrorCode> {
    {
        std::lock_guard lock(services_mutex_);
        if (active_service_calls_.count(call_id)) {
            return std::unexpected(errors::ErrorCode::INVALID_PAYLOAD);
        }
        active_service_calls_.insert(call_id);
    }

    int timeout_ms = timeout.value_or(5000);
    auto* self = this;
    std::string call_id_copy = call_id;

    // Clean up completed service threads
    cleanup_service_threads();

    // Run in a tracked background thread so we don't block the WS handler
    auto tracked = std::make_unique<TrackedThread>();
    auto* raw = tracked.get();
    raw->thread = std::thread([self, call_id_copy, service, type, request, timeout_ms, raw]() {
        auto result = self->executor_.execute(
            self->wrap_ros_command({"ros2", "service", "call", service, type, request.dump()}),
            timeout_ms);

        if (!result.has_value()) {
            self->notify_service_result(call_id_copy, false,
                std::nullopt, "Service call failed: timeout");
        } else if (result->exit_code != 0) {
            self->notify_service_result(call_id_copy, false,
                std::nullopt, result->stderr_output);
        } else {
            auto resp_json = yaml_block_to_json(result->stdout_output);
            self->notify_service_result(call_id_copy, true, resp_json, std::nullopt);
        }

        std::lock_guard lock(self->services_mutex_);
        self->active_service_calls_.erase(call_id_copy);
        raw->completed.store(true);
    });
    {
        std::lock_guard lock(service_threads_mutex_);
        service_threads_.push_back(std::move(tracked));
    }

    return {};
}

// --- Call/cancel action ---

auto LocalRosStreamManager::call_action(
    const std::string& call_id,
    const std::string& action,
    const std::string& type,
    const nlohmann::json& goal,
    const std::optional<int>& /*timeout*/)
    -> std::expected<void, errors::ErrorCode> {
    std::lock_guard lock(actions_mutex_);

    if (action_calls_.count(call_id)) {
        return std::unexpected(errors::ErrorCode::INVALID_PAYLOAD);
    }

    ActionCall ac;
    ac.call_id = call_id;
    ac.action = action;

    auto* self = this;
    std::string call_id_copy = call_id;

    subprocess::StreamCallbacks callbacks;
    callbacks.on_stdout = [self, call_id_copy](std::string_view line) {
        self->handle_action_stdout(call_id_copy, line);
    };
    callbacks.on_stderr = [](std::string_view) {};
    callbacks.on_exit = [self, call_id_copy](int exit_code) {
        std::lock_guard lock(self->actions_mutex_);
        auto it = self->action_calls_.find(call_id_copy);
        if (it != self->action_calls_.end() && !it->second.got_result) {
            self->notify_action_result(call_id_copy,
                exit_code == 0 ? "succeeded" : "aborted", std::nullopt);
            self->action_calls_.erase(it);
        }
    };

    auto handle = executor_.start_streaming(
        wrap_ros_command({"ros2", "action", "send_goal", action, type, goal.dump(), "--feedback"}),
        callbacks);
    if (!handle.has_value()) {
        return std::unexpected(handle.error());
    }

    ac.handle = std::move(handle.value());
    action_calls_.emplace(call_id, std::move(ac));
    return {};
}

void LocalRosStreamManager::handle_action_stdout(const std::string& call_id,
                                                   std::string_view line) {
    std::lock_guard lock(actions_mutex_);

    auto it = action_calls_.find(call_id);
    if (it == action_calls_.end()) return;

    auto& ac = it->second;
    std::string line_str(line);

    // ros2 action send_goal --feedback outputs:
    //   feedback lines with "[feedback]" marker, or just YAML blocks
    //   result with "[result]" marker or on process exit
    // We accumulate lines between "---" and detect feedback vs result
    if (line_str.find("feedback") != std::string::npos &&
        line_str.find(':') != std::string::npos &&
        line_str.find("---") == std::string::npos) {
        // Simple heuristic: lines containing "feedback" are feedback
        // More robust: accumulate full blocks between "---"
    }

    if (line_str == "---" || line_str == "---\n") {
        if (!ac.buffer.empty()) {
            auto fb_json = yaml_block_to_json(ac.buffer);
            notify_action_feedback(call_id, fb_json);
        }
        ac.buffer.clear();
    } else {
        ac.buffer += line_str;
        if (!line_str.empty() && line_str.back() != '\n') {
            ac.buffer += '\n';
        }
    }
}

auto LocalRosStreamManager::cancel_action(const std::string& call_id)
    -> std::expected<void, errors::ErrorCode> {
    std::unique_ptr<subprocess::StreamingHandle> handle;

    {
        std::lock_guard lock(actions_mutex_);
        auto it = action_calls_.find(call_id);
        if (it == action_calls_.end()) {
            return std::unexpected(errors::ErrorCode::ACTION_NOT_FOUND);
        }

        notify_action_result(call_id, "cancelled", std::nullopt);
        handle = std::move(it->second.handle);
        action_calls_.erase(it);
    }

    if (handle) {
        subprocess::SubprocessExecutor::stop_streaming(std::move(handle));
    }
    return {};
}

// --- Bag recording ---

auto LocalRosStreamManager::start_bag(
    const std::string& bag_id,
    const std::optional<std::vector<std::string>>& topics,
    const std::string& path,
    const std::optional<std::string>& format)
    -> std::expected<void, errors::ErrorCode> {
    std::lock_guard lock(bags_mutex_);

    if (bag_recordings_.count(bag_id)) {
        return std::unexpected(errors::ErrorCode::INVALID_PAYLOAD);
    }

    std::vector<std::string> cmd = {"ros2", "bag", "record", "-o", path};
    if (format.has_value()) {
        cmd.push_back("--storage");
        cmd.push_back(format.value());
    }
    if (topics.has_value() && !topics->empty()) {
        for (const auto& t : topics.value()) {
            cmd.push_back(t);
        }
    }

    BagRecording rec;
    rec.bag_id = bag_id;
    rec.path = path;
    rec.start_time = std::chrono::steady_clock::now();

    auto* self = this;
    std::string bag_id_copy = bag_id;

    subprocess::StreamCallbacks callbacks;
    callbacks.on_stdout = [](std::string_view) {};
    callbacks.on_stderr = [](std::string_view) {};
    callbacks.on_exit = [self, bag_id_copy](int) {
        self->notify_bag_status(bag_id_copy, "stopped",
            std::nullopt, std::nullopt, std::nullopt);
    };

    auto handle = executor_.start_streaming(wrap_ros_command(cmd), callbacks);
    if (!handle.has_value()) {
        return std::unexpected(errors::ErrorCode::BAG_WRITE_ERROR);
    }

    rec.handle = std::move(handle.value());
    bag_recordings_.emplace(bag_id, std::move(rec));

    notify_bag_status(bag_id, "recording",
        0.0, 0, 0.0);
    return {};
}

auto LocalRosStreamManager::stop_bag(const std::string& bag_id)
    -> std::expected<void, errors::ErrorCode> {
    std::unique_ptr<subprocess::StreamingHandle> handle;
    double duration = 0.0;

    {
        std::lock_guard lock(bags_mutex_);
        auto it = bag_recordings_.find(bag_id);
        if (it == bag_recordings_.end()) {
            return std::unexpected(errors::ErrorCode::BAG_NOT_RECORDING);
        }

        auto elapsed = std::chrono::steady_clock::now() - it->second.start_time;
        duration = std::chrono::duration<double>(elapsed).count();
        handle = std::move(it->second.handle);
        bag_recordings_.erase(it);
    }

    if (handle) {
        subprocess::SubprocessExecutor::stop_streaming(std::move(handle));
    }

    notify_bag_status(bag_id, "stopped",
        duration, std::nullopt, std::nullopt);
    return {};
}

// --- Node monitor ---

void LocalRosStreamManager::start_node_monitor() {
    if (node_monitor_running_.exchange(true)) return;

    node_poll_thread_ = std::thread(&LocalRosStreamManager::node_poll_loop, this);
}

void LocalRosStreamManager::stop_node_monitor() {
    node_monitor_running_ = false;
    if (node_poll_thread_.joinable()) {
        node_poll_thread_.join();
    }
}

void LocalRosStreamManager::node_poll_loop() {
    while (node_monitor_running_ && !shutting_down_) {
        auto result = executor_.execute(wrap_ros_command({"ros2", "node", "list"}), 5000);

        if (result.has_value() && result->exit_code == 0) {
            auto lines = split_lines(result->stdout_output);
            std::unordered_set<std::string> current_nodes;
            for (auto& line : lines) {
                auto trimmed = trim_str(line);
                if (!trimmed.empty()) {
                    current_nodes.insert(trimmed);
                }
            }

            std::lock_guard lock(node_mutex_);
            // New nodes
            for (const auto& node : current_nodes) {
                if (!last_known_nodes_.count(node)) {
                    notify_node_event("started", node);
                }
            }
            // Removed nodes
            for (const auto& node : last_known_nodes_) {
                if (!current_nodes.count(node)) {
                    notify_node_event("stopped", node);
                }
            }
            last_known_nodes_ = std::move(current_nodes);
        }

        // Poll every 2 seconds
        for (int i = 0; i < 20 && node_monitor_running_ && !shutting_down_; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

// --- Listener management ---

auto LocalRosStreamManager::add_listener(
    std::shared_ptr<IRosStreamListener> listener) -> void {
    std::lock_guard lock(listeners_mutex_);
    listeners_.push_back(std::move(listener));
}

auto LocalRosStreamManager::remove_listener(
    std::shared_ptr<IRosStreamListener> listener) -> void {
    std::lock_guard lock(listeners_mutex_);
    listeners_.erase(
        std::remove(listeners_.begin(), listeners_.end(), listener),
        listeners_.end());
}

// --- Shutdown ---

void LocalRosStreamManager::cleanup_service_threads() {
    std::lock_guard lock(service_threads_mutex_);
    for (auto it = service_threads_.begin(); it != service_threads_.end(); ) {
        if ((*it)->completed.load()) {
            if ((*it)->thread.joinable()) (*it)->thread.join();
            it = service_threads_.erase(it);
        } else {
            ++it;
        }
    }
}

auto LocalRosStreamManager::shutdown() -> void {
    if (shutting_down_.exchange(true)) return;

    // Join all service call threads
    {
        std::lock_guard lock(service_threads_mutex_);
        for (auto& t : service_threads_) {
            if (t->thread.joinable()) t->thread.join();
        }
        service_threads_.clear();
    }

    stop_node_monitor();

    {
        std::vector<std::unique_ptr<subprocess::StreamingHandle>> handles;
        {
            std::lock_guard lock(topics_mutex_);
            for (auto& [topic, stream] : topic_streams_) {
                handles.push_back(std::move(stream.handle));
            }
            topic_streams_.clear();
            subscriptions_.clear();
        }
        for (auto& h : handles) {
            if (h) subprocess::SubprocessExecutor::stop_streaming(std::move(h));
        }
    }

    {
        std::vector<std::unique_ptr<subprocess::StreamingHandle>> handles;
        {
            std::lock_guard lock(actions_mutex_);
            for (auto& [id, ac] : action_calls_) {
                handles.push_back(std::move(ac.handle));
            }
            action_calls_.clear();
        }
        for (auto& h : handles) {
            if (h) subprocess::SubprocessExecutor::stop_streaming(std::move(h));
        }
    }

    {
        std::vector<std::unique_ptr<subprocess::StreamingHandle>> handles;
        {
            std::lock_guard lock(bags_mutex_);
            for (auto& [id, rec] : bag_recordings_) {
                handles.push_back(std::move(rec.handle));
            }
            bag_recordings_.clear();
        }
        for (auto& h : handles) {
            if (h) subprocess::SubprocessExecutor::stop_streaming(std::move(h));
        }
    }
}

// --- Notification helpers ---

void LocalRosStreamManager::notify_topic_message(
    const std::string& subscription_id,
    const std::string& topic,
    const std::string& timestamp,
    const nlohmann::json& message) {
    std::vector<std::shared_ptr<IRosStreamListener>> copy;
    {
        std::lock_guard lock(listeners_mutex_);
        copy = listeners_;
    }
    for (auto& l : copy) {
        l->on_topic_message(subscription_id, topic, timestamp, message);
    }
}

void LocalRosStreamManager::notify_service_result(
    const std::string& call_id,
    bool success,
    const std::optional<nlohmann::json>& result,
    const std::optional<std::string>& error) {
    std::vector<std::shared_ptr<IRosStreamListener>> copy;
    {
        std::lock_guard lock(listeners_mutex_);
        copy = listeners_;
    }
    for (auto& l : copy) {
        l->on_service_result(call_id, success, result, error);
    }
}

void LocalRosStreamManager::notify_action_feedback(
    const std::string& call_id,
    const nlohmann::json& feedback) {
    std::vector<std::shared_ptr<IRosStreamListener>> copy;
    {
        std::lock_guard lock(listeners_mutex_);
        copy = listeners_;
    }
    for (auto& l : copy) {
        l->on_action_feedback(call_id, feedback);
    }
}

void LocalRosStreamManager::notify_action_result(
    const std::string& call_id,
    const std::string& status,
    const std::optional<nlohmann::json>& result) {
    std::vector<std::shared_ptr<IRosStreamListener>> copy;
    {
        std::lock_guard lock(listeners_mutex_);
        copy = listeners_;
    }
    for (auto& l : copy) {
        l->on_action_result(call_id, status, result);
    }
}

void LocalRosStreamManager::notify_node_event(
    const std::string& event,
    const std::string& node) {
    std::vector<std::shared_ptr<IRosStreamListener>> copy;
    {
        std::lock_guard lock(listeners_mutex_);
        copy = listeners_;
    }
    for (auto& l : copy) {
        l->on_node_event(event, node);
    }
}

void LocalRosStreamManager::notify_bag_status(
    const std::string& bag_id,
    const std::string& status,
    const std::optional<double>& duration,
    const std::optional<int>& message_count,
    const std::optional<double>& size_bytes) {
    std::vector<std::shared_ptr<IRosStreamListener>> copy;
    {
        std::lock_guard lock(listeners_mutex_);
        copy = listeners_;
    }
    for (auto& l : copy) {
        l->on_bag_status(bag_id, status, duration, message_count, size_bytes);
    }
}

// --- YAML parsing helpers ---

auto LocalRosStreamManager::yaml_block_to_json(const std::string& block)
    -> nlohmann::json {
    // Try JSON first
    try {
        return nlohmann::json::parse(block);
    } catch (...) {}

    // Parse as YAML
    auto lines = split_lines(block);
    if (lines.empty()) return nlohmann::json::object();

    auto [result, _] = yaml_lines_to_json(lines, 0, 0);
    return result;
}

auto LocalRosStreamManager::yaml_lines_to_json(
    const std::vector<std::string>& lines,
    std::size_t start,
    int indent) -> std::pair<nlohmann::json, std::size_t> {
    nlohmann::json obj = nlohmann::json::object();
    std::size_t i = start;

    while (i < lines.size()) {
        const auto& line = lines[i];
        if (line.empty()) { ++i; continue; }

        int current_indent = count_indent(line);
        if (current_indent < indent && i > start) break;

        // Find key: value
        auto content = trim_str(line);
        auto colon_pos = content.find(':');
        if (colon_pos == std::string::npos) { ++i; continue; }

        std::string key = content.substr(0, colon_pos);
        std::string value_str = trim_str(content.substr(colon_pos + 1));

        if (value_str.empty()) {
            // Nested map
            auto [child, next_i] = yaml_lines_to_json(lines, i + 1, current_indent + 2);
            obj[key] = child;
            i = next_i;
        } else {
            obj[key] = yaml_value_to_json(value_str);
            ++i;
        }
    }

    return {obj, i};
}

auto LocalRosStreamManager::yaml_value_to_json(const std::string& s) -> nlohmann::json {
    // Try JSON parse first
    try {
        return nlohmann::json::parse(s);
    } catch (...) {}

    // Boolean
    if (s == "true") return true;
    if (s == "false") return false;

    // Null
    if (s == "null" || s == "~") return nullptr;

    // Integer
    try {
        std::size_t pos;
        auto val = std::stoll(s, &pos);
        if (pos == s.size()) return val;
    } catch (...) {}

    // Float
    try {
        std::size_t pos;
        auto val = std::stod(s, &pos);
        if (pos == s.size()) return val;
    } catch (...) {}

    // Quoted string — strip quotes
    if (s.size() >= 2) {
        char first = s.front();
        char last = s.back();
        if ((first == '\'' && last == '\'') ||
            (first == '"' && last == '"')) {
            return s.substr(1, s.size() - 2);
        }
    }

    // Plain string
    return s;
}

auto LocalRosStreamManager::count_indent(const std::string& line) -> int {
    int count = 0;
    for (char c : line) {
        if (c == ' ') count++;
        else if (c == '\t') count += 2;
        else break;
    }
    return count;
}

auto LocalRosStreamManager::trim_str(const std::string& s) -> std::string {
    auto start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

auto LocalRosStreamManager::split_lines(const std::string& s) -> std::vector<std::string> {
    std::vector<std::string> lines;
    std::istringstream stream(s);
    std::string line;
    while (std::getline(stream, line)) {
        lines.push_back(line);
    }
    return lines;
}

}  // namespace rosweb::ros
