#include "tf/local_tf_manager.hpp"

#include <algorithm>
#include <chrono>
#include <sstream>

namespace rosweb::tf {

LocalTfManager::~LocalTfManager() {
    shutdown();
}

auto LocalTfManager::subscribe_tf(
    const std::string& subscription_id,
    const std::optional<std::vector<std::string>>& frames,
    const std::optional<int>& throttle_rate)
    -> std::expected<void, errors::ErrorCode> {
    std::lock_guard lock(subscriptions_mutex_);

    if (subscriptions_.count(subscription_id)) {
        return std::unexpected(errors::ErrorCode::INVALID_PAYLOAD);
    }

    TfSubscription sub{
        .subscription_id = subscription_id,
        .frames = frames,
        .throttle_rate = throttle_rate,
        .last_sent = {},
    };
    subscriptions_.emplace(subscription_id, std::move(sub));

    // Start the /tf stream if this is the first subscription
    if (!tf_stream_handle_) {
        start_tf_stream();
    }

    return {};
}

auto LocalTfManager::unsubscribe_tf(const std::string& subscription_id)
    -> std::expected<void, errors::ErrorCode> {
    std::lock_guard lock(subscriptions_mutex_);

    if (!subscriptions_.count(subscription_id)) {
        return std::unexpected(errors::ErrorCode::SUBSCRIPTION_NOT_FOUND);
    }

    subscriptions_.erase(subscription_id);

    // Stop the stream if no more subscriptions
    if (subscriptions_.empty() && tf_stream_handle_) {
        stop_tf_stream();
    }

    return {};
}

auto LocalTfManager::get_tf_tree()
    -> std::expected<models::TfTreePayload, errors::ErrorCode> {
    // Run ros2 topic echo /tf_static --once to get static transforms
    auto result = executor_.execute(
        {"ros2", "topic", "echo", "/tf_static", "--once"}, 10000);
    if (!result.has_value()) {
        return std::unexpected(result.error());
    }
    if (result->exit_code != 0) {
        return std::unexpected(errors::ErrorCode::ROS_SERVICE_UNAVAILABLE);
    }

    // Parse the output to build the tree
    auto transforms_json = yaml_block_to_json(result->stdout_output);

    // Build parent → children map
    std::unordered_map<std::string, std::string> child_to_parent;
    // The output may be a single transform or an array of transforms
    // ros2 topic echo /tf_static outputs one message which may contain multiple transforms
    // under a "transforms" array in YAML

    // Try to extract parent-child from the parsed structure
    // The typical structure is: transforms: [{header: {frame_id: X}, child_frame_id: Y, ...}]
    if (transforms_json.contains("transforms") && transforms_json["transforms"].is_array()) {
        for (const auto& tf : transforms_json["transforms"]) {
            std::string parent;
            std::string child;
            if (tf.contains("header") && tf["header"].contains("frame_id")) {
                parent = tf["header"]["frame_id"].get<std::string>();
            }
            if (tf.contains("child_frame_id")) {
                child = tf["child_frame_id"].get<std::string>();
            }
            if (!parent.empty() && !child.empty()) {
                child_to_parent[child] = parent;
            }
        }
    }

    // Build tree frames
    models::TfTreePayload tree;
    std::unordered_set<std::string> all_frames;
    std::unordered_map<std::string, std::vector<std::string>> parent_to_children;

    for (const auto& [child, parent] : child_to_parent) {
        all_frames.insert(child);
        all_frames.insert(parent);
        parent_to_children[parent].push_back(child);
    }

    for (const auto& frame : all_frames) {
        models::TfFrame f;
        f.name = frame;
        auto pit = child_to_parent.find(frame);
        if (pit != child_to_parent.end()) {
            f.parent = pit->second;
        }
        auto cit = parent_to_children.find(frame);
        if (cit != parent_to_children.end()) {
            f.children = cit->second;
        }
        tree.frames.push_back(std::move(f));
    }

    return tree;
}

void LocalTfManager::start_tf_stream() {
    auto* self = this;

    subprocess::StreamCallbacks callbacks;
    callbacks.on_stdout = [self](std::string_view line) {
        self->handle_tf_stdout(line);
    };
    callbacks.on_stderr = [](std::string_view) {};
    callbacks.on_exit = [self](int) {
        std::lock_guard lock(self->subscriptions_mutex_);
        if (self->tf_stream_handle_) {
            self->tf_stream_handle_.reset();
        }
    };

    auto handle = executor_.start_streaming(
        {"ros2", "topic", "echo", "/tf"}, callbacks);
    if (handle.has_value()) {
        tf_stream_handle_ = std::move(handle.value());
    }
}

void LocalTfManager::stop_tf_stream() {
    if (tf_stream_handle_) {
        subprocess::SubprocessExecutor::stop_streaming(
            std::move(tf_stream_handle_));
        tf_stream_handle_.reset();
    }
}

void LocalTfManager::handle_tf_stdout(std::string_view line) {
    std::lock_guard lock(subscriptions_mutex_);

    std::string line_str(line);
    if (line_str == "---" || line_str == "---\n") {
        if (tf_buffer_.empty()) return;

        auto msg_json = yaml_block_to_json(tf_buffer_);
        tf_buffer_.clear();

        // Parse transforms from the message
        // Structure: {transforms: [{header: {frame_id, stamp}, child_frame_id, transform: {translation, rotation}}]}
        std::vector<models::TfTransform> transforms;

        auto extract_transforms = [&](const nlohmann::json& j) {
            if (!j.contains("transforms") || !j["transforms"].is_array()) return;
            for (const auto& tf : j["transforms"]) {
                models::TfTransform t;
                if (tf.contains("header")) {
                    auto& hdr = tf["header"];
                    if (hdr.contains("frame_id")) {
                        t.parent = hdr["frame_id"].get<std::string>();
                    }
                    if (hdr.contains("stamp")) {
                        auto& stamp = hdr["stamp"];
                        if (stamp.contains("sec") && stamp.contains("nanosec")) {
                            auto ns = stamp["sec"].get<long long>() * 1000000000LL +
                                      stamp["nanosec"].get<long long>();
                            t.timestamp = std::to_string(ns);
                        }
                    }
                }
                if (tf.contains("child_frame_id")) {
                    t.child = tf["child_frame_id"].get<std::string>();
                }
                if (tf.contains("transform")) {
                    auto& tr = tf["transform"];
                    if (tr.contains("translation")) {
                        auto& tl = tr["translation"];
                        t.translation.x = tl.value("x", 0.0);
                        t.translation.y = tl.value("y", 0.0);
                        t.translation.z = tl.value("z", 0.0);
                    }
                    if (tr.contains("rotation")) {
                        auto& rot = tr["rotation"];
                        t.rotation.x = rot.value("x", 0.0);
                        t.rotation.y = rot.value("y", 0.0);
                        t.rotation.z = rot.value("z", 0.0);
                        t.rotation.w = rot.value("w", 1.0);
                    }
                }
                if (!t.parent.empty() && !t.child.empty()) {
                    transforms.push_back(std::move(t));
                }
            }
        };

        extract_transforms(msg_json);
        if (transforms.empty()) return;

        // Deliver to each subscription with optional frame filtering and throttle
        for (auto& [id, sub] : subscriptions_) {
            // Apply frame filter
            if (sub.frames.has_value() && !sub.frames->empty()) {
                bool has_matching = false;
                for (const auto& t : transforms) {
                    for (const auto& f : sub.frames.value()) {
                        if (t.parent == f || t.child == f) {
                            has_matching = true;
                            break;
                        }
                    }
                    if (has_matching) break;
                }
                if (!has_matching) continue;
            }

            // Apply throttle
            if (sub.throttle_rate.has_value() && sub.throttle_rate.value() > 0) {
                auto elapsed = std::chrono::steady_clock::now() - sub.last_sent;
                auto min_interval = std::chrono::milliseconds(1000 / sub.throttle_rate.value());
                if (elapsed < min_interval) continue;
            }
            sub.last_sent = std::chrono::steady_clock::now();

            // Filter transforms for this subscription
            std::vector<models::TfTransform> filtered;
            if (sub.frames.has_value() && !sub.frames->empty()) {
                for (const auto& t : transforms) {
                    for (const auto& f : sub.frames.value()) {
                        if (t.parent == f || t.child == f) {
                            filtered.push_back(t);
                            break;
                        }
                    }
                }
                notify_tf_update(id, filtered);
            } else {
                notify_tf_update(id, transforms);
            }
        }
    } else {
        tf_buffer_ += line_str;
        if (!line_str.empty() && line_str.back() != '\n') {
            tf_buffer_ += '\n';
        }
    }
}

auto LocalTfManager::add_listener(std::shared_ptr<ITfListener> listener) -> void {
    std::lock_guard lock(listeners_mutex_);
    listeners_.push_back(std::move(listener));
}

auto LocalTfManager::remove_listener(std::shared_ptr<ITfListener> listener) -> void {
    std::lock_guard lock(listeners_mutex_);
    listeners_.erase(
        std::remove(listeners_.begin(), listeners_.end(), listener),
        listeners_.end());
}

auto LocalTfManager::shutdown() -> void {
    if (shutting_down_.exchange(true)) return;

    std::lock_guard lock(subscriptions_mutex_);
    stop_tf_stream();
    subscriptions_.clear();
}

void LocalTfManager::notify_tf_update(
    const std::string& subscription_id,
    const std::vector<models::TfTransform>& transforms) {
    std::vector<std::shared_ptr<ITfListener>> copy;
    {
        std::lock_guard lock(listeners_mutex_);
        copy = listeners_;
    }
    for (auto& l : copy) {
        l->on_tf_update(subscription_id, transforms);
    }
}

// --- YAML parsing (same approach as LocalRosStreamManager) ---

auto LocalTfManager::yaml_block_to_json(const std::string& block) -> nlohmann::json {
    try {
        return nlohmann::json::parse(block);
    } catch (...) {}

    auto lines = split_lines(block);
    if (lines.empty()) return nlohmann::json::object();

    auto [result, _] = yaml_lines_to_json(lines, 0, 0);
    return result;
}

auto LocalTfManager::yaml_lines_to_json(
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

        auto content = trim_str(line);
        auto colon_pos = content.find(':');
        if (colon_pos == std::string::npos) { ++i; continue; }

        std::string key = content.substr(0, colon_pos);
        std::string value_str = trim_str(content.substr(colon_pos + 1));

        if (value_str.empty()) {
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

auto LocalTfManager::yaml_value_to_json(const std::string& s) -> nlohmann::json {
    try {
        return nlohmann::json::parse(s);
    } catch (...) {}

    if (s == "true") return true;
    if (s == "false") return false;
    if (s == "null" || s == "~") return nullptr;

    try {
        std::size_t pos;
        auto val = std::stoll(s, &pos);
        if (pos == s.size()) return val;
    } catch (...) {}

    try {
        std::size_t pos;
        auto val = std::stod(s, &pos);
        if (pos == s.size()) return val;
    } catch (...) {}

    if (s.size() >= 2) {
        char first = s.front();
        char last = s.back();
        if ((first == '\'' && last == '\'') ||
            (first == '"' && last == '"')) {
            return s.substr(1, s.size() - 2);
        }
    }

    return s;
}

auto LocalTfManager::count_indent(const std::string& line) -> int {
    int count = 0;
    for (char c : line) {
        if (c == ' ') count++;
        else if (c == '\t') count += 2;
        else break;
    }
    return count;
}

auto LocalTfManager::trim_str(const std::string& s) -> std::string {
    auto start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

auto LocalTfManager::split_lines(const std::string& s) -> std::vector<std::string> {
    std::vector<std::string> lines;
    std::istringstream stream(s);
    std::string line;
    while (std::getline(stream, line)) {
        lines.push_back(line);
    }
    return lines;
}

}  // namespace rosweb::tf
