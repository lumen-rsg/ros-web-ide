#include <doctest.h>
#include <nlohmann/json.hpp>

#include <string>
#include <vector>

#include "ws/ws_router.hpp"
#include "ws/ws_message.hpp"

using namespace rosweb::ws;

namespace {

class MockChannel : public IWsChannel {
public:
    std::string name_;
    std::vector<WsMessage> received_messages;
    int disconnect_count = 0;

    explicit MockChannel(std::string name) : name_(std::move(name)) {}

    auto channel_name() const -> std::string_view override { return name_; }

    void handle_message(const WsMessage& msg,
                        crow::websocket::connection& /*conn*/) override {
        received_messages.push_back(msg);
    }

    void handle_disconnect(crow::websocket::connection& /*conn*/) override {
        disconnect_count++;
    }
};

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

}  // namespace

TEST_SUITE("WsRouter") {
    TEST_CASE("dispatches to correct channel") {
        WsRouter router;
        auto term_ch = std::make_unique<MockChannel>("terminal");
        auto* term_ptr = term_ch.get();
        router.register_channel(std::move(term_ch));

        FakeConnection conn;
        std::string msg_str = R"({
            "channel": "terminal",
            "type": "create",
            "payload": {"terminalId": "t1"},
            "seq": 1
        })";

        router.on_message(conn, msg_str, false);

        REQUIRE(term_ptr->received_messages.size() == 1);
        CHECK(term_ptr->received_messages[0].type == "create");
        CHECK(term_ptr->received_messages[0].payload["terminalId"] == "t1");
    }

    TEST_CASE("unknown channel is silently ignored") {
        WsRouter router;
        auto term_ch = std::make_unique<MockChannel>("terminal");
        auto* term_ptr = term_ch.get();
        router.register_channel(std::move(term_ch));

        FakeConnection conn;
        std::string msg_str = R"({
            "channel": "unknown_channel",
            "type": "something",
            "payload": {}
        })";

        router.on_message(conn, msg_str, false);
        CHECK(term_ptr->received_messages.empty());
        CHECK(conn.sent_text.empty());
    }

    TEST_CASE("malformed JSON sends error") {
        WsRouter router;
        FakeConnection conn;

        router.on_message(conn, "not json{{{", false);

        REQUIRE(conn.sent_text.size() == 1);
        auto j = nlohmann::json::parse(conn.sent_text[0]);
        CHECK(j["type"] == "error");
        CHECK(j["payload"]["code"] == "INVALID_PAYLOAD");
    }

    TEST_CASE("binary messages are ignored") {
        WsRouter router;
        auto term_ch = std::make_unique<MockChannel>("terminal");
        auto* term_ptr = term_ch.get();
        router.register_channel(std::move(term_ch));

        FakeConnection conn;
        router.on_message(conn, "binary data", true);
        CHECK(term_ptr->received_messages.empty());
    }

    TEST_CASE("on_close notifies all channels") {
        WsRouter router;
        auto ch1 = std::make_unique<MockChannel>("terminal");
        auto ch2 = std::make_unique<MockChannel>("ros");
        auto* p1 = ch1.get();
        auto* p2 = ch2.get();
        router.register_channel(std::move(ch1));
        router.register_channel(std::move(ch2));

        FakeConnection conn;
        router.on_close(conn, "quit", 1000);

        CHECK(p1->disconnect_count == 1);
        CHECK(p2->disconnect_count == 1);
    }

    TEST_CASE("multiple channels dispatch independently") {
        WsRouter router;
        auto term_ch = std::make_unique<MockChannel>("terminal");
        auto ros_ch = std::make_unique<MockChannel>("ros");
        auto* term_ptr = term_ch.get();
        auto* ros_ptr = ros_ch.get();
        router.register_channel(std::move(term_ch));
        router.register_channel(std::move(ros_ch));

        FakeConnection conn;

        router.on_message(conn, R"({"channel": "terminal", "type": "create", "payload": {}})", false);
        router.on_message(conn, R"({"channel": "ros", "type": "subscribe-topic", "payload": {}})", false);

        CHECK(term_ptr->received_messages.size() == 1);
        CHECK(term_ptr->received_messages[0].type == "create");
        CHECK(ros_ptr->received_messages.size() == 1);
        CHECK(ros_ptr->received_messages[0].type == "subscribe-topic");
    }
}
