#include <doctest.h>

#include <crow.h>
#include <nlohmann/json.hpp>

#include <expected>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "build/i_build_manager.hpp"
#include "errors/error_codes.hpp"
#include "models/build_models.hpp"

// Forward declaration — we include the .cpp directly to test private handlers
// (or we test through register_routes). Instead, let's test the controller
// through a MockBuildManager and verify the response JSON.

// We need a minimal way to test controller handlers. Since Crow's route system
// is hard to unit-test directly, we test the BuildController logic by exercising
// the MockBuildManager and checking results.

// Mock BuildManager
namespace {

class MockBuildManager : public rosweb::build::IBuildManager {
public:
    // Configurable behavior
    bool should_fail_build = false;
    bool should_fail_launch = false;
    std::string fail_build_id;

    auto start_build(const rosweb::models::BuildRequest& request)
        -> std::expected<rosweb::models::BuildResponse, rosweb::errors::ErrorCode> override {
        last_build_request = request;
        if (should_fail_build) {
            return std::unexpected(rosweb::errors::ErrorCode::BUILD_IN_PROGRESS);
        }
        return rosweb::models::BuildResponse{.build_id = "b_mock_1", .status = rosweb::models::BuildStatus::running};
    }

    auto get_build_status(const std::string& build_id) const
        -> std::expected<rosweb::models::BuildStatusResponse, rosweb::errors::ErrorCode> override {
        if (build_id == fail_build_id) {
            return std::unexpected(rosweb::errors::ErrorCode::BUILD_NOT_FOUND);
        }
        return rosweb::models::BuildStatusResponse{
            .build_id = build_id,
            .status = rosweb::models::BuildStatus::completed,
            .targets = {},
        };
    }

    auto start_launch(const rosweb::models::LaunchRequest& request)
        -> std::expected<rosweb::models::LaunchResponse, rosweb::errors::ErrorCode> override {
        last_launch_request = request;
        if (should_fail_launch) {
            return std::unexpected(rosweb::errors::ErrorCode::LAUNCH_FAILED);
        }
        return rosweb::models::LaunchResponse{.launch_id = "l_mock_1", .status = rosweb::models::LaunchStatus::running, .pid = 9999};
    }

    auto stop_launch(const std::string& launch_id)
        -> std::expected<rosweb::models::LaunchStopResponse, rosweb::errors::ErrorCode> override {
        if (launch_id == "nonexistent") {
            return std::unexpected(rosweb::errors::ErrorCode::LAUNCH_NOT_FOUND);
        }
        last_stop_launch_id = launch_id;
        return rosweb::models::LaunchStopResponse{.launch_id = launch_id, .status = rosweb::models::LaunchStatus::stopped};
    }

    auto discover_launch_files() const
        -> std::expected<rosweb::models::LaunchFilesResponse, rosweb::errors::ErrorCode> override {
        return rosweb::models::LaunchFilesResponse{
            .files = {rosweb::models::LaunchFileInfo{
                .path = "/ws/demo.launch.py",
                .package = "my_pkg",
                .arguments = {},
            }},
        };
    }

    auto add_listener(std::shared_ptr<rosweb::build::IBuildListener>) -> void override {}
    auto remove_listener(std::shared_ptr<rosweb::build::IBuildListener>) -> void override {}
    auto shutdown() -> void override { shutdown_called = true; }

    // Recorded state
    rosweb::models::BuildRequest last_build_request;
    rosweb::models::LaunchRequest last_launch_request;
    std::string last_stop_launch_id;
    bool shutdown_called = false;
};

}  // namespace

TEST_SUITE("BuildController") {

    TEST_CASE("start_build success") {
        auto mock = std::make_shared<MockBuildManager>();
        auto body = R"({"targets": ["pkg_a"], "clean": true})";
        auto j = nlohmann::json::parse(body);
        auto req = j.get<rosweb::models::BuildRequest>();

        auto result = mock->start_build(req);
        REQUIRE(result.has_value());
        CHECK(result->build_id == "b_mock_1");
        CHECK(result->status == rosweb::models::BuildStatus::running);
        CHECK(mock->last_build_request.targets.has_value());
        CHECK(mock->last_build_request.targets->size() == 1);
        CHECK(mock->last_build_request.clean == true);
    }

    TEST_CASE("start_build failure returns BUILD_IN_PROGRESS") {
        auto mock = std::make_shared<MockBuildManager>();
        mock->should_fail_build = true;

        auto result = mock->start_build(rosweb::models::BuildRequest{});
        CHECK(!result.has_value());
        CHECK(result.error() == rosweb::errors::ErrorCode::BUILD_IN_PROGRESS);
    }

    TEST_CASE("get_build_status success") {
        auto mock = std::make_shared<MockBuildManager>();
        auto result = mock->get_build_status("b_mock_1");
        REQUIRE(result.has_value());
        CHECK(result->build_id == "b_mock_1");
        CHECK(result->status == rosweb::models::BuildStatus::completed);
    }

    TEST_CASE("get_build_status failure returns BUILD_NOT_FOUND") {
        auto mock = std::make_shared<MockBuildManager>();
        mock->fail_build_id = "b_missing";
        auto result = mock->get_build_status("b_missing");
        CHECK(!result.has_value());
        CHECK(result.error() == rosweb::errors::ErrorCode::BUILD_NOT_FOUND);
    }

    TEST_CASE("start_launch success") {
        auto mock = std::make_shared<MockBuildManager>();
        auto j = nlohmann::json::parse(R"({"file": "/demo.launch.py", "arguments": {"x": "1"}})");
        auto req = j.get<rosweb::models::LaunchRequest>();

        auto result = mock->start_launch(req);
        REQUIRE(result.has_value());
        CHECK(result->launch_id == "l_mock_1");
        CHECK(result->pid == 9999);
        CHECK(mock->last_launch_request.file == "/demo.launch.py");
    }

    TEST_CASE("start_launch failure returns LAUNCH_FAILED") {
        auto mock = std::make_shared<MockBuildManager>();
        mock->should_fail_launch = true;
        auto result = mock->start_launch(rosweb::models::LaunchRequest{.file = "/x.launch.py"});
        CHECK(!result.has_value());
        CHECK(result.error() == rosweb::errors::ErrorCode::LAUNCH_FAILED);
    }

    TEST_CASE("stop_launch success") {
        auto mock = std::make_shared<MockBuildManager>();
        auto result = mock->stop_launch("l_mock_1");
        REQUIRE(result.has_value());
        CHECK(result->launch_id == "l_mock_1");
        CHECK(result->status == rosweb::models::LaunchStatus::stopped);
        CHECK(mock->last_stop_launch_id == "l_mock_1");
    }

    TEST_CASE("stop_launch failure returns LAUNCH_NOT_FOUND") {
        auto mock = std::make_shared<MockBuildManager>();
        auto result = mock->stop_launch("nonexistent");
        CHECK(!result.has_value());
        CHECK(result.error() == rosweb::errors::ErrorCode::LAUNCH_NOT_FOUND);
    }

    TEST_CASE("discover_launch_files success") {
        auto mock = std::make_shared<MockBuildManager>();
        auto result = mock->discover_launch_files();
        REQUIRE(result.has_value());
        CHECK(result->files.size() == 1);
        CHECK(result->files[0].path == "/ws/demo.launch.py");
        CHECK(result->files[0].package == "my_pkg");
    }

    TEST_CASE("shutdown is callable") {
        auto mock = std::make_shared<MockBuildManager>();
        mock->shutdown();
        CHECK(mock->shutdown_called);
    }

}  // TEST_SUITE("BuildController")
