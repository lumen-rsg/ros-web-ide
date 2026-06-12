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

void SystemController::set_restart_callback(std::function<void()> callback) {
    restart_callback_ = std::move(callback);
}

void SystemController::register_routes(crow::App<crow::CORSHandler>& app) {
    CROW_ROUTE(app, "/api/v1/system/info")
    ([this](const crow::request& req) {
        return try_handle("GET /system/info", [&] { return handle_get_system_info(req); });
    });

    CROW_ROUTE(app, "/api/v1/system/ros-env")
    ([this](const crow::request& req) {
        return try_handle("GET /system/ros-env", [&] { return handle_get_ros_env(req); });
    });

    CROW_ROUTE(app, "/api/v1/system/ros-domain-id").methods("PUT"_method)
    ([this](const crow::request& req) {
        return try_handle("PUT /system/ros-domain-id", [&] { return handle_set_domain_id(req); });
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

auto SystemController::handle_set_domain_id(const crow::request& req) -> std::string {
    auto body = nlohmann::json::parse(req.body, nullptr, false);
    if (body.is_discarded()) {
        auto [status, resp] = make_error(errors::ErrorCode::INVALID_PAYLOAD,
            "Invalid JSON body");
        throw errors::ApiException(errors::ErrorCode::INVALID_PAYLOAD, "Invalid JSON body");
    }

    std::optional<int> domain_id;
    if (body.contains("domainId") && !body["domainId"].is_null()) {
        if (!body["domainId"].is_number_integer()) {
            throw errors::ApiException(errors::ErrorCode::INVALID_PAYLOAD,
                "domainId must be an integer or null");
        }
        int val = body["domainId"].get<int>();
        if (val < 0 || val > 232) {
            throw errors::ApiException(errors::ErrorCode::INVALID_PAYLOAD,
                "domainId must be between 0 and 232");
        }
        domain_id = val;
    }

    auto result = system_info_->set_domain_id(domain_id);
    if (!result.has_value()) {
        throw errors::ApiException(result.error(), "Failed to set ROS_DOMAIN_ID");
    }

    // Restart ROS subsystems to pick up new domain ID
    if (restart_callback_) {
        restart_callback_();
    }

    auto env = system_info_->get_ros_env();
    nlohmann::json j = env;
    return make_success(j);
}

}  // namespace rosweb::api
