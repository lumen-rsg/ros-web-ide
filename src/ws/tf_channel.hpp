#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include <crow.h>
#include "tf/i_tf_listener.hpp"
#include "tf/i_tf_manager.hpp"
#include "ws/i_ws_channel.hpp"

namespace rosweb::ws {

class TfChannel : public IWsChannel, public tf::ITfListener {
public:
    explicit TfChannel(std::shared_ptr<tf::ITfManager> tf_manager);
    ~TfChannel() override;

    void set_manager(std::shared_ptr<tf::ITfManager> tf_manager);

    // IWsChannel
    auto channel_name() const -> std::string_view override;
    void handle_message(const WsMessage& msg,
                        crow::websocket::connection& conn) override;
    void handle_disconnect(crow::websocket::connection& conn) override;

    // ITfListener
    void on_tf_update(
        const std::string& subscription_id,
        const std::vector<models::TfTransform>& transforms) override;

private:
    void handle_subscribe_tf(const WsMessage& msg,
                              crow::websocket::connection& conn);
    void handle_get_tf_tree(const WsMessage& msg,
                             crow::websocket::connection& conn);

    void send_ws(crow::websocket::connection& conn,
                 const std::string& type,
                 const nlohmann::json& payload,
                 std::optional<int> seq);

    void send_error(crow::websocket::connection& conn,
                    const std::string& code,
                    const std::string& message,
                    std::optional<int> seq);

    std::shared_ptr<tf::ITfManager> tf_manager_;

    mutable std::mutex subscriptions_mutex_;
    std::unordered_map<std::string, std::unordered_set<crow::websocket::connection*>>
        subscribers_;
};

}  // namespace rosweb::ws
