#include "ws/ws_message.hpp"

namespace rosweb::ws {

void to_json(nlohmann::json& j, const WsMessage& msg) {
    j = nlohmann::json{
        {"channel", msg.channel},
        {"type", msg.type},
        {"payload", msg.payload},
    };
    if (msg.seq.has_value()) {
        j["seq"] = *msg.seq;
    }
}

void from_json(const nlohmann::json& j, WsMessage& msg) {
    j.at("channel").get_to(msg.channel);
    j.at("type").get_to(msg.type);
    if (j.contains("payload") && j["payload"].is_object()) {
        msg.payload = j["payload"];
    }
    if (j.contains("seq") && !j["seq"].is_null()) {
        msg.seq = j["seq"].get<int>();
    }
}

}  // namespace rosweb::ws
