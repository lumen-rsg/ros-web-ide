#pragma once

#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

namespace rosweb::models {

// --- Request models (client → server, need from_json) ---

struct TopicSubscribeRequest {
    std::string subscription_id;
    std::string topic;
    std::optional<std::string> type;
    std::optional<int> throttle_rate;
    std::optional<int> queue_length;
};

struct TopicUnsubscribeRequest {
    std::string subscription_id;
};

struct TopicPublishRequest {
    std::string topic;
    std::string type;
    nlohmann::json message;
};

struct ServiceCallRequest {
    std::string call_id;
    std::string service;
    std::string type;
    nlohmann::json request;
    std::optional<int> timeout;
};

struct ActionCallRequest {
    std::string call_id;
    std::string action;
    std::string type;
    nlohmann::json goal;
    std::optional<int> timeout;
};

struct CancelActionRequest {
    std::string call_id;
};

struct StartBagRequest {
    std::string bag_id;
    std::optional<std::vector<std::string>> topics;
    std::string path;
    std::optional<std::string> format;
};

struct StopBagRequest {
    std::string bag_id;
};

// --- Response/push models (server → client, need to_json) ---

struct TopicSubscribedPayload {
    std::string subscription_id;
    std::string topic;
};

struct TopicMessagePayload {
    std::string subscription_id;
    std::string topic;
    std::string timestamp;
    nlohmann::json message;
};

struct ServiceResultPayload {
    std::string call_id;
    bool success;
    std::optional<nlohmann::json> result;
    std::optional<std::string> error;
};

struct ActionFeedbackPayload {
    std::string call_id;
    nlohmann::json feedback;
};

struct ActionResultPayload {
    std::string call_id;
    std::string status;
    std::optional<nlohmann::json> result;
};

struct NodeEventPayload {
    std::string event;
    std::string node;
};

struct BagStatusPayload {
    std::string bag_id;
    std::string status;
    std::optional<double> duration;
    std::optional<int> message_count;
    std::optional<double> size_bytes;
};

// Serialization declarations
void from_json(const nlohmann::json& j, TopicSubscribeRequest& v);
void from_json(const nlohmann::json& j, TopicUnsubscribeRequest& v);
void from_json(const nlohmann::json& j, TopicPublishRequest& v);
void from_json(const nlohmann::json& j, ServiceCallRequest& v);
void from_json(const nlohmann::json& j, ActionCallRequest& v);
void from_json(const nlohmann::json& j, CancelActionRequest& v);
void from_json(const nlohmann::json& j, StartBagRequest& v);
void from_json(const nlohmann::json& j, StopBagRequest& v);

void to_json(nlohmann::json& j, const TopicSubscribedPayload& v);
void to_json(nlohmann::json& j, const TopicMessagePayload& v);
void to_json(nlohmann::json& j, const ServiceResultPayload& v);
void to_json(nlohmann::json& j, const ActionFeedbackPayload& v);
void to_json(nlohmann::json& j, const ActionResultPayload& v);
void to_json(nlohmann::json& j, const NodeEventPayload& v);
void to_json(nlohmann::json& j, const BagStatusPayload& v);

}  // namespace rosweb::models
