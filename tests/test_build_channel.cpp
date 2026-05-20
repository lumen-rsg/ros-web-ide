#include <doctest.h>

#include <crow.h>
#include <nlohmann/json.hpp>

#include <memory>
#include <string>
#include <vector>

#include "build/i_build_manager.hpp"
#include "models/build_models.hpp"
#include "ws/ws_message.hpp"
#include "ws/build_channel.hpp"

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
    -> std::vector<rosweb::ws::WsMessage> {
    std::vector<rosweb::ws::WsMessage> result;
    for (const auto& m : msgs) {
        auto j = nlohmann::json::parse(m);
        result.push_back(j.get<rosweb::ws::WsMessage>());
    }
    return result;
}

class MockBuildManager : public rosweb::build::IBuildManager {
public:
    auto start_build(const rosweb::models::BuildRequest&)
        -> std::expected<rosweb::models::BuildResponse, rosweb::errors::ErrorCode> override {
        return rosweb::models::BuildResponse{};
    }
    auto get_build_status(const std::string&) const
        -> std::expected<rosweb::models::BuildStatusResponse, rosweb::errors::ErrorCode> override {
        return rosweb::models::BuildStatusResponse{};
    }
    auto start_launch(const rosweb::models::LaunchRequest&)
        -> std::expected<rosweb::models::LaunchResponse, rosweb::errors::ErrorCode> override {
        return rosweb::models::LaunchResponse{};
    }
    auto stop_launch(const std::string&)
        -> std::expected<rosweb::models::LaunchStopResponse, rosweb::errors::ErrorCode> override {
        return rosweb::models::LaunchStopResponse{};
    }
    auto discover_launch_files() const
        -> std::expected<rosweb::models::LaunchFilesResponse, rosweb::errors::ErrorCode> override {
        return rosweb::models::LaunchFilesResponse{};
    }
    auto add_listener(std::shared_ptr<rosweb::build::IBuildListener> l) -> void override {
        listeners.push_back(l);
    }
    auto remove_listener(std::shared_ptr<rosweb::build::IBuildListener> l) -> void override {
        auto it = std::find(listeners.begin(), listeners.end(), l);
        if (it != listeners.end()) listeners.erase(it);
    }
    auto shutdown() -> void override {}

    std::vector<std::shared_ptr<rosweb::build::IBuildListener>> listeners;
};

}  // namespace

TEST_SUITE("BuildChannel") {

    TEST_CASE("channel_name returns 'build'") {
        auto mock = std::make_shared<MockBuildManager>();
        auto channel = std::make_shared<rosweb::ws::BuildChannel>(mock);
        CHECK(channel->channel_name() == "build");
    }

    TEST_CASE("subscribe to build_id") {
        auto mock = std::make_shared<MockBuildManager>();
        auto channel = std::make_shared<rosweb::ws::BuildChannel>(mock);
        FakeConnection conn;

        auto msg = rosweb::ws::WsMessage{
            .channel = "build",
            .type = "subscribe",
            .payload = nlohmann::json{{"buildId", "b_1"}},
            .seq = std::nullopt,
        };
        channel->handle_message(msg, conn);

        // Trigger build output and verify it reaches the subscriber
        channel->on_build_output("b_1", "", "stdout", "hello");
        auto sent = parse_sent(conn.sent_text);
        CHECK(sent.size() == 1);
        CHECK(sent[0].type == "build-output");
        CHECK(sent[0].payload["buildId"] == "b_1");
        CHECK(sent[0].payload["data"] == "hello");
    }

    TEST_CASE("subscribe globally receives all events") {
        auto mock = std::make_shared<MockBuildManager>();
        auto channel = std::make_shared<rosweb::ws::BuildChannel>(mock);
        FakeConnection conn;

        auto msg = rosweb::ws::WsMessage{
            .channel = "build",
            .type = "subscribe",
            .payload = nlohmann::json::object(),
            .seq = std::nullopt,
        };
        channel->handle_message(msg, conn);

        channel->on_build_output("b_any", "", "stdout", "data");
        auto sent = parse_sent(conn.sent_text);
        CHECK(sent.size() == 1);
        CHECK(sent[0].type == "build-output");
    }

    TEST_CASE("unsubscribe stops events") {
        auto mock = std::make_shared<MockBuildManager>();
        auto channel = std::make_shared<rosweb::ws::BuildChannel>(mock);
        FakeConnection conn;

        // Subscribe
        auto sub_msg = rosweb::ws::WsMessage{
            .channel = "build",
            .type = "subscribe",
            .payload = nlohmann::json{{"buildId", "b_1"}},
            .seq = std::nullopt,
        };
        channel->handle_message(sub_msg, conn);

        // Unsubscribe
        auto unsub_msg = rosweb::ws::WsMessage{
            .channel = "build",
            .type = "unsubscribe",
            .payload = nlohmann::json{{"buildId", "b_1"}},
            .seq = std::nullopt,
        };
        channel->handle_message(unsub_msg, conn);

        conn.sent_text.clear();
        channel->on_build_output("b_1", "", "stdout", "should not receive");
        CHECK(conn.sent_text.empty());
    }

    TEST_CASE("disconnect removes from all subscriptions") {
        auto mock = std::make_shared<MockBuildManager>();
        auto channel = std::make_shared<rosweb::ws::BuildChannel>(mock);
        FakeConnection conn;

        auto sub_msg = rosweb::ws::WsMessage{
            .channel = "build",
            .type = "subscribe",
            .payload = nlohmann::json{{"buildId", "b_1"}},
            .seq = std::nullopt,
        };
        channel->handle_message(sub_msg, conn);
        channel->handle_disconnect(conn);

        conn.sent_text.clear();
        channel->on_build_output("b_1", "", "stdout", "should not receive");
        CHECK(conn.sent_text.empty());
    }

    TEST_CASE("build status change event") {
        auto mock = std::make_shared<MockBuildManager>();
        auto channel = std::make_shared<rosweb::ws::BuildChannel>(mock);
        FakeConnection conn;

        auto sub_msg = rosweb::ws::WsMessage{
            .channel = "build",
            .type = "subscribe",
            .payload = nlohmann::json{{"buildId", "b_1"}},
            .seq = std::nullopt,
        };
        channel->handle_message(sub_msg, conn);

        channel->on_build_status_changed("b_1", rosweb::models::BuildStatus::completed, {});
        auto sent = parse_sent(conn.sent_text);
        CHECK(sent.size() == 1);
        CHECK(sent[0].type == "build-status");
        CHECK(sent[0].payload["status"] == "completed");
    }

    TEST_CASE("launch output event") {
        auto mock = std::make_shared<MockBuildManager>();
        auto channel = std::make_shared<rosweb::ws::BuildChannel>(mock);
        FakeConnection conn;

        auto sub_msg = rosweb::ws::WsMessage{
            .channel = "build",
            .type = "subscribe",
            .payload = nlohmann::json{{"launchId", "l_1"}},
            .seq = std::nullopt,
        };
        channel->handle_message(sub_msg, conn);

        channel->on_launch_output("l_1", "/talker", "stdout", "Hello");
        auto sent = parse_sent(conn.sent_text);
        CHECK(sent.size() == 1);
        CHECK(sent[0].type == "launch-output");
        CHECK(sent[0].payload["launchId"] == "l_1");
        CHECK(sent[0].payload["node"] == "/talker");
    }

    TEST_CASE("launch status change event") {
        auto mock = std::make_shared<MockBuildManager>();
        auto channel = std::make_shared<rosweb::ws::BuildChannel>(mock);
        FakeConnection conn;

        auto sub_msg = rosweb::ws::WsMessage{
            .channel = "build",
            .type = "subscribe",
            .payload = nlohmann::json{{"launchId", "l_1"}},
            .seq = std::nullopt,
        };
        channel->handle_message(sub_msg, conn);

        channel->on_launch_status_changed("l_1", rosweb::models::LaunchStatus::stopped, 0);
        auto sent = parse_sent(conn.sent_text);
        CHECK(sent.size() == 1);
        CHECK(sent[0].type == "launch-status");
        CHECK(sent[0].payload["status"] == "stopped");
        CHECK(sent[0].payload["exitCode"] == 0);
    }

    TEST_CASE("events not sent to unsubscribed build_id") {
        auto mock = std::make_shared<MockBuildManager>();
        auto channel = std::make_shared<rosweb::ws::BuildChannel>(mock);
        FakeConnection conn;

        auto sub_msg = rosweb::ws::WsMessage{
            .channel = "build",
            .type = "subscribe",
            .payload = nlohmann::json{{"buildId", "b_1"}},
            .seq = std::nullopt,
        };
        channel->handle_message(sub_msg, conn);

        channel->on_build_output("b_2", "", "stdout", "other build");
        CHECK(conn.sent_text.empty());
    }

    TEST_CASE("unknown message type is silently ignored") {
        auto mock = std::make_shared<MockBuildManager>();
        auto channel = std::make_shared<rosweb::ws::BuildChannel>(mock);
        FakeConnection conn;

        auto msg = rosweb::ws::WsMessage{
            .channel = "build",
            .type = "unknown_type",
            .payload = nlohmann::json::object(),
            .seq = std::nullopt,
        };
        channel->handle_message(msg, conn);
        CHECK(conn.sent_text.empty());
    }

    TEST_CASE("listener registration via manager") {
        auto mock = std::make_shared<MockBuildManager>();
        auto channel = std::make_shared<rosweb::ws::BuildChannel>(mock);
        CHECK(mock->listeners.empty());
        mock->add_listener(channel);
        CHECK(mock->listeners.size() == 1);
    }

    TEST_CASE("listener removal via manager") {
        auto mock = std::make_shared<MockBuildManager>();
        auto channel = std::make_shared<rosweb::ws::BuildChannel>(mock);
        mock->add_listener(channel);
        CHECK(mock->listeners.size() == 1);
        mock->remove_listener(channel);
        CHECK(mock->listeners.empty());
    }

}  // TEST_SUITE("BuildChannel")
