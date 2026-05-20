#pragma once

#include <string_view>

#include <crow.h>

#include "ws/ws_message.hpp"

namespace rosweb::ws {

class IWsChannel {
public:
    virtual ~IWsChannel() = default;

    virtual auto channel_name() const -> std::string_view = 0;

    virtual void handle_message(const WsMessage& msg,
                                crow::websocket::connection& conn) = 0;

    virtual void handle_disconnect(crow::websocket::connection& conn) = 0;
};

}  // namespace rosweb::ws
