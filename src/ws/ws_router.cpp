#include "ws/ws_router.hpp"

#include <nlohmann/json.hpp>

#include "errors/error_codes.hpp"
#include "models/terminal_models.hpp"

namespace rosweb::ws {

void WsRouter::register_channel(std::shared_ptr<IWsChannel> channel) {
    auto name = std::string(channel->channel_name());
    channels_.emplace(std::move(name), std::move(channel));
}

void WsRouter::on_open(crow::websocket::connection& /*conn*/) {
    // Nothing to do on open for now
}

void WsRouter::on_message(crow::websocket::connection& conn,
                           const std::string& message, bool is_binary) {
    if (is_binary) {
        return;  // We only handle text (JSON) messages
    }

    WsMessage msg;
    try {
        auto j = nlohmann::json::parse(message);
        msg = j.get<WsMessage>();
    } catch (const std::exception&) {
        send_error(conn, "INVALID_PAYLOAD", "Malformed JSON message", std::nullopt);
        return;
    }

    auto it = channels_.find(msg.channel);
    if (it != channels_.end()) {
        it->second->handle_message(msg, conn);
    }
    // Unknown channels are silently ignored per API contract
}

void WsRouter::on_close(crow::websocket::connection& conn,
                          const std::string& /*reason*/, uint16_t /*status_code*/) {
    for (auto& [name, channel] : channels_) {
        channel->handle_disconnect(conn);
    }
}

void WsRouter::send_error(crow::websocket::connection& conn,
                            const std::string& code,
                            const std::string& message,
                            std::optional<int> seq) {
    models::WsErrorPayload err{.code = code, .message = message};
    WsMessage msg{.channel = "", .type = "error",
                  .payload = nlohmann::json(err), .seq = seq};
    nlohmann::json j = msg;
    conn.send_text(j.dump());
}

}  // namespace rosweb::ws
