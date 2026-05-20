#include "models/ros_stream_models.hpp"

namespace rosweb::models {

// --- from_json (request models) ---

void from_json(const nlohmann::json& j, TopicSubscribeRequest& v) {
    v.subscription_id = j.at("subscriptionId").get<std::string>();
    v.topic = j.at("topic").get<std::string>();
    if (j.contains("type") && !j["type"].is_null()) {
        v.type = j["type"].get<std::string>();
    }
    if (j.contains("throttleRate") && !j["throttleRate"].is_null()) {
        v.throttle_rate = j["throttleRate"].get<int>();
    }
    if (j.contains("queueLength") && !j["queueLength"].is_null()) {
        v.queue_length = j["queueLength"].get<int>();
    }
}

void from_json(const nlohmann::json& j, TopicUnsubscribeRequest& v) {
    v.subscription_id = j.at("subscriptionId").get<std::string>();
}

void from_json(const nlohmann::json& j, TopicPublishRequest& v) {
    v.topic = j.at("topic").get<std::string>();
    v.type = j.at("type").get<std::string>();
    v.message = j.at("message");
}

void from_json(const nlohmann::json& j, ServiceCallRequest& v) {
    v.call_id = j.at("callId").get<std::string>();
    v.service = j.at("service").get<std::string>();
    v.type = j.at("type").get<std::string>();
    v.request = j.at("request");
    if (j.contains("timeout") && !j["timeout"].is_null()) {
        v.timeout = j["timeout"].get<int>();
    }
}

void from_json(const nlohmann::json& j, ActionCallRequest& v) {
    v.call_id = j.at("callId").get<std::string>();
    v.action = j.at("action").get<std::string>();
    v.type = j.at("type").get<std::string>();
    v.goal = j.at("goal");
    if (j.contains("timeout") && !j["timeout"].is_null()) {
        v.timeout = j["timeout"].get<int>();
    }
}

void from_json(const nlohmann::json& j, CancelActionRequest& v) {
    v.call_id = j.at("callId").get<std::string>();
}

void from_json(const nlohmann::json& j, StartBagRequest& v) {
    v.bag_id = j.at("bagId").get<std::string>();
    if (j.contains("topics") && !j["topics"].is_null()) {
        v.topics = j["topics"].get<std::vector<std::string>>();
    }
    v.path = j.at("path").get<std::string>();
    if (j.contains("format") && !j["format"].is_null()) {
        v.format = j["format"].get<std::string>();
    }
}

void from_json(const nlohmann::json& j, StopBagRequest& v) {
    v.bag_id = j.at("bagId").get<std::string>();
}

// --- to_json (response/push models) ---

void to_json(nlohmann::json& j, const TopicSubscribedPayload& v) {
    j = nlohmann::json{
        {"subscriptionId", v.subscription_id},
        {"topic", v.topic},
    };
}

void to_json(nlohmann::json& j, const TopicMessagePayload& v) {
    j = nlohmann::json{
        {"subscriptionId", v.subscription_id},
        {"topic", v.topic},
        {"timestamp", v.timestamp},
        {"message", v.message},
    };
}

void to_json(nlohmann::json& j, const ServiceResultPayload& v) {
    j = nlohmann::json{
        {"callId", v.call_id},
        {"success", v.success},
    };
    if (v.result.has_value()) {
        j["result"] = v.result.value();
    }
    if (v.error.has_value()) {
        j["error"] = v.error.value();
    }
}

void to_json(nlohmann::json& j, const ActionFeedbackPayload& v) {
    j = nlohmann::json{
        {"callId", v.call_id},
        {"feedback", v.feedback},
    };
}

void to_json(nlohmann::json& j, const ActionResultPayload& v) {
    j = nlohmann::json{
        {"callId", v.call_id},
        {"status", v.status},
    };
    if (v.result.has_value()) {
        j["result"] = v.result.value();
    }
}

void to_json(nlohmann::json& j, const NodeEventPayload& v) {
    j = nlohmann::json{
        {"event", v.event},
        {"node", v.node},
    };
}

void to_json(nlohmann::json& j, const BagStatusPayload& v) {
    j = nlohmann::json{
        {"bagId", v.bag_id},
        {"status", v.status},
    };
    if (v.duration.has_value()) {
        j["duration"] = v.duration.value();
    }
    if (v.message_count.has_value()) {
        j["messageCount"] = v.message_count.value();
    }
    if (v.size_bytes.has_value()) {
        j["sizeBytes"] = v.size_bytes.value();
    }
}

}  // namespace rosweb::models
