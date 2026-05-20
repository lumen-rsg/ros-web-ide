#include "models/terminal_models.hpp"

namespace rosweb::models {

void to_json(nlohmann::json& j, const TerminalCreatedPayload& p) {
    j = nlohmann::json{{"terminalId", p.terminal_id}, {"pid", p.pid}};
}

void to_json(nlohmann::json& j, const TerminalOutputPayload& p) {
    j = nlohmann::json{{"terminalId", p.terminal_id}, {"data", p.data}};
}

void to_json(nlohmann::json& j, const TerminalExitedPayload& p) {
    j = nlohmann::json{{"terminalId", p.terminal_id}, {"exitCode", p.exit_code}};
}

void to_json(nlohmann::json& j, const WsErrorPayload& p) {
    j = nlohmann::json{{"code", p.code}, {"message", p.message}};
}

void from_json(const nlohmann::json& j, TerminalCreatePayload& p) {
    j.at("terminalId").get_to(p.terminal_id);
    if (j.contains("shell")) j.at("shell").get_to(p.shell);
    if (j.contains("cwd")) j.at("cwd").get_to(p.cwd);
    if (j.contains("env")) j.at("env").get_to(p.env);
    if (j.contains("cols")) j.at("cols").get_to(p.cols);
    if (j.contains("rows")) j.at("rows").get_to(p.rows);
}

void from_json(const nlohmann::json& j, TerminalInputPayload& p) {
    j.at("terminalId").get_to(p.terminal_id);
    j.at("data").get_to(p.data);
}

void from_json(const nlohmann::json& j, TerminalResizePayload& p) {
    j.at("terminalId").get_to(p.terminal_id);
    j.at("cols").get_to(p.cols);
    j.at("rows").get_to(p.rows);
}

void from_json(const nlohmann::json& j, TerminalClosePayload& p) {
    j.at("terminalId").get_to(p.terminal_id);
}

}  // namespace rosweb::models
