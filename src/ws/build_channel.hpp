#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include <crow.h>

#include "build/i_build_listener.hpp"
#include "build/i_build_manager.hpp"
#include "ws/i_ws_channel.hpp"

namespace rosweb::ws {

class BuildChannel : public IWsChannel, public build::IBuildListener {
public:
    explicit BuildChannel(std::shared_ptr<build::IBuildManager> build_manager);
    ~BuildChannel() override;

    void set_manager(std::shared_ptr<build::IBuildManager> build_manager);

    // IWsChannel
    auto channel_name() const -> std::string_view override;
    void handle_message(const WsMessage& msg,
                        crow::websocket::connection& conn) override;
    void handle_disconnect(crow::websocket::connection& conn) override;

    // IBuildListener
    void on_build_output(const std::string& build_id,
                          const std::string& target,
                          const std::string& stream,
                          const std::string& data) override;
    void on_build_status_changed(const std::string& build_id,
                                  models::BuildStatus status,
                                  const std::vector<models::BuildTargetStatus>& targets) override;
    void on_launch_output(const std::string& launch_id,
                           const std::string& node,
                           const std::string& stream,
                           const std::string& data) override;
    void on_launch_status_changed(const std::string& launch_id,
                                   models::LaunchStatus status,
                                   int exit_code) override;

private:
    void handle_subscribe(const WsMessage& msg, crow::websocket::connection& conn);
    void handle_unsubscribe(const WsMessage& msg, crow::websocket::connection& conn);

    void send_ws(crow::websocket::connection& conn,
                 const std::string& type,
                 const nlohmann::json& payload,
                 std::optional<int> seq);

    void broadcast(const std::string& type,
                   const nlohmann::json& payload,
                   const std::string& build_id = "",
                   const std::string& launch_id = "");

    std::shared_ptr<build::IBuildManager> build_manager_;

    mutable std::mutex subscriptions_mutex_;
    std::unordered_map<std::string, std::unordered_set<crow::websocket::connection*>>
        build_subscribers_;
    std::unordered_map<std::string, std::unordered_set<crow::websocket::connection*>>
        launch_subscribers_;
    std::unordered_set<crow::websocket::connection*> all_subscribers_;
};

}  // namespace rosweb::ws
