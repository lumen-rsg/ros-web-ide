#include "ws/ros_channel.hpp"

#include <algorithm>
#include <optional>
#include <string>

#include "errors/error_codes.hpp"
#include "models/ros_stream_models.hpp"
#include "models/terminal_models.hpp"

namespace rosweb::ws {

RosChannel::RosChannel(std::shared_ptr<ros::IRosStreamManager> stream_manager)
    : stream_manager_(std::move(stream_manager)) {}

RosChannel::~RosChannel() = default;

auto RosChannel::channel_name() const -> std::string_view {
    return "ros";
}

void RosChannel::handle_message(const WsMessage& msg,
                                 crow::websocket::connection& conn) {
    if (msg.type == "subscribe-topic") {
        handle_subscribe_topic(msg, conn);
    } else if (msg.type == "unsubscribe-topic") {
        handle_unsubscribe_topic(msg, conn);
    } else if (msg.type == "publish-topic") {
        handle_publish_topic(msg, conn);
    } else if (msg.type == "call-service") {
        handle_call_service(msg, conn);
    } else if (msg.type == "call-action") {
        handle_call_action(msg, conn);
    } else if (msg.type == "cancel-action") {
        handle_cancel_action(msg, conn);
    } else if (msg.type == "start-bag") {
        handle_start_bag(msg, conn);
    } else if (msg.type == "stop-bag") {
        handle_stop_bag(msg, conn);
    }
    // Unknown types are silently ignored per API contract
}

void RosChannel::handle_disconnect(crow::websocket::connection& conn) {
    std::lock_guard lock(subscriptions_mutex_);

    for (auto& [id, conns] : topic_subscribers_) {
        conns.erase(&conn);
    }
    for (auto& [id, conns] : service_callers_) {
        conns.erase(&conn);
    }
    for (auto& [id, conns] : action_callers_) {
        conns.erase(&conn);
    }
    for (auto& [id, conns] : bag_subscribers_) {
        conns.erase(&conn);
    }
    node_subscribers_.erase(&conn);
}

// --- Message handlers ---

void RosChannel::handle_subscribe_topic(const WsMessage& msg,
                                          crow::websocket::connection& conn) {
    models::TopicSubscribeRequest req;
    try {
        msg.payload.get_to(req);
    } catch (const std::exception&) {
        send_error(conn, "INVALID_PAYLOAD", "Invalid subscribe-topic payload", msg.seq);
        return;
    }

    auto result = stream_manager_->subscribe_topic(
        req.subscription_id, req.topic, req.type,
        req.throttle_rate, req.queue_length);
    if (!result.has_value()) {
        auto code_str = std::string(errors::error_code_to_string(result.error()));
        send_error(conn, code_str, "Failed to subscribe to topic", msg.seq);
        return;
    }

    {
        std::lock_guard lock(subscriptions_mutex_);
        topic_subscribers_[req.subscription_id].insert(&conn);
        node_subscribers_.insert(&conn);
    }

    models::TopicSubscribedPayload payload{
        .subscription_id = req.subscription_id,
        .topic = req.topic,
    };
    nlohmann::json pj = payload;
    send_ws(conn, "subscribed", pj, msg.seq);
}

void RosChannel::handle_unsubscribe_topic(const WsMessage& msg,
                                            crow::websocket::connection& conn) {
    models::TopicUnsubscribeRequest req;
    try {
        msg.payload.get_to(req);
    } catch (const std::exception&) {
        send_error(conn, "INVALID_PAYLOAD", "Invalid unsubscribe-topic payload", msg.seq);
        return;
    }

    auto result = stream_manager_->unsubscribe_topic(req.subscription_id);
    if (!result.has_value()) {
        auto code_str = std::string(errors::error_code_to_string(result.error()));
        send_error(conn, code_str, "Failed to unsubscribe from topic", msg.seq);
        return;
    }

    {
        std::lock_guard lock(subscriptions_mutex_);
        auto it = topic_subscribers_.find(req.subscription_id);
        if (it != topic_subscribers_.end()) {
            it->second.erase(&conn);
            if (it->second.empty()) {
                topic_subscribers_.erase(it);
            }
        }
    }
}

void RosChannel::handle_publish_topic(const WsMessage& msg,
                                       crow::websocket::connection& conn) {
    models::TopicPublishRequest req;
    try {
        msg.payload.get_to(req);
    } catch (const std::exception&) {
        send_error(conn, "INVALID_PAYLOAD", "Invalid publish-topic payload", msg.seq);
        return;
    }

    auto result = stream_manager_->publish_topic(
        req.topic, req.type, req.message);
    if (!result.has_value()) {
        auto code_str = std::string(errors::error_code_to_string(result.error()));
        send_error(conn, code_str, "Failed to publish to topic", msg.seq);
    }
    // No success response needed for fire-and-forget publish
}

void RosChannel::handle_call_service(const WsMessage& msg,
                                      crow::websocket::connection& conn) {
    models::ServiceCallRequest req;
    try {
        msg.payload.get_to(req);
    } catch (const std::exception&) {
        send_error(conn, "INVALID_PAYLOAD", "Invalid call-service payload", msg.seq);
        return;
    }

    auto result = stream_manager_->call_service(
        req.call_id, req.service, req.type, req.request, req.timeout);
    if (!result.has_value()) {
        auto code_str = std::string(errors::error_code_to_string(result.error()));
        send_error(conn, code_str, "Failed to call service", msg.seq);
        return;
    }

    std::lock_guard lock(subscriptions_mutex_);
    service_callers_[req.call_id].insert(&conn);
}

void RosChannel::handle_call_action(const WsMessage& msg,
                                     crow::websocket::connection& conn) {
    models::ActionCallRequest req;
    try {
        msg.payload.get_to(req);
    } catch (const std::exception&) {
        send_error(conn, "INVALID_PAYLOAD", "Invalid call-action payload", msg.seq);
        return;
    }

    auto result = stream_manager_->call_action(
        req.call_id, req.action, req.type, req.goal, req.timeout);
    if (!result.has_value()) {
        auto code_str = std::string(errors::error_code_to_string(result.error()));
        send_error(conn, code_str, "Failed to call action", msg.seq);
        return;
    }

    std::lock_guard lock(subscriptions_mutex_);
    action_callers_[req.call_id].insert(&conn);
}

void RosChannel::handle_cancel_action(const WsMessage& msg,
                                       crow::websocket::connection& conn) {
    models::CancelActionRequest req;
    try {
        msg.payload.get_to(req);
    } catch (const std::exception&) {
        send_error(conn, "INVALID_PAYLOAD", "Invalid cancel-action payload", msg.seq);
        return;
    }

    auto result = stream_manager_->cancel_action(req.call_id);
    if (!result.has_value()) {
        auto code_str = std::string(errors::error_code_to_string(result.error()));
        send_error(conn, code_str, "Failed to cancel action", msg.seq);
        return;
    }

    std::lock_guard lock(subscriptions_mutex_);
    action_callers_.erase(req.call_id);
}

void RosChannel::handle_start_bag(const WsMessage& msg,
                                   crow::websocket::connection& conn) {
    models::StartBagRequest req;
    try {
        msg.payload.get_to(req);
    } catch (const std::exception&) {
        send_error(conn, "INVALID_PAYLOAD", "Invalid start-bag payload", msg.seq);
        return;
    }

    auto result = stream_manager_->start_bag(
        req.bag_id, req.topics, req.path, req.format);
    if (!result.has_value()) {
        auto code_str = std::string(errors::error_code_to_string(result.error()));
        send_error(conn, code_str, "Failed to start bag recording", msg.seq);
        return;
    }

    std::lock_guard lock(subscriptions_mutex_);
    bag_subscribers_[req.bag_id].insert(&conn);
}

void RosChannel::handle_stop_bag(const WsMessage& msg,
                                  crow::websocket::connection& conn) {
    models::StopBagRequest req;
    try {
        msg.payload.get_to(req);
    } catch (const std::exception&) {
        send_error(conn, "INVALID_PAYLOAD", "Invalid stop-bag payload", msg.seq);
        return;
    }

    auto result = stream_manager_->stop_bag(req.bag_id);
    if (!result.has_value()) {
        auto code_str = std::string(errors::error_code_to_string(result.error()));
        send_error(conn, code_str, "Failed to stop bag recording", msg.seq);
        return;
    }

    std::lock_guard lock(subscriptions_mutex_);
    bag_subscribers_.erase(req.bag_id);
}

// --- IRosStreamListener ---

void RosChannel::on_topic_message(
    const std::string& subscription_id,
    const std::string& topic,
    const std::string& timestamp,
    const nlohmann::json& message) {
    models::TopicMessagePayload payload{
        .subscription_id = subscription_id,
        .topic = topic,
        .timestamp = timestamp,
        .message = message,
    };
    nlohmann::json pj = payload;

    std::vector<crow::websocket::connection*> recipients;
    {
        std::lock_guard lock(subscriptions_mutex_);
        auto it = topic_subscribers_.find(subscription_id);
        if (it != topic_subscribers_.end()) {
            for (auto* conn : it->second) {
                recipients.push_back(conn);
            }
        }
    }

    for (auto* conn : recipients) {
        send_ws(*conn, "topic-message", pj, std::nullopt);
    }
}

void RosChannel::on_service_result(
    const std::string& call_id,
    bool success,
    const std::optional<nlohmann::json>& result,
    const std::optional<std::string>& error) {
    models::ServiceResultPayload payload{
        .call_id = call_id,
        .success = success,
        .result = result,
        .error = error,
    };
    nlohmann::json pj = payload;

    std::vector<crow::websocket::connection*> recipients;
    {
        std::lock_guard lock(subscriptions_mutex_);
        auto it = service_callers_.find(call_id);
        if (it != service_callers_.end()) {
            for (auto* conn : it->second) {
                recipients.push_back(conn);
            }
            service_callers_.erase(it);
        }
    }

    for (auto* conn : recipients) {
        send_ws(*conn, "service-result", pj, std::nullopt);
    }
}

void RosChannel::on_action_feedback(
    const std::string& call_id,
    const nlohmann::json& feedback) {
    models::ActionFeedbackPayload payload{
        .call_id = call_id,
        .feedback = feedback,
    };
    nlohmann::json pj = payload;

    std::vector<crow::websocket::connection*> recipients;
    {
        std::lock_guard lock(subscriptions_mutex_);
        auto it = action_callers_.find(call_id);
        if (it != action_callers_.end()) {
            for (auto* conn : it->second) {
                recipients.push_back(conn);
            }
        }
    }

    for (auto* conn : recipients) {
        send_ws(*conn, "action-feedback", pj, std::nullopt);
    }
}

void RosChannel::on_action_result(
    const std::string& call_id,
    const std::string& status,
    const std::optional<nlohmann::json>& result) {
    models::ActionResultPayload payload{
        .call_id = call_id,
        .status = status,
        .result = result,
    };
    nlohmann::json pj = payload;

    std::vector<crow::websocket::connection*> recipients;
    {
        std::lock_guard lock(subscriptions_mutex_);
        auto it = action_callers_.find(call_id);
        if (it != action_callers_.end()) {
            for (auto* conn : it->second) {
                recipients.push_back(conn);
            }
            action_callers_.erase(it);
        }
    }

    for (auto* conn : recipients) {
        send_ws(*conn, "action-result", pj, std::nullopt);
    }
}

void RosChannel::on_node_event(
    const std::string& event,
    const std::string& node) {
    models::NodeEventPayload payload{
        .event = event,
        .node = node,
    };
    nlohmann::json pj = payload;

    std::vector<crow::websocket::connection*> recipients;
    {
        std::lock_guard lock(subscriptions_mutex_);
        for (auto* conn : node_subscribers_) {
            recipients.push_back(conn);
        }
    }

    for (auto* conn : recipients) {
        send_ws(*conn, "node-event", pj, std::nullopt);
    }
}

void RosChannel::on_bag_status(
    const std::string& bag_id,
    const std::string& status,
    const std::optional<double>& duration,
    const std::optional<int>& message_count,
    const std::optional<double>& size_bytes) {
    models::BagStatusPayload payload{
        .bag_id = bag_id,
        .status = status,
        .duration = duration,
        .message_count = message_count,
        .size_bytes = size_bytes,
    };
    nlohmann::json pj = payload;

    std::vector<crow::websocket::connection*> recipients;
    {
        std::lock_guard lock(subscriptions_mutex_);
        auto it = bag_subscribers_.find(bag_id);
        if (it != bag_subscribers_.end()) {
            for (auto* conn : it->second) {
                recipients.push_back(conn);
            }
        }
    }

    for (auto* conn : recipients) {
        send_ws(*conn, "bag-status", pj, std::nullopt);
    }
}

// --- Helpers ---

void RosChannel::send_ws(crow::websocket::connection& conn,
                          const std::string& type,
                          const nlohmann::json& payload,
                          std::optional<int> seq) {
    WsMessage msg{.channel = std::string(channel_name()), .type = type,
                  .payload = payload, .seq = seq};
    nlohmann::json j = msg;
    conn.send_text(j.dump());
}

void RosChannel::send_error(crow::websocket::connection& conn,
                             const std::string& code,
                             const std::string& message,
                             std::optional<int> seq) {
    models::WsErrorPayload err{.code = code, .message = message};
    WsMessage msg{.channel = std::string(channel_name()), .type = "error",
                  .payload = nlohmann::json(err), .seq = seq};
    nlohmann::json j = msg;
    conn.send_text(j.dump());
}

}  // namespace rosweb::ws
