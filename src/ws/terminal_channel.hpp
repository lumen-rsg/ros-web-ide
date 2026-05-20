#pragma once

#include <memory>

#include <crow.h>

#include "terminal/i_pty_manager.hpp"
#include "ws/i_ws_channel.hpp"

namespace rosweb::ws {

class TerminalChannel : public IWsChannel {
public:
    explicit TerminalChannel(std::shared_ptr<terminal::IPtyManager> pty_manager);

    auto channel_name() const -> std::string_view override;

    void handle_message(const WsMessage& msg,
                        crow::websocket::connection& conn) override;

    void handle_disconnect(crow::websocket::connection& conn) override;

private:
    void handle_create(const WsMessage& msg, crow::websocket::connection& conn);
    void handle_input(const WsMessage& msg, crow::websocket::connection& conn);
    void handle_resize(const WsMessage& msg, crow::websocket::connection& conn);
    void handle_close(const WsMessage& msg, crow::websocket::connection& conn);

    void send_ws(crow::websocket::connection& conn,
                 const std::string& type,
                 const nlohmann::json& payload,
                 std::optional<int> seq);

    void send_error(crow::websocket::connection& conn,
                    const std::string& code,
                    const std::string& message,
                    std::optional<int> seq);

    std::shared_ptr<terminal::IPtyManager> pty_manager_;
};

}  // namespace rosweb::ws
