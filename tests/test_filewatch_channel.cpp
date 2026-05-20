#include <doctest.h>
#include <nlohmann/json.hpp>

#include <mutex>
#include <string>
#include <vector>

#include "filewatch/i_filewatch_manager.hpp"
#include "models/filewatch_models.hpp"
#include "ws/i_ws_channel.hpp"
#include "ws/ws_message.hpp"
#include "ws/filewatch_channel.hpp"

using namespace rosweb::filewatch;
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

class MockFileWatchManager : public IFileWatchManager {
public:
    bool should_fail = false;
    std::string last_watch_id;
    std::string last_watch_path;
    bool last_watch_recursive = false;
    std::string last_unwatch_id;
    bool shutdown_called = false;

    auto watch(const std::string& watch_id, const std::string& path, bool recursive)
        -> std::expected<void, rosweb::errors::ErrorCode> override {
        last_watch_id = watch_id;
        last_watch_path = path;
        last_watch_recursive = recursive;
        if (should_fail) return std::unexpected(rosweb::errors::ErrorCode::FS_PATH_NOT_FOUND);
        return {};
    }

    auto unwatch(const std::string& watch_id)
        -> std::expected<void, rosweb::errors::ErrorCode> override {
        last_unwatch_id = watch_id;
        if (should_fail) return std::unexpected(rosweb::errors::ErrorCode::FS_PATH_NOT_FOUND);
        return {};
    }

    auto add_listener(std::shared_ptr<IFileWatchListener> listener) -> void override {}
    auto remove_listener(std::shared_ptr<IFileWatchListener> listener) -> void override {}
    auto shutdown() -> void override { shutdown_called = true; }
};

auto parse_response(const std::string& text) -> std::pair<std::string, nlohmann::json> {
    auto j = nlohmann::json::parse(text);
    return {j["type"].get<std::string>(), j["payload"]};
}

}  // namespace

TEST_SUITE("FileWatchChannel") {
    TEST_CASE("channel_name returns 'file-watch'") {
        auto mgr = std::make_shared<MockFileWatchManager>();
        FileWatchChannel ch(mgr);
        CHECK_EQ(ch.channel_name(), "file-watch");
    }

    TEST_CASE("watch sends watching confirmation") {
        auto mgr = std::make_shared<MockFileWatchManager>();
        FileWatchChannel ch(mgr);
        FakeConnection conn;

        WsMessage msg{.channel = "file-watch", .type = "watch",
                       .payload = {{"watchId", "w_1"}, {"path", "/some/path"}},
                       .seq = 10};
        ch.handle_message(msg, conn);

        CHECK_EQ(mgr->last_watch_id, "w_1");
        CHECK_EQ(mgr->last_watch_path, "/some/path");
        CHECK_EQ(mgr->last_watch_recursive, true);

        REQUIRE_EQ(conn.sent_text.size(), 1);
        auto [type, payload] = parse_response(conn.sent_text[0]);
        CHECK_EQ(type, "watching");
        CHECK_EQ(payload["watchId"], "w_1");
        CHECK_EQ(payload["path"], "/some/path");

        // Verify seq is echoed
        auto full = nlohmann::json::parse(conn.sent_text[0]);
        CHECK_EQ(full["seq"], 10);
    }

    TEST_CASE("watch failure sends error") {
        auto mgr = std::make_shared<MockFileWatchManager>();
        mgr->should_fail = true;
        FileWatchChannel ch(mgr);
        FakeConnection conn;

        WsMessage msg{.channel = "file-watch", .type = "watch",
                       .payload = {{"watchId", "w_1"}, {"path", "/bad"}},
                       .seq = 11};
        ch.handle_message(msg, conn);

        REQUIRE_EQ(conn.sent_text.size(), 1);
        auto [type, payload] = parse_response(conn.sent_text[0]);
        CHECK_EQ(type, "error");
    }

    TEST_CASE("on_file_changed sends changed to subscribers") {
        auto mgr = std::make_shared<MockFileWatchManager>();
        FileWatchChannel ch(mgr);
        FakeConnection conn;

        // Subscribe first
        WsMessage msg{.channel = "file-watch", .type = "watch",
                       .payload = {{"watchId", "w_1"}, {"path", "/some/path"}},
                       .seq = 1};
        ch.handle_message(msg, conn);
        conn.sent_text.clear();

        // Trigger file change event
        ch.on_file_changed("w_1", "/some/path/file.cpp", FileChangeKind::modified, std::nullopt);

        REQUIRE_EQ(conn.sent_text.size(), 1);
        auto [type, payload] = parse_response(conn.sent_text[0]);
        CHECK_EQ(type, "changed");
        CHECK_EQ(payload["watchId"], "w_1");
        CHECK_EQ(payload["path"], "/some/path/file.cpp");
        CHECK_EQ(payload["kind"], "modified");
    }

    TEST_CASE("on_file_changed with rename includes old_path") {
        auto mgr = std::make_shared<MockFileWatchManager>();
        FileWatchChannel ch(mgr);
        FakeConnection conn;

        WsMessage msg{.channel = "file-watch", .type = "watch",
                       .payload = {{"watchId", "w_1"}, {"path", "/path"}},
                       .seq = 1};
        ch.handle_message(msg, conn);
        conn.sent_text.clear();

        ch.on_file_changed("w_1", "/path/new.cpp", FileChangeKind::renamed, "/path/old.cpp");

        auto [type, payload] = parse_response(conn.sent_text[0]);
        CHECK_EQ(payload["kind"], "renamed");
        CHECK_EQ(payload["oldPath"], "/path/old.cpp");
    }

    TEST_CASE("on_file_changed not sent to unsubscribed watch") {
        auto mgr = std::make_shared<MockFileWatchManager>();
        FileWatchChannel ch(mgr);
        FakeConnection conn;

        // No watch subscription — event should be ignored
        ch.on_file_changed("w_999", "/some/path", FileChangeKind::modified, std::nullopt);
        CHECK_EQ(conn.sent_text.size(), 0);
    }

    TEST_CASE("unwatch removes subscription") {
        auto mgr = std::make_shared<MockFileWatchManager>();
        FileWatchChannel ch(mgr);
        FakeConnection conn;

        // Subscribe
        WsMessage sub{.channel = "file-watch", .type = "watch",
                       .payload = {{"watchId", "w_1"}, {"path", "/path"}},
                       .seq = 1};
        ch.handle_message(sub, conn);
        conn.sent_text.clear();

        // Unsubscribe
        WsMessage unsub{.channel = "file-watch", .type = "unwatch",
                         .payload = {{"watchId", "w_1"}},
                         .seq = 2};
        ch.handle_message(unsub, conn);

        CHECK_EQ(mgr->last_unwatch_id, "w_1");

        // Events for w_1 should no longer be delivered
        ch.on_file_changed("w_1", "/path/file.cpp", FileChangeKind::modified, std::nullopt);
        CHECK_EQ(conn.sent_text.size(), 0);  // Only unwatch confirmation, no changed event
    }

    TEST_CASE("disconnect cleans up subscriptions") {
        auto mgr = std::make_shared<MockFileWatchManager>();
        FileWatchChannel ch(mgr);
        FakeConnection conn;

        WsMessage msg{.channel = "file-watch", .type = "watch",
                       .payload = {{"watchId", "w_1"}, {"path", "/path"}},
                       .seq = 1};
        ch.handle_message(msg, conn);
        conn.sent_text.clear();

        ch.handle_disconnect(conn);

        // No events should be sent after disconnect
        ch.on_file_changed("w_1", "/path/file.cpp", FileChangeKind::modified, std::nullopt);
        CHECK_EQ(conn.sent_text.size(), 0);
    }

    TEST_CASE("unknown message type is silently ignored") {
        auto mgr = std::make_shared<MockFileWatchManager>();
        FileWatchChannel ch(mgr);
        FakeConnection conn;

        WsMessage msg{.channel = "file-watch", .type = "unknown_type",
                       .payload = {}, .seq = 1};
        ch.handle_message(msg, conn);
        CHECK_EQ(conn.sent_text.size(), 0);
    }
}
