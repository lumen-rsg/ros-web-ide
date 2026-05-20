#include <doctest.h>
#include <nlohmann/json.hpp>

#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "ros/i_ros_stream_listener.hpp"
#include "ros/i_ros_stream_manager.hpp"
#include "errors/error_codes.hpp"
#include "models/ros_stream_models.hpp"
#include "models/terminal_models.hpp"
#include "ws/i_ws_channel.hpp"
#include "ws/ws_message.hpp"
#include "ws/ros_channel.hpp"

using namespace rosweb::ros;
using namespace rosweb::ws;
using namespace rosweb::models;

namespace {

class FakeConnection : public crow::websocket::connection {
public:
    std::vector<std::string> sent_text;

    void send_binary(std::string msg) override { (void)msg; }
    void send_text(std::string msg) override { sent_text.push_back(std::move(msg)); }
    void send_ping(std::string) override {}
    void send_pong(std::string) override {}
    void close(std::string const&, uint16_t) override {}
    std::string get_remote_ip() override { return "127.0.0.1"; }
    std::string get_subprotocol() const override { return ""; }
};

auto parse_sent(const std::vector<std::string>& msgs)
    -> std::vector<WsMessage> {
    std::vector<WsMessage> result;
    for (const auto& m : msgs) {
        auto j = nlohmann::json::parse(m);
        result.push_back(j.get<WsMessage>());
    }
    return result;
}

class MockRosStreamManager : public IRosStreamManager {
public:
    bool subscribe_should_fail = false;
    bool publish_should_fail = false;
    bool service_should_fail = false;
    bool action_should_fail = false;
    bool bag_should_fail = false;
    bool cancel_should_fail = false;

    std::string last_subscription_id;
    std::string last_topic;
    std::string last_bag_id;
    std::string last_call_id;
    std::string last_service;
    std::string last_action;

    std::vector<std::shared_ptr<IRosStreamListener>> listeners;

    auto subscribe_topic(
        const std::string& subscription_id,
        const std::string& topic,
        const std::optional<std::string>&,
        const std::optional<int>&,
        const std::optional<int>&)
        -> std::expected<void, rosweb::errors::ErrorCode> override {
        last_subscription_id = subscription_id;
        last_topic = topic;
        if (subscribe_should_fail) {
            return std::unexpected(rosweb::errors::ErrorCode::ROS_SERVICE_UNAVAILABLE);
        }
        return {};
    }

    auto unsubscribe_topic(const std::string& subscription_id)
        -> std::expected<void, rosweb::errors::ErrorCode> override {
        last_subscription_id = subscription_id;
        if (subscription_id == "unknown_sub") {
            return std::unexpected(rosweb::errors::ErrorCode::SUBSCRIPTION_NOT_FOUND);
        }
        return {};
    }

    auto publish_topic(
        const std::string& topic,
        const std::string& type,
        const nlohmann::json&)
        -> std::expected<void, rosweb::errors::ErrorCode> override {
        last_topic = topic;
        if (publish_should_fail) {
            return std::unexpected(rosweb::errors::ErrorCode::ROS_INVALID_MESSAGE);
        }
        return {};
    }

    auto call_service(
        const std::string& call_id,
        const std::string& service,
        const std::string& type,
        const nlohmann::json&,
        const std::optional<int>&)
        -> std::expected<void, rosweb::errors::ErrorCode> override {
        last_call_id = call_id;
        last_service = service;
        if (service_should_fail) {
            return std::unexpected(rosweb::errors::ErrorCode::ROS_SERVICE_UNAVAILABLE);
        }
        return {};
    }

    auto call_action(
        const std::string& call_id,
        const std::string& action,
        const std::string& type,
        const nlohmann::json&,
        const std::optional<int>&)
        -> std::expected<void, rosweb::errors::ErrorCode> override {
        last_call_id = call_id;
        last_action = action;
        if (action_should_fail) {
            return std::unexpected(rosweb::errors::ErrorCode::ACTION_NOT_FOUND);
        }
        return {};
    }

    auto cancel_action(const std::string& call_id)
        -> std::expected<void, rosweb::errors::ErrorCode> override {
        last_call_id = call_id;
        if (cancel_should_fail) {
            return std::unexpected(rosweb::errors::ErrorCode::ACTION_NOT_FOUND);
        }
        return {};
    }

    auto start_bag(
        const std::string& bag_id,
        const std::optional<std::vector<std::string>>&,
        const std::string&,
        const std::optional<std::string>&)
        -> std::expected<void, rosweb::errors::ErrorCode> override {
        last_bag_id = bag_id;
        if (bag_should_fail) {
            return std::unexpected(rosweb::errors::ErrorCode::BAG_WRITE_ERROR);
        }
        return {};
    }

    auto stop_bag(const std::string& bag_id)
        -> std::expected<void, rosweb::errors::ErrorCode> override {
        last_bag_id = bag_id;
        if (bag_id == "unknown_bag") {
            return std::unexpected(rosweb::errors::ErrorCode::BAG_NOT_RECORDING);
        }
        return {};
    }

    auto start_node_monitor() -> void override {}
    auto stop_node_monitor() -> void override {}

    auto add_listener(std::shared_ptr<IRosStreamListener> l) -> void override {
        listeners.push_back(std::move(l));
    }
    auto remove_listener(std::shared_ptr<IRosStreamListener>) -> void override {}

    auto shutdown() -> void override {}
};

auto make_subscribe_msg(const std::string& sub_id, const std::string& topic,
                         std::optional<int> seq = std::nullopt) -> WsMessage {
    WsMessage msg;
    msg.channel = "ros";
    msg.type = "subscribe-topic";
    msg.payload = nlohmann::json{{"subscriptionId", sub_id}, {"topic", topic}};
    msg.seq = seq;
    return msg;
}

}  // namespace

TEST_SUITE("RosChannel") {

    TEST_CASE("channel_name returns ros") {
        auto mgr = std::make_shared<MockRosStreamManager>();
        RosChannel ch(mgr);
        CHECK(ch.channel_name() == "ros");
    }

    TEST_CASE("subscribe-topic sends subscribed confirmation") {
        auto mgr = std::make_shared<MockRosStreamManager>();
        RosChannel ch(mgr);
        FakeConnection conn;

        auto msg = make_subscribe_msg("sub_1", "/chatter", 42);
        ch.handle_message(msg, conn);

        auto parsed = parse_sent(conn.sent_text);
        REQUIRE(parsed.size() == 1);
        CHECK(parsed[0].channel == "ros");
        CHECK(parsed[0].type == "subscribed");
        CHECK(parsed[0].seq.value() == 42);
        CHECK(parsed[0].payload["subscriptionId"] == "sub_1");
        CHECK(parsed[0].payload["topic"] == "/chatter");
    }

    TEST_CASE("subscribe-topic with invalid payload sends error") {
        auto mgr = std::make_shared<MockRosStreamManager>();
        RosChannel ch(mgr);
        FakeConnection conn;

        WsMessage msg;
        msg.channel = "ros";
        msg.type = "subscribe-topic";
        msg.payload = nlohmann::json{{"bad", "payload"}};
        msg.seq = 1;
        ch.handle_message(msg, conn);

        auto parsed = parse_sent(conn.sent_text);
        REQUIRE(parsed.size() == 1);
        CHECK(parsed[0].type == "error");
        CHECK(parsed[0].payload["code"] == "INVALID_PAYLOAD");
    }

    TEST_CASE("subscribe-topic when manager fails sends error") {
        auto mgr = std::make_shared<MockRosStreamManager>();
        mgr->subscribe_should_fail = true;
        RosChannel ch(mgr);
        FakeConnection conn;

        auto msg = make_subscribe_msg("sub_1", "/chatter", 1);
        ch.handle_message(msg, conn);

        auto parsed = parse_sent(conn.sent_text);
        REQUIRE(parsed.size() == 1);
        CHECK(parsed[0].type == "error");
        CHECK(parsed[0].payload["code"] == "ROS_SERVICE_UNAVAILABLE");
    }

    TEST_CASE("unsubscribe-topic removes subscription") {
        auto mgr = std::make_shared<MockRosStreamManager>();
        RosChannel ch(mgr);
        FakeConnection conn;

        // Subscribe first
        auto sub_msg = make_subscribe_msg("sub_1", "/chatter");
        ch.handle_message(sub_msg, conn);
        conn.sent_text.clear();

        // Unsubscribe
        WsMessage unsub;
        unsub.channel = "ros";
        unsub.type = "unsubscribe-topic";
        unsub.payload = nlohmann::json{{"subscriptionId", "sub_1"}};
        ch.handle_message(unsub, conn);

        // No message sent for successful unsubscribe
        CHECK(conn.sent_text.empty());
        CHECK(mgr->last_subscription_id == "sub_1");
    }

    TEST_CASE("unsubscribe-topic for unknown ID sends error") {
        auto mgr = std::make_shared<MockRosStreamManager>();
        RosChannel ch(mgr);
        FakeConnection conn;

        WsMessage unsub;
        unsub.channel = "ros";
        unsub.type = "unsubscribe-topic";
        unsub.payload = nlohmann::json{{"subscriptionId", "unknown_sub"}};
        unsub.seq = 5;
        ch.handle_message(unsub, conn);

        auto parsed = parse_sent(conn.sent_text);
        REQUIRE(parsed.size() == 1);
        CHECK(parsed[0].type == "error");
        CHECK(parsed[0].seq.value() == 5);
    }

    TEST_CASE("publish-topic calls manager") {
        auto mgr = std::make_shared<MockRosStreamManager>();
        RosChannel ch(mgr);
        FakeConnection conn;

        WsMessage msg;
        msg.channel = "ros";
        msg.type = "publish-topic";
        msg.payload = nlohmann::json{
            {"topic", "/cmd_vel"},
            {"type", "geometry_msgs/msg/Twist"},
            {"message", {{"linear", {{"x", 1.0}}}}}
        };
        ch.handle_message(msg, conn);

        CHECK(mgr->last_topic == "/cmd_vel");
        CHECK(conn.sent_text.empty());  // no response on success
    }

    TEST_CASE("publish-topic with invalid payload sends error") {
        auto mgr = std::make_shared<MockRosStreamManager>();
        RosChannel ch(mgr);
        FakeConnection conn;

        WsMessage msg;
        msg.channel = "ros";
        msg.type = "publish-topic";
        msg.payload = nlohmann::json{{"bad", "payload"}};
        msg.seq = 10;
        ch.handle_message(msg, conn);

        auto parsed = parse_sent(conn.sent_text);
        REQUIRE(parsed.size() == 1);
        CHECK(parsed[0].type == "error");
    }

    TEST_CASE("call-service records caller") {
        auto mgr = std::make_shared<MockRosStreamManager>();
        RosChannel ch(mgr);
        FakeConnection conn;

        WsMessage msg;
        msg.channel = "ros";
        msg.type = "call-service";
        msg.payload = nlohmann::json{
            {"callId", "call_1"},
            {"service", "/set_parameters"},
            {"type", "rcl_interfaces/srv/SetParameters"},
            {"request", {}}
        };
        msg.seq = 20;
        ch.handle_message(msg, conn);

        CHECK(mgr->last_call_id == "call_1");
        CHECK(conn.sent_text.empty());  // async, no immediate response
    }

    TEST_CASE("on_topic_message delivered to subscriber") {
        auto mgr = std::make_shared<MockRosStreamManager>();
        RosChannel ch(mgr);
        FakeConnection conn;

        auto sub_msg = make_subscribe_msg("sub_1", "/chatter");
        ch.handle_message(sub_msg, conn);
        conn.sent_text.clear();

        ch.on_topic_message("sub_1", "/chatter", "1716201000",
            nlohmann::json::parse(R"({"data": "Hello!"})"));

        auto parsed = parse_sent(conn.sent_text);
        REQUIRE(parsed.size() == 1);
        CHECK(parsed[0].type == "topic-message");
        CHECK(parsed[0].payload["subscriptionId"] == "sub_1");
        CHECK(parsed[0].payload["message"]["data"] == "Hello!");
    }

    TEST_CASE("on_topic_message not delivered to other subscribers") {
        auto mgr = std::make_shared<MockRosStreamManager>();
        RosChannel ch(mgr);
        FakeConnection conn1, conn2;

        auto sub1 = make_subscribe_msg("sub_1", "/chatter");
        ch.handle_message(sub1, conn1);

        auto sub2 = make_subscribe_msg("sub_2", "/cmd_vel");
        ch.handle_message(sub2, conn2);

        conn1.sent_text.clear();
        conn2.sent_text.clear();

        ch.on_topic_message("sub_1", "/chatter", "0",
            nlohmann::json::parse(R"({"data": "Hi"})"));

        CHECK(conn1.sent_text.size() == 1);
        CHECK(conn2.sent_text.empty());
    }

    TEST_CASE("on_service_result delivered to caller") {
        auto mgr = std::make_shared<MockRosStreamManager>();
        RosChannel ch(mgr);
        FakeConnection conn;

        WsMessage msg;
        msg.channel = "ros";
        msg.type = "call-service";
        msg.payload = nlohmann::json{
            {"callId", "call_1"},
            {"service", "/svc"},
            {"type", "SrvType"},
            {"request", {}}
        };
        ch.handle_message(msg, conn);
        conn.sent_text.clear();

        ch.on_service_result("call_1", true,
            nlohmann::json::parse(R"({"ok": true})"), std::nullopt);

        auto parsed = parse_sent(conn.sent_text);
        REQUIRE(parsed.size() == 1);
        CHECK(parsed[0].type == "service-result");
        CHECK(parsed[0].payload["callId"] == "call_1");
        CHECK(parsed[0].payload["success"] == true);
    }

    TEST_CASE("on_action_feedback delivered to caller") {
        auto mgr = std::make_shared<MockRosStreamManager>();
        RosChannel ch(mgr);
        FakeConnection conn;

        WsMessage msg;
        msg.channel = "ros";
        msg.type = "call-action";
        msg.payload = nlohmann::json{
            {"callId", "act_1"},
            {"action", "/nav"},
            {"type", "NavAction"},
            {"goal", {}}
        };
        ch.handle_message(msg, conn);
        conn.sent_text.clear();

        ch.on_action_feedback("act_1",
            nlohmann::json::parse(R"({"progress": 0.5})"));

        auto parsed = parse_sent(conn.sent_text);
        REQUIRE(parsed.size() == 1);
        CHECK(parsed[0].type == "action-feedback");
    }

    TEST_CASE("on_action_result delivered to caller") {
        auto mgr = std::make_shared<MockRosStreamManager>();
        RosChannel ch(mgr);
        FakeConnection conn;

        WsMessage msg;
        msg.channel = "ros";
        msg.type = "call-action";
        msg.payload = nlohmann::json{
            {"callId", "act_1"},
            {"action", "/nav"},
            {"type", "NavAction"},
            {"goal", {}}
        };
        ch.handle_message(msg, conn);
        conn.sent_text.clear();

        ch.on_action_result("act_1", "succeeded",
            nlohmann::json::parse(R"({"code": 0})"));

        auto parsed = parse_sent(conn.sent_text);
        REQUIRE(parsed.size() == 1);
        CHECK(parsed[0].type == "action-result");
        CHECK(parsed[0].payload["status"] == "succeeded");
    }

    TEST_CASE("on_node_event delivered to subscribers") {
        auto mgr = std::make_shared<MockRosStreamManager>();
        RosChannel ch(mgr);
        FakeConnection conn;

        // Subscribe to a topic first (which also subscribes to node events)
        auto sub_msg = make_subscribe_msg("sub_1", "/chatter");
        ch.handle_message(sub_msg, conn);
        conn.sent_text.clear();

        ch.on_node_event("started", "/talker");

        auto parsed = parse_sent(conn.sent_text);
        REQUIRE(parsed.size() == 1);
        CHECK(parsed[0].type == "node-event");
        CHECK(parsed[0].payload["event"] == "started");
        CHECK(parsed[0].payload["node"] == "/talker");
    }

    TEST_CASE("on_bag_status delivered to bag subscriber") {
        auto mgr = std::make_shared<MockRosStreamManager>();
        RosChannel ch(mgr);
        FakeConnection conn;

        WsMessage msg;
        msg.channel = "ros";
        msg.type = "start-bag";
        msg.payload = nlohmann::json{
            {"bagId", "bag_1"},
            {"path", "/tmp/bag"}
        };
        ch.handle_message(msg, conn);
        conn.sent_text.clear();

        ch.on_bag_status("bag_1", "recording", 10.5, 100, 4096.0);

        auto parsed = parse_sent(conn.sent_text);
        REQUIRE(parsed.size() == 1);
        CHECK(parsed[0].type == "bag-status");
        CHECK(parsed[0].payload["bagId"] == "bag_1");
        CHECK(parsed[0].payload["status"] == "recording");
    }

    TEST_CASE("cancel-action for unknown call sends error") {
        auto mgr = std::make_shared<MockRosStreamManager>();
        mgr->cancel_should_fail = true;
        RosChannel ch(mgr);
        FakeConnection conn;

        WsMessage msg;
        msg.channel = "ros";
        msg.type = "cancel-action";
        msg.payload = nlohmann::json{{"callId", "unknown_act"}};
        msg.seq = 30;
        ch.handle_message(msg, conn);

        auto parsed = parse_sent(conn.sent_text);
        REQUIRE(parsed.size() == 1);
        CHECK(parsed[0].type == "error");
        CHECK(parsed[0].payload["code"] == "ACTION_NOT_FOUND");
    }

    TEST_CASE("stop-bag for unknown bag sends error") {
        auto mgr = std::make_shared<MockRosStreamManager>();
        RosChannel ch(mgr);
        FakeConnection conn;

        WsMessage msg;
        msg.channel = "ros";
        msg.type = "stop-bag";
        msg.payload = nlohmann::json{{"bagId", "unknown_bag"}};
        msg.seq = 40;
        ch.handle_message(msg, conn);

        auto parsed = parse_sent(conn.sent_text);
        REQUIRE(parsed.size() == 1);
        CHECK(parsed[0].type == "error");
        CHECK(parsed[0].payload["code"] == "BAG_NOT_RECORDING");
    }

    TEST_CASE("disconnect cleans up subscriptions") {
        auto mgr = std::make_shared<MockRosStreamManager>();
        RosChannel ch(mgr);
        FakeConnection conn1, conn2;

        auto sub1 = make_subscribe_msg("sub_1", "/chatter");
        ch.handle_message(sub1, conn1);

        auto sub2 = make_subscribe_msg("sub_2", "/chatter");
        ch.handle_message(sub2, conn2);

        conn1.sent_text.clear();
        conn2.sent_text.clear();

        ch.handle_disconnect(conn1);

        // conn2 should still get messages for sub_2
        ch.on_topic_message("sub_2", "/chatter", "0",
            nlohmann::json::parse(R"({"data": "test"})"));
        CHECK(conn2.sent_text.size() == 1);

        // sub_1 should not be delivered (conn1 disconnected)
        // This doesn't crash — the connection pointer was removed
    }

    TEST_CASE("unknown message type silently ignored") {
        auto mgr = std::make_shared<MockRosStreamManager>();
        RosChannel ch(mgr);
        FakeConnection conn;

        WsMessage msg;
        msg.channel = "ros";
        msg.type = "unknown-type";
        msg.payload = nlohmann::json{};
        ch.handle_message(msg, conn);

        CHECK(conn.sent_text.empty());
    }

    TEST_CASE("listener registration via manager") {
        auto mgr = std::make_shared<MockRosStreamManager>();
        auto ch = std::make_shared<RosChannel>(mgr);
        mgr->add_listener(ch);

        REQUIRE(mgr->listeners.size() == 1);
    }
}
