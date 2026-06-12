#include "ws/build_channel.hpp"

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#include "errors/error_codes.hpp"
#include "models/terminal_models.hpp"
#include "models/build_models.hpp"

namespace rosweb::ws {

BuildChannel::BuildChannel(std::shared_ptr<build::IBuildManager> build_manager)
    : build_manager_(std::move(build_manager)) {}

BuildChannel::~BuildChannel() = default;

void BuildChannel::set_manager(std::shared_ptr<build::IBuildManager> build_manager) {
    build_manager_ = std::move(build_manager);
}

auto BuildChannel::channel_name() const -> std::string_view {
    return "build";
}

void BuildChannel::handle_message(const WsMessage& msg,
                                   crow::websocket::connection& conn) {
    if (msg.type == "subscribe") {
        handle_subscribe(msg, conn);
    } else if (msg.type == "unsubscribe") {
        handle_unsubscribe(msg, conn);
    }
    // Unknown types are silently ignored per API contract
}

void BuildChannel::handle_disconnect(crow::websocket::connection& conn) {
    std::lock_guard lock(subscriptions_mutex_);
    // Remove from all build subscriber sets
    for (auto& [id, conns] : build_subscribers_) {
        conns.erase(&conn);
    }
    // Remove from all launch subscriber sets
    for (auto& [id, conns] : launch_subscribers_) {
        conns.erase(&conn);
    }
    all_subscribers_.erase(&conn);
}

void BuildChannel::handle_subscribe(const WsMessage& msg,
                                      crow::websocket::connection& conn) {
    std::lock_guard lock(subscriptions_mutex_);
    if (msg.payload.contains("buildId") && msg.payload["buildId"].is_string()) {
        auto build_id = msg.payload["buildId"].get<std::string>();
        build_subscribers_[build_id].insert(&conn);
    } else if (msg.payload.contains("launchId") && msg.payload["launchId"].is_string()) {
        auto launch_id = msg.payload["launchId"].get<std::string>();
        launch_subscribers_[launch_id].insert(&conn);
    } else {
        all_subscribers_.insert(&conn);
    }
}

void BuildChannel::handle_unsubscribe(const WsMessage& msg,
                                        crow::websocket::connection& conn) {
    std::lock_guard lock(subscriptions_mutex_);
    if (msg.payload.contains("buildId") && msg.payload["buildId"].is_string()) {
        auto build_id = msg.payload["buildId"].get<std::string>();
        auto it = build_subscribers_.find(build_id);
        if (it != build_subscribers_.end()) {
            it->second.erase(&conn);
        }
    } else if (msg.payload.contains("launchId") && msg.payload["launchId"].is_string()) {
        auto launch_id = msg.payload["launchId"].get<std::string>();
        auto it = launch_subscribers_.find(launch_id);
        if (it != launch_subscribers_.end()) {
            it->second.erase(&conn);
        }
    } else {
        all_subscribers_.erase(&conn);
    }
}

// --- IBuildListener ---

void BuildChannel::on_build_output(const std::string& build_id,
                                     const std::string& target,
                                     const std::string& stream,
                                     const std::string& data) {
    models::BuildOutputPayload payload;
    payload.build_id = build_id;
    payload.target = target.empty() ? std::nullopt : std::optional<std::string>(target);
    payload.stream = stream;
    payload.data = data;
    nlohmann::json pj = payload;
    broadcast("build-output", pj, build_id);
}

void BuildChannel::on_build_status_changed(const std::string& build_id,
                                              models::BuildStatus status,
                                              const std::vector<models::BuildTargetStatus>& targets) {
    models::BuildStatusPayload payload;
    payload.build_id = build_id;
    payload.status = status;
    payload.targets = targets;
    nlohmann::json pj = payload;
    broadcast("build-status", pj, build_id);
}

void BuildChannel::on_launch_output(const std::string& launch_id,
                                      const std::string& node,
                                      const std::string& stream,
                                      const std::string& data) {
    models::LaunchOutputPayload payload;
    payload.launch_id = launch_id;
    payload.node = node.empty() ? std::nullopt : std::optional<std::string>(node);
    payload.stream = stream;
    payload.data = data;
    nlohmann::json pj = payload;
    broadcast("launch-output", pj, "", launch_id);
}

void BuildChannel::on_launch_status_changed(const std::string& launch_id,
                                               models::LaunchStatus status,
                                               int exit_code) {
    models::LaunchStatusPayload payload;
    payload.launch_id = launch_id;
    payload.status = status;
    payload.exit_code = exit_code;
    nlohmann::json pj = payload;
    broadcast("launch-status", pj, "", launch_id);
}

// --- Helpers ---

void BuildChannel::send_ws(crow::websocket::connection& conn,
                             const std::string& type,
                             const nlohmann::json& payload,
                             std::optional<int> seq) {
    WsMessage msg{.channel = std::string(channel_name()), .type = type,
                  .payload = payload, .seq = seq};
    nlohmann::json j = msg;
    conn.send_text(j.dump());
}

void BuildChannel::broadcast(const std::string& type,
                               const nlohmann::json& payload,
                               const std::string& build_id,
                               const std::string& launch_id) {
    // Copy subscriber sets under the lock, then send outside the lock
    std::vector<crow::websocket::connection*> recipients;

    {
        std::lock_guard lock(subscriptions_mutex_);

        if (!build_id.empty()) {
            auto it = build_subscribers_.find(build_id);
            if (it != build_subscribers_.end()) {
                for (auto* conn : it->second) {
                    recipients.push_back(conn);
                }
            }
        }

        if (!launch_id.empty()) {
            auto it = launch_subscribers_.find(launch_id);
            if (it != launch_subscribers_.end()) {
                for (auto* conn : it->second) {
                    recipients.push_back(conn);
                }
            }
        }

        for (auto* conn : all_subscribers_) {
            // Avoid duplicates
            if (std::find(recipients.begin(), recipients.end(), conn) == recipients.end()) {
                recipients.push_back(conn);
            }
        }
    }

    for (auto* conn : recipients) {
        send_ws(*conn, type, payload, std::nullopt);
    }
}

}  // namespace rosweb::ws
