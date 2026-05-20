#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

#include <crow.h>

#include "ws/i_ws_channel.hpp"
#include "ws/ws_message.hpp"

namespace rosweb::ws {

class WsRouter {
public:
    void register_channel(std::shared_ptr<IWsChannel> channel);

    void on_open(crow::websocket::connection& conn);
    void on_message(crow::websocket::connection& conn,
                    const std::string& message, bool is_binary);
    void on_close(crow::websocket::connection& conn,
                  const std::string& reason, uint16_t status_code);

private:
    void send_error(crow::websocket::connection& conn,
                    const std::string& code,
                    const std::string& message,
                    std::optional<int> seq);

    std::unordered_map<std::string, std::shared_ptr<IWsChannel>> channels_;
};

}  // namespace rosweb::ws
