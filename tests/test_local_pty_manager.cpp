#include <doctest.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "terminal/local_pty_manager.hpp"
#include "errors/error_codes.hpp"

using namespace rosweb::terminal;

namespace {

struct OutputCollector {
    std::mutex mutex;
    std::condition_variable cv;
    std::vector<std::string> chunks;
    std::atomic<bool> exited{false};
    int exit_code{-1};

    void on_output(const std::string& /*id*/, std::string data) {
        std::lock_guard lock(mutex);
        chunks.push_back(std::move(data));
        cv.notify_all();
    }

    void on_exit(const std::string& /*id*/, int code) {
        std::lock_guard lock(mutex);
        exit_code = code;
        exited = true;
        cv.notify_all();
    }

    auto wait_for_output(size_t min_size = 1, int timeout_ms = 3000) -> bool {
        std::unique_lock lock(mutex);
        return cv.wait_for(lock, std::chrono::milliseconds(timeout_ms), [&] {
            return chunks.size() >= min_size;
        });
    }

    auto wait_for_exit(int timeout_ms = 3000) -> bool {
        std::unique_lock lock(mutex);
        return cv.wait_for(lock, std::chrono::milliseconds(timeout_ms), [&] {
            return exited.load();
        });
    }

    auto all_output() -> std::string {
        std::lock_guard lock(mutex);
        std::string result;
        for (const auto& chunk : chunks) {
            result += chunk;
        }
        return result;
    }
};

}  // namespace

TEST_SUITE("LocalPtyManager") {
    TEST_CASE("create and exit") {
        LocalPtyManager mgr(".");
        OutputCollector collector;

        PtyCreateParams params{
            .terminal_id = "t1",
            .shell = "/bin/sh",
            .cols = 80,
            .rows = 24
        };
        auto result = mgr.create(params,
            [&](const std::string& id, std::string data) { collector.on_output(id, data); },
            [&](const std::string& id, int code) { collector.on_exit(id, code); });

        REQUIRE(result.has_value());
        CHECK(result.value() > 0);

        // Wait for shell prompt or any output
        CHECK(collector.wait_for_output());

        // Close the terminal
        auto kill_result = mgr.kill("t1");
        CHECK(kill_result.has_value());

        CHECK(collector.wait_for_exit());
    }

    TEST_CASE("write and read output") {
        LocalPtyManager mgr(".");
        OutputCollector collector;

        PtyCreateParams params{
            .terminal_id = "t2",
            .shell = "/bin/sh",
            .cols = 80,
            .rows = 24
        };
        auto result = mgr.create(params,
            [&](const std::string& id, std::string data) { collector.on_output(id, data); },
            [&](const std::string& id, int code) { collector.on_exit(id, code); });
        REQUIRE(result.has_value());

        // Wait for initial shell prompt
        REQUIRE(collector.wait_for_output(1, 3000));

        // Send a command
        auto write_result = mgr.write("t2", "echo hello_world\n");
        CHECK(write_result.has_value());

        // Wait for output to contain our text (may take several chunks)
        bool found = false;
        for (int i = 0; i < 30 && !found; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            found = collector.all_output().find("hello_world") != std::string::npos;
        }
        CHECK(found);

        mgr.kill("t2");
    }

    TEST_CASE("terminal not found on write") {
        LocalPtyManager mgr(".");
        auto result = mgr.write("nonexistent", "data");
        CHECK_FALSE(result.has_value());
        CHECK(result.error() == rosweb::errors::ErrorCode::TERMINAL_NOT_FOUND);
    }

    TEST_CASE("terminal not found on kill") {
        LocalPtyManager mgr(".");
        auto result = mgr.kill("nonexistent");
        CHECK_FALSE(result.has_value());
        CHECK(result.error() == rosweb::errors::ErrorCode::TERMINAL_NOT_FOUND);
    }

    TEST_CASE("resize terminal") {
        LocalPtyManager mgr(".");
        OutputCollector collector;

        PtyCreateParams params{
            .terminal_id = "t3",
            .shell = "/bin/sh",
            .cols = 80,
            .rows = 24
        };
        auto result = mgr.create(params,
            [&](const std::string& id, std::string data) { collector.on_output(id, data); },
            [&](const std::string& id, int code) { collector.on_exit(id, code); });
        REQUIRE(result.has_value());

        // Wait for shell to be ready
        REQUIRE(collector.wait_for_output(1, 3000));

        auto resize_result = mgr.resize("t3", 120, 40);
        CHECK(resize_result.has_value());

        mgr.kill("t3");
    }

    TEST_CASE("resize non-existent terminal") {
        LocalPtyManager mgr(".");
        auto result = mgr.resize("nonexistent", 120, 40);
        CHECK_FALSE(result.has_value());
        CHECK(result.error() == rosweb::errors::ErrorCode::TERMINAL_NOT_FOUND);
    }

    TEST_CASE("max terminal limit") {
        LocalPtyManager mgr(".");

        auto on_out = [&](const std::string&, std::string) {};
        auto on_ex = [&](const std::string&, int) {};

        // Create MAX_TERMINALS terminals
        for (size_t i = 0; i < IPtyManager::MAX_TERMINALS; ++i) {
            PtyCreateParams params{
                .terminal_id = "limit_" + std::to_string(i),
                .shell = "/bin/sh",
                .cols = 80,
                .rows = 24
            };
            auto result = mgr.create(params, on_out, on_ex);
            REQUIRE(result.has_value());
        }

        CHECK(mgr.active_count() == IPtyManager::MAX_TERMINALS);

        // The 11th should fail
        PtyCreateParams params{.terminal_id = "limit_overflow", .cols = 80, .rows = 24};
        auto result = mgr.create(params, on_out, on_ex);
        CHECK_FALSE(result.has_value());
        CHECK(result.error() == rosweb::errors::ErrorCode::TERMINAL_LIMIT_REACHED);

        mgr.close_all();
        CHECK(mgr.active_count() == 0);
    }

    TEST_CASE("close_all cleans up") {
        LocalPtyManager mgr(".");
        OutputCollector collector;

        PtyCreateParams p1{.terminal_id = "ca1", .shell = "/bin/sh", .cols = 80, .rows = 24};
        PtyCreateParams p2{.terminal_id = "ca2", .shell = "/bin/sh", .cols = 80, .rows = 24};

        REQUIRE(mgr.create(p1,
            [&](const std::string& id, std::string data) { collector.on_output(id, data); },
            [&](const std::string& id, int code) { collector.on_exit(id, code); }).has_value());
        REQUIRE(mgr.create(p2,
            [&](const std::string& id, std::string data) { collector.on_output(id, data); },
            [&](const std::string& id, int code) { collector.on_exit(id, code); }).has_value());
        CHECK(mgr.active_count() == 2);

        // Wait for shells to start and produce output
        REQUIRE(collector.wait_for_output(2, 3000));

        mgr.close_all();
        CHECK(mgr.active_count() == 0);
    }

    TEST_CASE("exit code propagated") {
        LocalPtyManager mgr(".");
        OutputCollector collector;

        PtyCreateParams params{
            .terminal_id = "exit_test",
            .shell = "/bin/sh",
            .cols = 80,
            .rows = 24
        };
        auto result = mgr.create(params,
            [&](const std::string& id, std::string data) { collector.on_output(id, data); },
            [&](const std::string& id, int code) { collector.on_exit(id, code); });
        REQUIRE(result.has_value());

        // Wait for shell to start
        REQUIRE(collector.wait_for_output(1, 3000));

        // Exit with code 42
        mgr.write("exit_test", "exit 42\n");

        CHECK(collector.wait_for_exit(3000));
        CHECK(collector.exit_code == 42);
    }
}
