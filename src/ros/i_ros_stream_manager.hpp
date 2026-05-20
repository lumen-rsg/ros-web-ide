#pragma once

#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>
#include "errors/error_codes.hpp"
#include "ros/i_ros_stream_listener.hpp"

namespace rosweb::ros {

class IRosStreamManager {
public:
    virtual ~IRosStreamManager() = default;

    virtual auto subscribe_topic(
        const std::string& subscription_id,
        const std::string& topic,
        const std::optional<std::string>& type,
        const std::optional<int>& throttle_rate,
        const std::optional<int>& queue_length)
        -> std::expected<void, errors::ErrorCode> = 0;

    virtual auto unsubscribe_topic(const std::string& subscription_id)
        -> std::expected<void, errors::ErrorCode> = 0;

    virtual auto publish_topic(
        const std::string& topic,
        const std::string& type,
        const nlohmann::json& message)
        -> std::expected<void, errors::ErrorCode> = 0;

    virtual auto call_service(
        const std::string& call_id,
        const std::string& service,
        const std::string& type,
        const nlohmann::json& request,
        const std::optional<int>& timeout)
        -> std::expected<void, errors::ErrorCode> = 0;

    virtual auto call_action(
        const std::string& call_id,
        const std::string& action,
        const std::string& type,
        const nlohmann::json& goal,
        const std::optional<int>& timeout)
        -> std::expected<void, errors::ErrorCode> = 0;

    virtual auto cancel_action(const std::string& call_id)
        -> std::expected<void, errors::ErrorCode> = 0;

    virtual auto start_bag(
        const std::string& bag_id,
        const std::optional<std::vector<std::string>>& topics,
        const std::string& path,
        const std::optional<std::string>& format)
        -> std::expected<void, errors::ErrorCode> = 0;

    virtual auto stop_bag(const std::string& bag_id)
        -> std::expected<void, errors::ErrorCode> = 0;

    virtual auto start_node_monitor() -> void = 0;
    virtual auto stop_node_monitor() -> void = 0;

    virtual auto add_listener(std::shared_ptr<IRosStreamListener> listener) -> void = 0;
    virtual auto remove_listener(std::shared_ptr<IRosStreamListener> listener) -> void = 0;

    virtual auto shutdown() -> void = 0;
};

}  // namespace rosweb::ros
