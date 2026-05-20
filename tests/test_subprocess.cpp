#include <doctest.h>

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <vector>

#include "subprocess/subprocess_executor.hpp"
#include "subprocess/subprocess_types.hpp"

using namespace rosweb::subprocess;
using namespace rosweb::errors;

TEST_SUITE("SubprocessExecutor") {
    TEST_CASE("execute — captures stdout") {
        SubprocessExecutor executor;
        auto result = executor.execute({"echo", "hello world"});
        REQUIRE(result.has_value());
        CHECK_EQ(result->exit_code, 0);
        CHECK_EQ(result->stdout_output, "hello world\n");
    }

    TEST_CASE("execute — captures stderr") {
        SubprocessExecutor executor;
        auto result = executor.execute({"bash", "-c", "echo err >&2"});
        REQUIRE(result.has_value());
        CHECK_EQ(result->exit_code, 0);
        CHECK_EQ(result->stderr_output, "err\n");
    }

    TEST_CASE("execute — captures both stdout and stderr") {
        SubprocessExecutor executor;
        auto result = executor.execute({"bash", "-c", "echo out; echo err >&2"});
        REQUIRE(result.has_value());
        CHECK_EQ(result->exit_code, 0);
        CHECK(result->stdout_output.find("out") != std::string::npos);
        CHECK(result->stderr_output.find("err") != std::string::npos);
    }

    TEST_CASE("execute — returns non-zero exit code") {
        SubprocessExecutor executor;
        auto result = executor.execute({"bash", "-c", "exit 42"});
        REQUIRE(result.has_value());
        CHECK_EQ(result->exit_code, 42);
    }

    TEST_CASE("execute — nonexistent command returns exit code 127") {
        SubprocessExecutor executor;
        auto result = executor.execute({"nonexistent_command_xyz_123"});
        REQUIRE(result.has_value());
        CHECK_EQ(result->exit_code, 127);
    }

    TEST_CASE("execute — empty args returns error") {
        SubprocessExecutor executor;
        auto result = executor.execute({});
        CHECK_FALSE(result.has_value());
    }

    TEST_CASE("execute — respects cwd") {
        SubprocessExecutor executor;
        auto result = executor.execute({"pwd"}, 5000, "/");
        REQUIRE(result.has_value());
        CHECK_EQ(result->stdout_output, "/\n");
    }

    TEST_CASE("execute — timeout kills process") {
        SubprocessExecutor executor;
        auto result = executor.execute({"sleep", "10"}, 500);
        CHECK_FALSE(result.has_value());
        CHECK_EQ(result.error(), ErrorCode::ROS_SERVICE_TIMEOUT);
    }

    TEST_CASE("execute — large output") {
        SubprocessExecutor executor;
        auto result = executor.execute({"bash", "-c", "for i in $(seq 1 1000); do echo line_$i; done"});
        REQUIRE(result.has_value());
        CHECK_EQ(result->exit_code, 0);
        int line_count = 0;
        for (char c : result->stdout_output) {
            if (c == '\n') line_count++;
        }
        CHECK_EQ(line_count, 1000);
    }

    TEST_CASE("command_exists — finds ls") {
        CHECK(SubprocessExecutor::command_exists("ls"));
    }

    TEST_CASE("command_exists — finds echo") {
        CHECK(SubprocessExecutor::command_exists("echo"));
    }

    TEST_CASE("command_exists — returns false for nonexistent") {
        CHECK_FALSE(SubprocessExecutor::command_exists("nonexistent_cmd_xyz_42"));
    }

    TEST_CASE("start_streaming — delivers lines via callback") {
        SubprocessExecutor executor;

        std::mutex mtx;
        std::condition_variable cv;
        std::vector<std::string> lines;
        bool exited = false;
        int exit_code = -1;

        StreamCallbacks callbacks;
        callbacks.on_stdout = [&](std::string_view line) {
            std::lock_guard lock(mtx);
            lines.emplace_back(line);
            cv.notify_one();
        };
        callbacks.on_exit = [&](int code) {
            std::lock_guard lock(mtx);
            exited = true;
            exit_code = code;
            cv.notify_one();
        };

        auto handle = executor.start_streaming(
            {"bash", "-c", "echo line1; echo line2; echo line3"}, std::move(callbacks));
        REQUIRE(handle.has_value());

        // Wait for exit
        {
            std::unique_lock lock(mtx);
            cv.wait_for(lock, std::chrono::seconds(5), [&] { return exited; });
        }

        SubprocessExecutor::stop_streaming(std::move(*handle));

        CHECK(exited);
        CHECK_EQ(exit_code, 0);
        CHECK_GE(lines.size(), 3);
    }

    TEST_CASE("start_streaming + stop_streaming — clean termination") {
        SubprocessExecutor executor;

        std::mutex mtx;
        std::vector<std::string> lines;
        bool exited = false;

        StreamCallbacks callbacks;
        callbacks.on_stdout = [&](std::string_view line) {
            std::lock_guard lock(mtx);
            lines.emplace_back(line);
        };
        callbacks.on_exit = [&](int) {
            std::lock_guard lock(mtx);
            exited = true;
        };

        auto handle = executor.start_streaming(
            {"bash", "-c", "for i in $(seq 1 100); do echo tick_$i; sleep 0.01; done"},
            std::move(callbacks));
        REQUIRE(handle.has_value());

        // Let it produce a few lines
        std::this_thread::sleep_for(std::chrono::milliseconds(200));

        SubprocessExecutor::stop_streaming(std::move(*handle));

        CHECK(!lines.empty());
    }
}
