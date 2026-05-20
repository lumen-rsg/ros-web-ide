#include "ws/filewatch_channel.hpp"

#include <algorithm>
#include <optional>
#include <string>

#include "errors/error_codes.hpp"
#include "models/filewatch_models.hpp"
#include "models/terminal_models.hpp"

namespace rosweb::ws {

FileWatchChannel::FileWatchChannel(std::shared_ptr<filewatch::IFileWatchManager> manager)
    : manager_(std::move(manager)) {}

FileWatchChannel::~FileWatchChannel() = default;

auto FileWatchChannel::channel_name() const -> std::string_view {
    return "file-watch";
}

void FileWatchChannel::handle_message(const WsMessage& msg,
                                       crow::websocket::connection& conn) {
    if (msg.type == "watch") {
        handle_watch(msg, conn);
    } else if (msg.type == "unwatch") {
        handle_unwatch(msg, conn);
    }
    // Unknown types silently ignored per API contract
}

void FileWatchChannel::handle_disconnect(crow::websocket::connection& conn) {
    // Remove from all subscriber sets
    std::vector<std::string> to_cleanup;
    {
        std::lock_guard lock(subscriptions_mutex_);
        for (auto& [wid, conns] : subscribers_) {
            conns.erase(&conn);
            if (conns.empty()) to_cleanup.push_back(wid);
        }
        for (auto& wid : to_cleanup) {
            subscribers_.erase(wid);
        }
    }

    // Also unwatch at the manager level (best-effort)
    for (auto& wid : to_cleanup) {
        manager_->unwatch(wid);
    }
}

void FileWatchChannel::handle_watch(const WsMessage& msg,
                                     crow::websocket::connection& conn) {
    models::FileWatchRequest req;
    try {
        msg.payload.get_to(req);
    } catch (const std::exception&) {
        models::WsErrorPayload err{.code = "INVALID_PAYLOAD",
                                    .message = "Invalid watch payload"};
        nlohmann::json ej = err;
        send_ws(conn, "error", ej, msg.seq);
        return;
    }

    auto result = manager_->watch(req.watch_id, req.path, req.recursive);
    if (!result.has_value()) {
        models::WsErrorPayload err{.code = std::string(error_code_to_string(result.error())),
                                    .message = "Failed to watch path"};
        nlohmann::json ej = err;
        send_ws(conn, "error", ej, msg.seq);
        return;
    }

    // Track subscriber
    {
        std::lock_guard lock(subscriptions_mutex_);
        subscribers_[req.watch_id].insert(&conn);
    }

    // Send confirmation
    models::FileWatchConfirmPayload confirm{.watch_id = req.watch_id, .path = req.path};
    nlohmann::json pj = confirm;
    send_ws(conn, "watching", pj, msg.seq);
}

void FileWatchChannel::handle_unwatch(const WsMessage& msg,
                                       crow::websocket::connection& conn) {
    models::FileUnwatchRequest req;
    try {
        msg.payload.get_to(req);
    } catch (const std::exception&) {
        models::WsErrorPayload err{.code = "INVALID_PAYLOAD",
                                    .message = "Invalid unwatch payload"};
        nlohmann::json ej = err;
        send_ws(conn, "error", ej, msg.seq);
        return;
    }

    auto result = manager_->unwatch(req.watch_id);
    if (!result.has_value()) {
        models::WsErrorPayload err{.code = std::string(error_code_to_string(result.error())),
                                    .message = "Failed to unwatch"};
        nlohmann::json ej = err;
        send_ws(conn, "error", ej, msg.seq);
        return;
    }

    {
        std::lock_guard lock(subscriptions_mutex_);
        subscribers_.erase(req.watch_id);
    }
}

// --- IFileWatchListener ---

void FileWatchChannel::on_file_changed(const std::string& watch_id,
                                         const std::string& path,
                                         models::FileChangeKind kind,
                                         const std::optional<std::string>& old_path) {
    models::FileChangePayload payload;
    payload.watch_id = watch_id;
    payload.path = path;
    payload.kind = kind;
    payload.old_path = old_path;
    nlohmann::json pj = payload;

    // Copy subscribers under lock, send outside lock
    std::vector<crow::websocket::connection*> recipients;
    {
        std::lock_guard lock(subscriptions_mutex_);
        auto it = subscribers_.find(watch_id);
        if (it != subscribers_.end()) {
            for (auto* conn : it->second) {
                recipients.push_back(conn);
            }
        }
    }

    for (auto* conn : recipients) {
        send_ws(*conn, "changed", pj, std::nullopt);
    }
}

// --- Helpers ---

void FileWatchChannel::send_ws(crow::websocket::connection& conn,
                                 const std::string& type,
                                 const nlohmann::json& payload,
                                 std::optional<int> seq) {
    WsMessage msg{.channel = std::string(channel_name()), .type = type,
                  .payload = payload, .seq = seq};
    nlohmann::json j = msg;
    conn.send_text(j.dump());
}

}  // namespace rosweb::ws
