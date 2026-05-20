#include <doctest.h>
#include <nlohmann/json.hpp>

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "models/tf_models.hpp"
#include "tf/i_tf_listener.hpp"
#include "tf/i_tf_manager.hpp"
#include "ws/i_ws_channel.hpp"
#include "ws/ws_message.hpp"
#include "ws/tf_channel.hpp"

using namespace rosweb::tf;
using namespace rosweb::ws;
using namespace rosweb::models;
using namespace rosweb::errors;

namespace {

using namespace rosweb::errors;

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

class MockTfManager : public ITfManager {
public:
    bool subscribe_should_fail = false;
    bool tree_should_fail = false;

    std::string last_subscription_id;
    std::vector<std::shared_ptr<ITfListener>> listeners;

    auto subscribe_tf(
        const std::string& subscription_id,
        const std::optional<std::vector<std::string>>&,
        const std::optional<int>&)
        -> std::expected<void, rosweb::errors::ErrorCode> override {
        last_subscription_id = subscription_id;
        if (subscribe_should_fail) {
            return std::unexpected(rosweb::errors::ErrorCode::ROS_SERVICE_UNAVAILABLE);
        }
        return {};
    }

    auto unsubscribe_tf(const std::string& subscription_id)
        -> std::expected<void, rosweb::errors::ErrorCode> override {
        last_subscription_id = subscription_id;
        if (subscription_id == "unknown_tf") {
            return std::unexpected(rosweb::errors::ErrorCode::SUBSCRIPTION_NOT_FOUND);
        }
        return {};
    }

    auto get_tf_tree()
        -> std::expected<TfTreePayload, rosweb::errors::ErrorCode> override {
        if (tree_should_fail) {
            return std::unexpected(rosweb::errors::ErrorCode::ROS_SERVICE_UNAVAILABLE);
        }
        TfTreePayload tree;
        TfFrame f;
        f.name = "base_link";
        f.children = {"lidar"};
        tree.frames.push_back(f);
        return tree;
    }

    auto add_listener(std::shared_ptr<ITfListener> l) -> void override {
        listeners.push_back(std::move(l));
    }
    auto remove_listener(std::shared_ptr<ITfListener>) -> void override {}
    auto shutdown() -> void override {}
};

}  // namespace

TEST_SUITE("TfChannel") {

    TEST_CASE("channel_name returns tf") {
        auto mgr = std::make_shared<MockTfManager>();
        TfChannel ch(mgr);
        CHECK(ch.channel_name() == "tf");
    }

    TEST_CASE("subscribe-tf sends subscribed-tf confirmation") {
        auto mgr = std::make_shared<MockTfManager>();
        TfChannel ch(mgr);
        FakeConnection conn;

        WsMessage msg;
        msg.channel = "tf";
        msg.type = "subscribe-tf";
        msg.payload = nlohmann::json{{"subscriptionId", "tf_1"}};
        msg.seq = 30;
        ch.handle_message(msg, conn);

        auto parsed = parse_sent(conn.sent_text);
        REQUIRE(parsed.size() == 1);
        CHECK(parsed[0].channel == "tf");
        CHECK(parsed[0].type == "subscribed-tf");
        CHECK(parsed[0].seq.value() == 30);
        CHECK(parsed[0].payload["subscriptionId"] == "tf_1");
    }

    TEST_CASE("subscribe-tf with invalid payload sends error") {
        auto mgr = std::make_shared<MockTfManager>();
        TfChannel ch(mgr);
        FakeConnection conn;

        WsMessage msg;
        msg.channel = "tf";
        msg.type = "subscribe-tf";
        msg.payload = nlohmann::json{{"bad", "data"}};
        msg.seq = 1;
        ch.handle_message(msg, conn);

        auto parsed = parse_sent(conn.sent_text);
        REQUIRE(parsed.size() == 1);
        CHECK(parsed[0].type == "error");
        CHECK(parsed[0].payload["code"] == "INVALID_PAYLOAD");
    }

    TEST_CASE("subscribe-tf when manager fails sends error") {
        auto mgr = std::make_shared<MockTfManager>();
        mgr->subscribe_should_fail = true;
        TfChannel ch(mgr);
        FakeConnection conn;

        WsMessage msg;
        msg.channel = "tf";
        msg.type = "subscribe-tf";
        msg.payload = nlohmann::json{{"subscriptionId", "tf_1"}};
        msg.seq = 2;
        ch.handle_message(msg, conn);

        auto parsed = parse_sent(conn.sent_text);
        REQUIRE(parsed.size() == 1);
        CHECK(parsed[0].type == "error");
    }

    TEST_CASE("get-tf-tree returns tree with echoed seq") {
        auto mgr = std::make_shared<MockTfManager>();
        TfChannel ch(mgr);
        FakeConnection conn;

        WsMessage msg;
        msg.channel = "tf";
        msg.type = "get-tf-tree";
        msg.payload = nlohmann::json::object();
        msg.seq = 31;
        ch.handle_message(msg, conn);

        auto parsed = parse_sent(conn.sent_text);
        REQUIRE(parsed.size() == 1);
        CHECK(parsed[0].type == "tf-tree");
        CHECK(parsed[0].seq.value() == 31);
        CHECK(parsed[0].payload["frames"].is_array());
        CHECK(parsed[0].payload["frames"].size() == 1);
        CHECK(parsed[0].payload["frames"][0]["name"] == "base_link");
    }

    TEST_CASE("get-tf-tree when manager fails sends error") {
        auto mgr = std::make_shared<MockTfManager>();
        mgr->tree_should_fail = true;
        TfChannel ch(mgr);
        FakeConnection conn;

        WsMessage msg;
        msg.channel = "tf";
        msg.type = "get-tf-tree";
        msg.payload = nlohmann::json::object();
        msg.seq = 32;
        ch.handle_message(msg, conn);

        auto parsed = parse_sent(conn.sent_text);
        REQUIRE(parsed.size() == 1);
        CHECK(parsed[0].type == "error");
    }

    TEST_CASE("on_tf_update delivered to subscriber") {
        auto mgr = std::make_shared<MockTfManager>();
        TfChannel ch(mgr);
        FakeConnection conn;

        WsMessage sub;
        sub.channel = "tf";
        sub.type = "subscribe-tf";
        sub.payload = nlohmann::json{{"subscriptionId", "tf_1"}};
        ch.handle_message(sub, conn);
        conn.sent_text.clear();

        TfTransform t;
        t.parent = "base_link";
        t.child = "lidar";
        t.translation = {.x = 0.5, .y = 0.0, .z = 0.3};
        t.rotation = {.x = 0.0, .y = 0.0, .z = 0.0, .w = 1.0};
        t.timestamp = "1716201000";

        ch.on_tf_update("tf_1", {t});

        auto parsed = parse_sent(conn.sent_text);
        REQUIRE(parsed.size() == 1);
        CHECK(parsed[0].type == "tf-update");
        CHECK(parsed[0].payload["subscriptionId"] == "tf_1");
        CHECK(parsed[0].payload["transforms"].size() == 1);
        CHECK(parsed[0].payload["transforms"][0]["parent"] == "base_link");
    }

    TEST_CASE("on_tf_update not delivered to unsubscribed") {
        auto mgr = std::make_shared<MockTfManager>();
        TfChannel ch(mgr);
        FakeConnection conn1, conn2;

        WsMessage sub;
        sub.channel = "tf";
        sub.type = "subscribe-tf";
        sub.payload = nlohmann::json{{"subscriptionId", "tf_1"}};
        ch.handle_message(sub, conn1);

        WsMessage sub2;
        sub2.channel = "tf";
        sub2.type = "subscribe-tf";
        sub2.payload = nlohmann::json{{"subscriptionId", "tf_2"}};
        ch.handle_message(sub2, conn2);

        conn1.sent_text.clear();
        conn2.sent_text.clear();

        TfTransform t;
        t.parent = "a";
        t.child = "b";
        ch.on_tf_update("tf_1", {t});

        CHECK(conn1.sent_text.size() == 1);
        CHECK(conn2.sent_text.empty());
    }

    TEST_CASE("disconnect removes subscriptions") {
        auto mgr = std::make_shared<MockTfManager>();
        TfChannel ch(mgr);
        FakeConnection conn;

        WsMessage sub;
        sub.channel = "tf";
        sub.type = "subscribe-tf";
        sub.payload = nlohmann::json{{"subscriptionId", "tf_1"}};
        ch.handle_message(sub, conn);
        conn.sent_text.clear();

        ch.handle_disconnect(conn);

        // tf-update for tf_1 should not crash (connection removed)
        TfTransform t;
        t.parent = "a";
        t.child = "b";
        ch.on_tf_update("tf_1", {t});
        CHECK(conn.sent_text.empty());
    }

    TEST_CASE("unknown message type silently ignored") {
        auto mgr = std::make_shared<MockTfManager>();
        TfChannel ch(mgr);
        FakeConnection conn;

        WsMessage msg;
        msg.channel = "tf";
        msg.type = "unknown-type";
        msg.payload = nlohmann::json{};
        ch.handle_message(msg, conn);

        CHECK(conn.sent_text.empty());
    }

    TEST_CASE("listener registration via manager") {
        auto mgr = std::make_shared<MockTfManager>();
        auto ch = std::make_shared<TfChannel>(mgr);
        mgr->add_listener(ch);

        REQUIRE(mgr->listeners.size() == 1);
    }
}
