#include <doctest.h>

#include <nlohmann/json.hpp>

#include "models/build_models.hpp"

using namespace rosweb::models;

TEST_SUITE("BuildModels") {

    TEST_CASE("build_status_to_string") {
        CHECK(build_status_to_string(BuildStatus::running) == "running");
        CHECK(build_status_to_string(BuildStatus::completed) == "completed");
        CHECK(build_status_to_string(BuildStatus::failed) == "failed");
        CHECK(build_status_to_string(BuildStatus::cancelled) == "cancelled");
    }

    TEST_CASE("launch_status_to_string") {
        CHECK(launch_status_to_string(LaunchStatus::running) == "running");
        CHECK(launch_status_to_string(LaunchStatus::stopped) == "stopped");
    }

    TEST_CASE("BuildTargetStatus to_json") {
        BuildTargetStatus t{.name = "my_pkg", .status = BuildStatus::completed, .return_code = 0};
        nlohmann::json j = t;
        CHECK(j["name"] == "my_pkg");
        CHECK(j["status"] == "completed");
        CHECK(j["returnCode"] == 0);
    }

    TEST_CASE("BuildTargetStatus to_json without return_code") {
        BuildTargetStatus t{.name = "other", .status = BuildStatus::running, .return_code = std::nullopt};
        nlohmann::json j = t;
        CHECK(j["name"] == "other");
        CHECK(j["status"] == "running");
        CHECK(!j.contains("returnCode"));
    }

    TEST_CASE("BuildResponse to_json") {
        BuildResponse r{.build_id = "b_123_456", .status = BuildStatus::running};
        nlohmann::json j = r;
        CHECK(j["buildId"] == "b_123_456");
        CHECK(j["status"] == "running");
    }

    TEST_CASE("BuildStatusResponse to_json") {
        BuildStatusResponse r{
            .build_id = "b_1",
            .status = BuildStatus::completed,
            .targets = {BuildTargetStatus{.name = "pkg_a", .status = BuildStatus::completed, .return_code = 0}},
        };
        nlohmann::json j = r;
        CHECK(j["buildId"] == "b_1");
        CHECK(j["status"] == "completed");
        CHECK(j["targets"]["pkg_a"]["status"] == "completed");
        CHECK(j["targets"]["pkg_a"]["returnCode"] == 0);
    }

    TEST_CASE("LaunchResponse to_json") {
        LaunchResponse r{.launch_id = "l_1", .status = LaunchStatus::running, .pid = 12345};
        nlohmann::json j = r;
        CHECK(j["launchId"] == "l_1");
        CHECK(j["status"] == "running");
        CHECK(j["pid"] == 12345);
    }

    TEST_CASE("LaunchStopResponse to_json") {
        LaunchStopResponse r{.launch_id = "l_1", .status = LaunchStatus::stopped};
        nlohmann::json j = r;
        CHECK(j["launchId"] == "l_1");
        CHECK(j["status"] == "stopped");
    }

    TEST_CASE("LaunchArgument to_json with all fields") {
        LaunchArgument a{
            .name = "use_sim",
            .type = "string",
            .default_value = "false",
            .description = "Use simulation",
        };
        nlohmann::json j = a;
        CHECK(j["name"] == "use_sim");
        CHECK(j["type"] == "string");
        CHECK(j["default"] == "false");
        CHECK(j["description"] == "Use simulation");
    }

    TEST_CASE("LaunchArgument to_json without optionals") {
        LaunchArgument a{.name = "rate", .type = "int"};
        nlohmann::json j = a;
        CHECK(j["name"] == "rate");
        CHECK(j["type"] == "int");
        CHECK(!j.contains("default"));
        CHECK(!j.contains("description"));
    }

    TEST_CASE("LaunchFileInfo to_json") {
        LaunchFileInfo f{
            .path = "/ws/src/pkg/launch/demo.launch.py",
            .package = "pkg",
            .arguments = {LaunchArgument{.name = "use_sim", .type = "string"}},
        };
        nlohmann::json j = f;
        CHECK(j["path"] == "/ws/src/pkg/launch/demo.launch.py");
        CHECK(j["package"] == "pkg");
        CHECK(j["arguments"].is_array());
        CHECK(j["arguments"].size() == 1);
    }

    TEST_CASE("LaunchFilesResponse to_json") {
        LaunchFilesResponse r{
            .files = {LaunchFileInfo{.path = "/a.launch.py", .package = "p"}},
        };
        nlohmann::json j = r;
        CHECK(j["files"].is_array());
        CHECK(j["files"].size() == 1);
        CHECK(j["files"][0]["path"] == "/a.launch.py");
    }

    TEST_CASE("BuildOutputPayload to_json") {
        BuildOutputPayload p{.build_id = "b_1", .target = "pkg", .stream = "stdout", .data = "hello"};
        nlohmann::json j = p;
        CHECK(j["buildId"] == "b_1");
        CHECK(j["target"] == "pkg");
        CHECK(j["stream"] == "stdout");
        CHECK(j["data"] == "hello");
    }

    TEST_CASE("BuildOutputPayload to_json without target") {
        BuildOutputPayload p{.build_id = "b_1", .target = std::nullopt, .stream = "stderr", .data = "err"};
        nlohmann::json j = p;
        CHECK(!j.contains("target"));
    }

    TEST_CASE("LaunchOutputPayload to_json") {
        LaunchOutputPayload p{.launch_id = "l_1", .node = "/talker", .stream = "stdout", .data = "msg"};
        nlohmann::json j = p;
        CHECK(j["launchId"] == "l_1");
        CHECK(j["node"] == "/talker");
    }

    TEST_CASE("LaunchStatusPayload to_json") {
        LaunchStatusPayload p{.launch_id = "l_1", .status = LaunchStatus::stopped, .exit_code = 0};
        nlohmann::json j = p;
        CHECK(j["launchId"] == "l_1");
        CHECK(j["status"] == "stopped");
        CHECK(j["exitCode"] == 0);
    }

    // --- from_json tests ---

    TEST_CASE("BuildRequest from_json full") {
        auto j = nlohmann::json::parse(R"({
            "targets": ["pkg_a", "pkg_b"],
            "args": ["--cmake-args", "-DCMAKE_BUILD_TYPE=Release"],
            "clean": true
        })");
        auto req = j.get<BuildRequest>();
        REQUIRE(req.targets.has_value());
        CHECK(req.targets->size() == 2);
        CHECK(req.targets->at(0) == "pkg_a");
        REQUIRE(req.args.has_value());
        CHECK(req.args->size() == 2);
        CHECK(req.clean == true);
    }

    TEST_CASE("BuildRequest from_json minimal") {
        auto j = nlohmann::json::parse("{}");
        auto req = j.get<BuildRequest>();
        CHECK(!req.targets.has_value());
        CHECK(!req.args.has_value());
        CHECK(req.clean == false);
    }

    TEST_CASE("LaunchRequest from_json with arguments") {
        auto j = nlohmann::json::parse(R"({
            "package": "my_pkg",
            "file": "demo.launch.py",
            "arguments": {"use_sim": "true"}
        })");
        auto req = j.get<LaunchRequest>();
        CHECK(req.package == "my_pkg");
        CHECK(req.file == "demo.launch.py");
        REQUIRE(req.arguments.has_value());
        CHECK(req.arguments->at("use_sim") == "true");
    }

    TEST_CASE("LaunchRequest from_json without arguments") {
        auto j = nlohmann::json::parse(R"({"package": "my_pkg", "file": "demo.launch.py"})");
        auto req = j.get<LaunchRequest>();
        CHECK(req.package == "my_pkg");
        CHECK(req.file == "demo.launch.py");
        CHECK(!req.arguments.has_value());
    }

    TEST_CASE("LaunchStopRequest from_json") {
        auto j = nlohmann::json::parse(R"({"launchId": "l_123"})");
        auto req = j.get<LaunchStopRequest>();
        CHECK(req.launch_id == "l_123");
    }

}  // TEST_SUITE("BuildModels")
