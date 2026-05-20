#include <doctest.h>
#include <nlohmann/json.hpp>

#include "models/terminal_models.hpp"

using namespace rosweb::models;

TEST_SUITE("Terminal models") {
    TEST_CASE("TerminalCreatedPayload serialization") {
        TerminalCreatedPayload p{.terminal_id = "term_1", .pid = 12345};
        nlohmann::json j = p;
        CHECK(j["terminalId"] == "term_1");
        CHECK(j["pid"] == 12345);
        CHECK(j.size() == 2);
    }

    TEST_CASE("TerminalOutputPayload serialization") {
        TerminalOutputPayload p{.terminal_id = "term_1", .data = "\033[32mhello\033[0m"};
        nlohmann::json j = p;
        CHECK(j["terminalId"] == "term_1");
        CHECK(j["data"] == "\033[32mhello\033[0m");
    }

    TEST_CASE("TerminalExitedPayload serialization") {
        TerminalExitedPayload p{.terminal_id = "term_1", .exit_code = 0};
        nlohmann::json j = p;
        CHECK(j["terminalId"] == "term_1");
        CHECK(j["exitCode"] == 0);
    }

    TEST_CASE("WsErrorPayload serialization") {
        WsErrorPayload p{.code = "TERMINAL_NOT_FOUND", .message = "No such terminal"};
        nlohmann::json j = p;
        CHECK(j["code"] == "TERMINAL_NOT_FOUND");
        CHECK(j["message"] == "No such terminal");
    }

    TEST_CASE("TerminalCreatePayload deserialization — full") {
        nlohmann::json j = R"({
            "terminalId": "term_1",
            "shell": "/bin/zsh",
            "cwd": "/home/user",
            "env": {"FOO": "bar"},
            "cols": 120,
            "rows": 40
        })"_json;
        auto p = j.get<TerminalCreatePayload>();
        CHECK(p.terminal_id == "term_1");
        REQUIRE(p.shell.has_value());
        CHECK(*p.shell == "/bin/zsh");
        REQUIRE(p.cwd.has_value());
        CHECK(*p.cwd == "/home/user");
        REQUIRE(p.env.has_value());
        CHECK(p.env->at("FOO") == "bar");
        CHECK(p.cols == 120);
        CHECK(p.rows == 40);
    }

    TEST_CASE("TerminalCreatePayload deserialization — minimal") {
        nlohmann::json j = R"({"terminalId": "t2"})"_json;
        auto p = j.get<TerminalCreatePayload>();
        CHECK(p.terminal_id == "t2");
        CHECK_FALSE(p.shell.has_value());
        CHECK_FALSE(p.cwd.has_value());
        CHECK_FALSE(p.env.has_value());
        CHECK(p.cols == 80);
        CHECK(p.rows == 24);
    }

    TEST_CASE("TerminalInputPayload deserialization") {
        nlohmann::json j = R"({"terminalId": "t1", "data": "ls -la\r"})"_json;
        auto p = j.get<TerminalInputPayload>();
        CHECK(p.terminal_id == "t1");
        CHECK(p.data == "ls -la\r");
    }

    TEST_CASE("TerminalResizePayload deserialization") {
        nlohmann::json j = R"({"terminalId": "t1", "cols": 200, "rows": 50})"_json;
        auto p = j.get<TerminalResizePayload>();
        CHECK(p.terminal_id == "t1");
        CHECK(p.cols == 200);
        CHECK(p.rows == 50);
    }

    TEST_CASE("TerminalClosePayload deserialization") {
        nlohmann::json j = R"({"terminalId": "t1"})"_json;
        auto p = j.get<TerminalClosePayload>();
        CHECK(p.terminal_id == "t1");
    }
}
