#include <doctest.h>
#include <nlohmann/json.hpp>

#include <string>
#include <vector>

#include "terminal/i_pty_manager.hpp"
#include "ws/terminal_channel.hpp"
#include "ws/ws_message.hpp"
#include "models/terminal_models.hpp"
#include "errors/error_codes.hpp"

using namespace rosweb::ws;
using namespace rosweb::terminal;
using namespace rosweb::models;

namespace {

struct RecordedCreate {
    PtyCreateParams params;
    std::function<void(const std::string&, std::string)> on_output;
    std::function<void(const std::string&, int)> on_exit;
};

struct RecordedWrite {
    std::string terminal_id;
    std::string data;
};

struct RecordedResize {
    std::string terminal_id;
    int cols;
    int rows;
};

class MockPtyManager : public IPtyManager {
public:
    std::vector<RecordedCreate> creates;
    std::vector<RecordedWrite> writes;
    std::vector<RecordedResize> resizes;
    std::vector<std::string> kills;
    bool close_all_called = false;
    size_t forced_active_count = 0;
    std::optional<rosweb::errors::ErrorCode> force_create_error;

    auto create(
        const PtyCreateParams& params,
        std::function<void(const std::string&, std::string)> on_output,
        std::function<void(const std::string&, int)> on_exit
    ) -> std::expected<int, rosweb::errors::ErrorCode> override {
        if (force_create_error) {
            return std::unexpected(*force_create_error);
        }
        creates.push_back({params, std::move(on_output), std::move(on_exit)});
        return static_cast<int>(creates.size()) + 9999;  // fake pid
    }

    auto write(const std::string& terminal_id, std::string_view data)
        -> std::expected<void, rosweb::errors::ErrorCode> override {
        writes.push_back({terminal_id, std::string(data)});
        return {};
    }

    auto resize(const std::string& terminal_id, int cols, int rows)
        -> std::expected<void, rosweb::errors::ErrorCode> override {
        resizes.push_back({terminal_id, cols, rows});
        return {};
    }

    auto kill(const std::string& terminal_id)
        -> std::expected<void, rosweb::errors::ErrorCode> override {
        kills.push_back(terminal_id);
        return {};
    }

    auto close_all() -> void override {
        close_all_called = true;
    }

    auto active_count() const -> size_t override {
        return forced_active_count;
    }
};

// Fake connection that captures sent text
class FakeConnection : public crow::websocket::connection {
public:
    std::vector<std::string> sent_text;
    std::vector<std::string> sent_binary;

    void send_binary(std::string msg) override { sent_binary.push_back(std::move(msg)); }
    void send_text(std::string msg) override { sent_text.push_back(std::move(msg)); }
    void send_ping(std::string) override {}
    void send_pong(std::string) override {}
    void close(std::string const&, uint16_t) override {}
    std::string get_remote_ip() override { return "127.0.0.1"; }
    std::string get_subprotocol() const override { return ""; }
};

auto parse_sent(const FakeConnection& conn) -> std::vector<WsMessage> {
    std::vector<WsMessage> result;
    for (const auto& text : conn.sent_text) {
        auto j = nlohmann::json::parse(text);
        result.push_back(j.get<WsMessage>());
    }
    return result;
}

}  // namespace

TEST_SUITE("TerminalChannel") {
    TEST_CASE("channel_name returns 'terminal'") {
        auto pty = std::make_shared<MockPtyManager>();
        TerminalChannel ch(pty);
        CHECK(ch.channel_name() == "terminal");
    }

    TEST_CASE("create — success") {
        auto pty = std::make_shared<MockPtyManager>();
        TerminalChannel ch(pty);
        FakeConnection conn;

        WsMessage msg{.channel = "terminal", .type = "create",
                      .payload = R"({"terminalId": "t1", "cols": 120, "rows": 40})"_json,
                      .seq = 1};

        ch.handle_message(msg, conn);

        REQUIRE(pty->creates.size() == 1);
        CHECK(pty->creates[0].params.terminal_id == "t1");
        CHECK(pty->creates[0].params.cols == 120);
        CHECK(pty->creates[0].params.rows == 40);

        auto sent = parse_sent(conn);
        REQUIRE(sent.size() == 1);
        CHECK(sent[0].type == "created");
        CHECK(sent[0].channel == "terminal");
        CHECK(sent[0].payload["terminalId"] == "t1");
        CHECK(sent[0].payload["pid"].get<int>() > 0);
        REQUIRE(sent[0].seq.has_value());
        CHECK(*sent[0].seq == 1);
    }

    TEST_CASE("create — limit reached sends error") {
        auto pty = std::make_shared<MockPtyManager>();
        pty->force_create_error = rosweb::errors::ErrorCode::TERMINAL_LIMIT_REACHED;
        TerminalChannel ch(pty);
        FakeConnection conn;

        WsMessage msg{.channel = "terminal", .type = "create",
                      .payload = R"({"terminalId": "t1", "cols": 80, "rows": 24})"_json,
                      .seq = 2};

        ch.handle_message(msg, conn);

        auto sent = parse_sent(conn);
        REQUIRE(sent.size() == 1);
        CHECK(sent[0].type == "error");
        CHECK(sent[0].payload["code"] == "TERMINAL_LIMIT_REACHED");
        REQUIRE(sent[0].seq.has_value());
        CHECK(*sent[0].seq == 2);
    }

    TEST_CASE("create — invalid payload sends error") {
        auto pty = std::make_shared<MockPtyManager>();
        TerminalChannel ch(pty);
        FakeConnection conn;

        WsMessage msg{.channel = "terminal", .type = "create",
                      .payload = R"({"bad_field": 123})"_json,  // missing terminalId
                      .seq = 3};

        ch.handle_message(msg, conn);

        auto sent = parse_sent(conn);
        REQUIRE(sent.size() == 1);
        CHECK(sent[0].type == "error");
        CHECK(sent[0].payload["code"] == "INVALID_PAYLOAD");
    }

    TEST_CASE("input — success") {
        auto pty = std::make_shared<MockPtyManager>();
        TerminalChannel ch(pty);
        FakeConnection conn;

        WsMessage msg{.channel = "terminal", .type = "input",
                      .payload = R"({"terminalId": "t1", "data": "ls -la\r"})"_json};

        ch.handle_message(msg, conn);

        REQUIRE(pty->writes.size() == 1);
        CHECK(pty->writes[0].terminal_id == "t1");
        CHECK(pty->writes[0].data == "ls -la\r");
        // No response sent for input
        CHECK(conn.sent_text.empty());
    }

    TEST_CASE("resize — success") {
        auto pty = std::make_shared<MockPtyManager>();
        TerminalChannel ch(pty);
        FakeConnection conn;

        WsMessage msg{.channel = "terminal", .type = "resize",
                      .payload = R"({"terminalId": "t1", "cols": 200, "rows": 50})"_json};

        ch.handle_message(msg, conn);

        REQUIRE(pty->resizes.size() == 1);
        CHECK(pty->resizes[0].terminal_id == "t1");
        CHECK(pty->resizes[0].cols == 200);
        CHECK(pty->resizes[0].rows == 50);
    }

    TEST_CASE("close — success") {
        auto pty = std::make_shared<MockPtyManager>();
        TerminalChannel ch(pty);
        FakeConnection conn;

        WsMessage msg{.channel = "terminal", .type = "close",
                      .payload = R"({"terminalId": "t1"})"_json};

        ch.handle_message(msg, conn);

        REQUIRE(pty->kills.size() == 1);
        CHECK(pty->kills[0] == "t1");
    }

    TEST_CASE("unknown type is silently ignored") {
        auto pty = std::make_shared<MockPtyManager>();
        TerminalChannel ch(pty);
        FakeConnection conn;

        WsMessage msg{.channel = "terminal", .type = "unknown_type",
                      .payload = R"({})"_json};

        ch.handle_message(msg, conn);
        CHECK(conn.sent_text.empty());
        CHECK(pty->creates.empty());
    }

    TEST_CASE("disconnect calls close_all") {
        auto pty = std::make_shared<MockPtyManager>();
        TerminalChannel ch(pty);
        FakeConnection conn;

        ch.handle_disconnect(conn);
        CHECK(pty->close_all_called);
    }

    TEST_CASE("on_output callback sends output message") {
        auto pty = std::make_shared<MockPtyManager>();
        TerminalChannel ch(pty);
        FakeConnection conn;

        WsMessage msg{.channel = "terminal", .type = "create",
                      .payload = R"({"terminalId": "t1", "cols": 80, "rows": 24})"_json,
                      .seq = 1};
        ch.handle_message(msg, conn);

        // Simulate PTY output via the captured callback
        REQUIRE(pty->creates.size() == 1);
        pty->creates[0].on_output("t1", "hello world\n");

        auto sent = parse_sent(conn);
        // First is "created", second is "output"
        REQUIRE(sent.size() == 2);
        CHECK(sent[1].type == "output");
        CHECK(sent[1].channel == "terminal");
        CHECK(sent[1].payload["terminalId"] == "t1");
        CHECK(sent[1].payload["data"] == "hello world\n");
    }

    TEST_CASE("on_exit callback sends exited message") {
        auto pty = std::make_shared<MockPtyManager>();
        TerminalChannel ch(pty);
        FakeConnection conn;

        WsMessage msg{.channel = "terminal", .type = "create",
                      .payload = R"({"terminalId": "t1", "cols": 80, "rows": 24})"_json,
                      .seq = 1};
        ch.handle_message(msg, conn);

        // Simulate PTY exit via the captured callback
        REQUIRE(pty->creates.size() == 1);
        pty->creates[0].on_exit("t1", 0);

        auto sent = parse_sent(conn);
        REQUIRE(sent.size() == 2);
        CHECK(sent[1].type == "exited");
        CHECK(sent[1].payload["terminalId"] == "t1");
        CHECK(sent[1].payload["exitCode"] == 0);
    }
}
