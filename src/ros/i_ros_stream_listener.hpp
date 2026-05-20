#pragma once

#include <nlohmann/json.hpp>
#include <optional>
#include <string>

namespace rosweb::ros {

class IRosStreamListener {
public:
    virtual ~IRosStreamListener() = default;

    virtual void on_topic_message(
        const std::string& subscription_id,
        const std::string& topic,
        const std::string& timestamp,
        const nlohmann::json& message) = 0;

    virtual void on_service_result(
        const std::string& call_id,
        bool success,
        const std::optional<nlohmann::json>& result,
        const std::optional<std::string>& error) = 0;

    virtual void on_action_feedback(
        const std::string& call_id,
        const nlohmann::json& feedback) = 0;

    virtual void on_action_result(
        const std::string& call_id,
        const std::string& status,
        const std::optional<nlohmann::json>& result) = 0;

    virtual void on_node_event(
        const std::string& event,
        const std::string& node) = 0;

    virtual void on_bag_status(
        const std::string& bag_id,
        const std::string& status,
        const std::optional<double>& duration,
        const std::optional<int>& message_count,
        const std::optional<double>& size_bytes) = 0;
};

}  // namespace rosweb::ros
