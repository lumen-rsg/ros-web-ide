#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <nlohmann/json.hpp>

namespace rosweb::models {

struct TerminalCreatePayload {
    std::string terminal_id;
    std::optional<std::string> shell;
    std::optional<std::string> cwd;
    std::optional<std::unordered_map<std::string, std::string>> env;
    int cols = 80;
    int rows = 24;
};

struct TerminalCreatedPayload {
    std::string terminal_id;
    int pid;
};

struct TerminalInputPayload {
    std::string terminal_id;
    std::string data;
};

struct TerminalResizePayload {
    std::string terminal_id;
    int cols;
    int rows;
};

struct TerminalClosePayload {
    std::string terminal_id;
};

struct TerminalOutputPayload {
    std::string terminal_id;
    std::string data;
};

struct TerminalExitedPayload {
    std::string terminal_id;
    int exit_code;
};

struct WsErrorPayload {
    std::string code;
    std::string message;
};

void to_json(nlohmann::json& j, const TerminalCreatedPayload& p);
void to_json(nlohmann::json& j, const TerminalOutputPayload& p);
void to_json(nlohmann::json& j, const TerminalExitedPayload& p);
void to_json(nlohmann::json& j, const WsErrorPayload& p);

void from_json(const nlohmann::json& j, TerminalCreatePayload& p);
void from_json(const nlohmann::json& j, TerminalInputPayload& p);
void from_json(const nlohmann::json& j, TerminalResizePayload& p);
void from_json(const nlohmann::json& j, TerminalClosePayload& p);

}  // namespace rosweb::models
