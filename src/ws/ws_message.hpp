#pragma once

#include <optional>
#include <string>
#include <nlohmann/json.hpp>

namespace rosweb::ws {

struct WsMessage {
    std::string channel;
    std::string type;
    nlohmann::json payload;
    std::optional<int> seq;
};

void to_json(nlohmann::json& j, const WsMessage& msg);
void from_json(const nlohmann::json& j, WsMessage& msg);

}  // namespace rosweb::ws
