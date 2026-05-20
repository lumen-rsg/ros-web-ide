#include <doctest.h>
#include <nlohmann/json.hpp>

#include "ws/ws_message.hpp"

using namespace rosweb::ws;

TEST_SUITE("WsMessage") {
    TEST_CASE("to_json — with seq") {
        WsMessage msg{.channel = "terminal", .type = "output",
                      .payload = {{"terminalId", "t1"}, {"data", "hello"}},
                      .seq = 42};
        nlohmann::json j = msg;
        CHECK(j["channel"] == "terminal");
        CHECK(j["type"] == "output");
        CHECK(j["payload"]["terminalId"] == "t1");
        CHECK(j["seq"] == 42);
    }

    TEST_CASE("to_json — without seq") {
        WsMessage msg{.channel = "terminal", .type = "output",
                      .payload = {{"terminalId", "t1"}}};
        nlohmann::json j = msg;
        CHECK(j["channel"] == "terminal");
        CHECK_FALSE(j.contains("seq"));
    }

    TEST_CASE("from_json — full message") {
        auto j = R"({
            "channel": "terminal",
            "type": "create",
            "payload": {"terminalId": "t1", "cols": 80, "rows": 24},
            "seq": 1
        })"_json;
        auto msg = j.get<WsMessage>();
        CHECK(msg.channel == "terminal");
        CHECK(msg.type == "create");
        CHECK(msg.payload["terminalId"] == "t1");
        REQUIRE(msg.seq.has_value());
        CHECK(*msg.seq == 1);
    }

    TEST_CASE("from_json — no seq") {
        auto j = R"({
            "channel": "terminal",
            "type": "input",
            "payload": {"terminalId": "t1", "data": "ls"}
        })"_json;
        auto msg = j.get<WsMessage>();
        CHECK(msg.channel == "terminal");
        CHECK(msg.type == "input");
        CHECK_FALSE(msg.seq.has_value());
    }

    TEST_CASE("from_json — no payload field") {
        auto j = R"({
            "channel": "ros",
            "type": "node-event"
        })"_json;
        auto msg = j.get<WsMessage>();
        CHECK(msg.channel == "ros");
        CHECK(msg.type == "node-event");
        CHECK(msg.payload.is_null());
    }

    TEST_CASE("Round-trip — with seq") {
        WsMessage original{.channel = "build", .type = "build-output",
                           .payload = {{"buildId", "b1"}, {"data", "compiling..."}},
                           .seq = 5};
        nlohmann::json j = original;
        auto round_tripped = j.get<WsMessage>();
        CHECK(round_tripped.channel == original.channel);
        CHECK(round_tripped.type == original.type);
        CHECK(round_tripped.payload == original.payload);
        REQUIRE(round_tripped.seq.has_value());
        CHECK(*round_tripped.seq == 5);
    }
}
