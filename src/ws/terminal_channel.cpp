#include "ws/terminal_channel.hpp"

#include <functional>
#include <optional>
#include <string>

#include "errors/error_codes.hpp"
#include "models/terminal_models.hpp"

namespace rosweb::ws {

TerminalChannel::TerminalChannel(std::shared_ptr<terminal::IPtyManager> pty_manager)
    : pty_manager_(std::move(pty_manager)) {}

auto TerminalChannel::channel_name() const -> std::string_view {
    return "terminal";
}

void TerminalChannel::handle_message(const WsMessage& msg,
                                     crow::websocket::connection& conn) {
    if (msg.type == "create") {
        handle_create(msg, conn);
    } else if (msg.type == "input") {
        handle_input(msg, conn);
    } else if (msg.type == "resize") {
        handle_resize(msg, conn);
    } else if (msg.type == "close") {
        handle_close(msg, conn);
    }
    // Unknown types are silently ignored per API contract
}

void TerminalChannel::handle_disconnect(crow::websocket::connection& /*conn*/) {
    pty_manager_->close_all();
}

void TerminalChannel::handle_create(const WsMessage& msg,
                                    crow::websocket::connection& conn) {
    models::TerminalCreatePayload payload;
    try {
        msg.payload.get_to(payload);
    } catch (const std::exception&) {
        send_error(conn, "INVALID_PAYLOAD", "Invalid create payload", msg.seq);
        return;
    }

    std::string term_id = payload.terminal_id;
    auto* conn_ptr = &conn;
    auto on_output_with_id = [conn_ptr, term_id](const std::string& /*id*/, std::string data) {
        models::TerminalOutputPayload out{.terminal_id = term_id, .data = std::move(data)};
        nlohmann::json payload_json = out;
        WsMessage ws_msg{.channel = "terminal", .type = "output", .payload = payload_json};
        nlohmann::json msg_json = ws_msg;
        conn_ptr->send_text(msg_json.dump());
    };

    auto on_exit = [conn_ptr, term_id](const std::string& /*id*/, int exit_code) {
        models::TerminalExitedPayload out{.terminal_id = term_id, .exit_code = exit_code};
        nlohmann::json payload_json = out;
        WsMessage ws_msg{.channel = "terminal", .type = "exited", .payload = payload_json};
        nlohmann::json msg_json = ws_msg;
        conn_ptr->send_text(msg_json.dump());
    };

    terminal::PtyCreateParams params{
        .terminal_id = payload.terminal_id,
        .shell = payload.shell,
        .cwd = payload.cwd,
        .env = payload.env,
        .cols = payload.cols,
        .rows = payload.rows,
    };

    auto result = pty_manager_->create(params, on_output_with_id, on_exit);
    if (!result.has_value()) {
        auto code_str = std::string(errors::error_code_to_string(result.error()));
        send_error(conn, code_str, "Failed to create terminal", msg.seq);
        return;
    }

    models::TerminalCreatedPayload created{.terminal_id = term_id, .pid = result.value()};
    send_ws(conn, "created", nlohmann::json(created), msg.seq);
}

void TerminalChannel::handle_input(const WsMessage& msg,
                                   crow::websocket::connection& conn) {
    models::TerminalInputPayload payload;
    try {
        msg.payload.get_to(payload);
    } catch (const std::exception&) {
        send_error(conn, "INVALID_PAYLOAD", "Invalid input payload", msg.seq);
        return;
    }

    auto result = pty_manager_->write(payload.terminal_id, payload.data);
    if (!result.has_value()) {
        auto code_str = std::string(errors::error_code_to_string(result.error()));
        send_error(conn, code_str, "Failed to write to terminal", msg.seq);
    }
}

void TerminalChannel::handle_resize(const WsMessage& msg,
                                    crow::websocket::connection& conn) {
    models::TerminalResizePayload payload;
    try {
        msg.payload.get_to(payload);
    } catch (const std::exception&) {
        send_error(conn, "INVALID_PAYLOAD", "Invalid resize payload", msg.seq);
        return;
    }

    auto result = pty_manager_->resize(payload.terminal_id, payload.cols, payload.rows);
    if (!result.has_value()) {
        auto code_str = std::string(errors::error_code_to_string(result.error()));
        send_error(conn, code_str, "Failed to resize terminal", msg.seq);
    }
}

void TerminalChannel::handle_close(const WsMessage& msg,
                                   crow::websocket::connection& conn) {
    models::TerminalClosePayload payload;
    try {
        msg.payload.get_to(payload);
    } catch (const std::exception&) {
        send_error(conn, "INVALID_PAYLOAD", "Invalid close payload", msg.seq);
        return;
    }

    auto result = pty_manager_->kill(payload.terminal_id);
    if (!result.has_value()) {
        auto code_str = std::string(errors::error_code_to_string(result.error()));
        send_error(conn, code_str, "Failed to close terminal", msg.seq);
    }
    // "exited" message is sent by the on_exit callback when the PTY process exits
}

void TerminalChannel::send_ws(crow::websocket::connection& conn,
                              const std::string& type,
                              const nlohmann::json& payload,
                              std::optional<int> seq) {
    WsMessage msg{.channel = std::string(channel_name()), .type = type,
                  .payload = payload, .seq = seq};
    nlohmann::json j = msg;
    conn.send_text(j.dump());
}

void TerminalChannel::send_error(crow::websocket::connection& conn,
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
