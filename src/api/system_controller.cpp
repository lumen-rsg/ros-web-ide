#include "api/system_controller.hpp"

#include <crow.h>
#include <nlohmann/json.hpp>

#include <functional>
#include <string>
#include <string_view>

#include "api/rest_helpers.hpp"
#include "errors/error_codes.hpp"
#include "errors/api_exception.hpp"
#include "models/system_models.hpp"

namespace rosweb::api {

SystemController::SystemController(std::shared_ptr<system::ISystemInfo> system_info)
    : system_info_(std::move(system_info)) {}

void SystemController::register_routes(crow::App<crow::CORSHandler>& app) {
    CROW_ROUTE(app, "/api/v1/system/info")
    ([this](const crow::request& req) {
        return try_handle("GET /system/info", [&] { return handle_get_system_info(req); });
    });

    CROW_ROUTE(app, "/api/v1/system/ros-env")
    ([this](const crow::request& req) {
        return try_handle("GET /system/ros-env", [&] { return handle_get_ros_env(req); });
    });
}

auto SystemController::try_handle(std::string_view endpoint_name,
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

auto SystemController::handle_get_system_info(const crow::request& /*req*/) -> std::string {
    auto info = system_info_->get_system_info();
    nlohmann::json j = info;
    return make_success(j);
}

auto SystemController::handle_get_ros_env(const crow::request& /*req*/) -> std::string {
    auto env = system_info_->get_ros_env();
    nlohmann::json j = env;
    return make_success(j);
}

}  // namespace rosweb::api
