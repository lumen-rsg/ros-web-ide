#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include <crow.h>

#include "filewatch/i_filewatch_manager.hpp"
#include "ws/i_ws_channel.hpp"

namespace rosweb::ws {

class FileWatchChannel : public IWsChannel, public filewatch::IFileWatchListener {
public:
    explicit FileWatchChannel(std::shared_ptr<filewatch::IFileWatchManager> manager);
    ~FileWatchChannel() override;

    // IWsChannel
    auto channel_name() const -> std::string_view override;
    void handle_message(const WsMessage& msg,
                        crow::websocket::connection& conn) override;
    void handle_disconnect(crow::websocket::connection& conn) override;

    // IFileWatchListener
    void on_file_changed(const std::string& watch_id,
                          const std::string& path,
                          models::FileChangeKind kind,
                          const std::optional<std::string>& old_path) override;

private:
    void handle_watch(const WsMessage& msg, crow::websocket::connection& conn);
    void handle_unwatch(const WsMessage& msg, crow::websocket::connection& conn);

    void send_ws(crow::websocket::connection& conn,
                 const std::string& type,
                 const nlohmann::json& payload,
                 std::optional<int> seq);

    std::shared_ptr<filewatch::IFileWatchManager> manager_;

    mutable std::mutex subscriptions_mutex_;
    std::unordered_map<std::string, std::unordered_set<crow::websocket::connection*>>
        subscribers_;  // watch_id -> connections
};

}  // namespace rosweb::ws
