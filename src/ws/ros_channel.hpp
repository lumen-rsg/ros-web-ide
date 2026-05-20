#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include <crow.h>
#include "ros/i_ros_stream_listener.hpp"
#include "ros/i_ros_stream_manager.hpp"
#include "ws/i_ws_channel.hpp"

namespace rosweb::ws {

class RosChannel : public IWsChannel, public ros::IRosStreamListener {
public:
    explicit RosChannel(std::shared_ptr<ros::IRosStreamManager> stream_manager);
    ~RosChannel() override;

    // IWsChannel
    auto channel_name() const -> std::string_view override;
    void handle_message(const WsMessage& msg,
                        crow::websocket::connection& conn) override;
    void handle_disconnect(crow::websocket::connection& conn) override;

    // IRosStreamListener
    void on_topic_message(
        const std::string& subscription_id,
        const std::string& topic,
        const std::string& timestamp,
        const nlohmann::json& message) override;

    void on_service_result(
        const std::string& call_id,
        bool success,
        const std::optional<nlohmann::json>& result,
        const std::optional<std::string>& error) override;

    void on_action_feedback(
        const std::string& call_id,
        const nlohmann::json& feedback) override;

    void on_action_result(
        const std::string& call_id,
        const std::string& status,
        const std::optional<nlohmann::json>& result) override;

    void on_node_event(
        const std::string& event,
        const std::string& node) override;

    void on_bag_status(
        const std::string& bag_id,
        const std::string& status,
        const std::optional<double>& duration,
        const std::optional<int>& message_count,
        const std::optional<double>& size_bytes) override;

private:
    void handle_subscribe_topic(const WsMessage& msg,
                                 crow::websocket::connection& conn);
    void handle_unsubscribe_topic(const WsMessage& msg,
                                   crow::websocket::connection& conn);
    void handle_publish_topic(const WsMessage& msg,
                               crow::websocket::connection& conn);
    void handle_call_service(const WsMessage& msg,
                              crow::websocket::connection& conn);
    void handle_call_action(const WsMessage& msg,
                             crow::websocket::connection& conn);
    void handle_cancel_action(const WsMessage& msg,
                               crow::websocket::connection& conn);
    void handle_start_bag(const WsMessage& msg,
                           crow::websocket::connection& conn);
    void handle_stop_bag(const WsMessage& msg,
                          crow::websocket::connection& conn);

    void send_ws(crow::websocket::connection& conn,
                 const std::string& type,
                 const nlohmann::json& payload,
                 std::optional<int> seq);

    void send_error(crow::websocket::connection& conn,
                    const std::string& code,
                    const std::string& message,
                    std::optional<int> seq);

    std::shared_ptr<ros::IRosStreamManager> stream_manager_;

    mutable std::mutex subscriptions_mutex_;

    // subscription_id -> set of connections receiving that topic's messages
    std::unordered_map<std::string, std::unordered_set<crow::websocket::connection*>>
        topic_subscribers_;

    // call_id -> connection that made the service call
    std::unordered_map<std::string, std::unordered_set<crow::websocket::connection*>>
        service_callers_;

    // call_id -> connection that made the action call
    std::unordered_map<std::string, std::unordered_set<crow::websocket::connection*>>
        action_callers_;

    // bag_id -> set of connections monitoring the bag
    std::unordered_map<std::string, std::unordered_set<crow::websocket::connection*>>
        bag_subscribers_;

    // Global node event subscribers
    std::unordered_set<crow::websocket::connection*> node_subscribers_;
};

}  // namespace rosweb::ws
