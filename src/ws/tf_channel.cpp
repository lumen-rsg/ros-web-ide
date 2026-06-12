#include "ws/tf_channel.hpp"

#include <algorithm>
#include <optional>
#include <string>

#include "errors/error_codes.hpp"
#include "models/terminal_models.hpp"
#include "models/tf_models.hpp"

namespace rosweb::ws {

TfChannel::TfChannel(std::shared_ptr<tf::ITfManager> tf_manager)
    : tf_manager_(std::move(tf_manager)) {}

TfChannel::~TfChannel() = default;

void TfChannel::set_manager(std::shared_ptr<tf::ITfManager> tf_manager) {
    tf_manager_ = std::move(tf_manager);
}

auto TfChannel::channel_name() const -> std::string_view {
    return "tf";
}

void TfChannel::handle_message(const WsMessage& msg,
                                crow::websocket::connection& conn) {
    if (msg.type == "subscribe-tf") {
        handle_subscribe_tf(msg, conn);
    } else if (msg.type == "get-tf-tree") {
        handle_get_tf_tree(msg, conn);
    }
    // Unknown types are silently ignored per API contract
}

void TfChannel::handle_disconnect(crow::websocket::connection& conn) {
    std::lock_guard lock(subscriptions_mutex_);
    for (auto& [id, conns] : subscribers_) {
        conns.erase(&conn);
    }
}

// --- Message handlers ---

void TfChannel::handle_subscribe_tf(const WsMessage& msg,
                                      crow::websocket::connection& conn) {
    models::TfSubscribeRequest req;
    try {
        msg.payload.get_to(req);
    } catch (const std::exception&) {
        send_error(conn, "INVALID_PAYLOAD", "Invalid subscribe-tf payload", msg.seq);
        return;
    }

    auto result = tf_manager_->subscribe_tf(
        req.subscription_id, req.frames, req.throttle_rate);
    if (!result.has_value()) {
        auto code_str = std::string(errors::error_code_to_string(result.error()));
        send_error(conn, code_str, "Failed to subscribe to TF", msg.seq);
        return;
    }

    {
        std::lock_guard lock(subscriptions_mutex_);
        subscribers_[req.subscription_id].insert(&conn);
    }

    models::TfSubscribedPayload payload{
        .subscription_id = req.subscription_id,
    };
    nlohmann::json pj = payload;
    send_ws(conn, "subscribed-tf", pj, msg.seq);
}

void TfChannel::handle_get_tf_tree(const WsMessage& msg,
                                     crow::websocket::connection& conn) {
    auto result = tf_manager_->get_tf_tree();
    if (!result.has_value()) {
        auto code_str = std::string(errors::error_code_to_string(result.error()));
        send_error(conn, code_str, "Failed to get TF tree", msg.seq);
        return;
    }

    nlohmann::json pj = result.value();
    send_ws(conn, "tf-tree", pj, msg.seq);
}

// --- ITfListener ---

void TfChannel::on_tf_update(
    const std::string& subscription_id,
    const std::vector<models::TfTransform>& transforms) {
    models::TfUpdatePayload payload{
        .subscription_id = subscription_id,
        .transforms = transforms,
    };
    nlohmann::json pj = payload;

    std::vector<crow::websocket::connection*> recipients;
    {
        std::lock_guard lock(subscriptions_mutex_);
        auto it = subscribers_.find(subscription_id);
        if (it != subscribers_.end()) {
            for (auto* conn : it->second) {
                recipients.push_back(conn);
            }
        }
    }

    for (auto* conn : recipients) {
        send_ws(*conn, "tf-update", pj, std::nullopt);
    }
}

// --- Helpers ---

void TfChannel::send_ws(crow::websocket::connection& conn,
                          const std::string& type,
                          const nlohmann::json& payload,
                          std::optional<int> seq) {
    WsMessage msg{.channel = std::string(channel_name()), .type = type,
                  .payload = payload, .seq = seq};
    nlohmann::json j = msg;
    conn.send_text(j.dump());
}

void TfChannel::send_error(crow::websocket::connection& conn,
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
