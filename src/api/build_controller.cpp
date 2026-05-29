#include "api/build_controller.hpp"

#include <crow.h>
#include <nlohmann/json.hpp>

#include <functional>
#include <string>
#include <string_view>

#include "api/rest_helpers.hpp"
#include "errors/error_codes.hpp"
#include "errors/api_exception.hpp"
#include "models/build_models.hpp"

namespace rosweb::api {

namespace {
template<typename T>
auto build_unwrap(std::expected<T, errors::ErrorCode> result, const std::string& context = "")
    -> T {
    if (result.has_value()) {
        return std::move(result.value());
    }
    throw errors::BuildException(result.error(), context);
}
}  // namespace
}  // namespace

namespace rosweb::api {

BuildController::BuildController(std::shared_ptr<build::IBuildManager> build_manager)
    : build_manager_(std::move(build_manager)) {}

void BuildController::register_routes(crow::App<crow::CORSHandler>& app) {
    CROW_ROUTE(app, "/api/v1/build").methods("POST"_method)
    ([this](const crow::request& req) {
        return try_handle("POST /build", [&] { return handle_start_build(req); });
    });

    CROW_ROUTE(app, "/api/v1/build/status")
    ([this](const crow::request& req) {
        return try_handle("GET /build/status", [&] { return handle_get_build_status(req); });
    });

    CROW_ROUTE(app, "/api/v1/launch").methods("POST"_method)
    ([this](const crow::request& req) {
        return try_handle("POST /launch", [&] { return handle_start_launch(req); });
    });

    CROW_ROUTE(app, "/api/v1/launch/stop").methods("POST"_method)
    ([this](const crow::request& req) {
        return try_handle("POST /launch/stop", [&] { return handle_stop_launch(req); });
    });

    CROW_ROUTE(app, "/api/v1/launch-files")
    ([this](const crow::request& req) {
        return try_handle("GET /launch-files", [&] { return handle_get_launch_files(req); });
    });
}

auto BuildController::try_handle(std::string_view /*endpoint_name*/,
                                  std::function<std::string()> handler) -> std::string {
    try {
        return handler();
    } catch (const errors::ApiException& e) {
        auto [status, body] = make_error(e.code(), e.what());
        return body;
    } catch (const std::exception& e) {
        auto [status, body] = make_error(errors::ErrorCode::INTERNAL_ERROR, e.what());
        return body;
    }
}

auto BuildController::handle_start_build(const crow::request& req) -> std::string {
    auto body = nlohmann::json::parse(req.body);
    auto build_req = body.get<models::BuildRequest>();
    auto result = build_unwrap(build_manager_->start_build(build_req));
    nlohmann::json j = result;
    return make_success(j);
}

auto BuildController::handle_get_build_status(const crow::request& req) -> std::string {
    const char* build_id = req.url_params.get("buildId");
    if (!build_id || std::strlen(build_id) == 0) {
        auto [status, body] = make_error(errors::ErrorCode::BUILD_NOT_FOUND,
                                          "Missing required parameter: buildId");
        return body;
    }
    auto result = build_unwrap(build_manager_->get_build_status(build_id), build_id);
    nlohmann::json j = result;
    return make_success(j);
}

auto BuildController::handle_start_launch(const crow::request& req) -> std::string {
    auto body = nlohmann::json::parse(req.body);
    auto launch_req = body.get<models::LaunchRequest>();
    auto result = build_unwrap(build_manager_->start_launch(launch_req));
    nlohmann::json j = result;
    return make_success(j);
}

auto BuildController::handle_stop_launch(const crow::request& req) -> std::string {
    auto body = nlohmann::json::parse(req.body);
    auto stop_req = body.get<models::LaunchStopRequest>();
    auto result = build_unwrap(build_manager_->stop_launch(stop_req.launch_id));
    nlohmann::json j = result;
    return make_success(j);
}

auto BuildController::handle_get_launch_files(const crow::request& /*req*/) -> std::string {
    auto result = build_unwrap(build_manager_->discover_launch_files());
    nlohmann::json j = result;
    return make_success(j);
}

}  // namespace rosweb::api
