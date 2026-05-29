#include "api/ros_controller.hpp"

#include <crow.h>
#include <nlohmann/json.hpp>

#include <functional>
#include <string>
#include <string_view>

#include "api/rest_helpers.hpp"
#include "errors/error_codes.hpp"
#include "errors/api_exception.hpp"
#include "models/ros_models.hpp"

namespace rosweb::api {

namespace {
template<typename T>
auto ros_unwrap(std::expected<T, errors::ErrorCode> result, const std::string& context = "")
    -> T {
    if (result.has_value()) {
        return std::move(result.value());
    }
    throw errors::RosException(result.error(), context);
}
}  // namespace

RosController::RosController(std::shared_ptr<ros::IRosManager> ros_manager)
    : ros_manager_(std::move(ros_manager)) {}

void RosController::register_routes(crow::App<crow::CORSHandler>& app) {
    CROW_ROUTE(app, "/api/v1/ros/nodes")
    ([this](const crow::request& req) {
        return try_handle("GET /ros/nodes", [&] { return handle_list_nodes(req); });
    });

    CROW_ROUTE(app, "/api/v1/ros/topics")
    ([this](const crow::request& req) {
        return try_handle("GET /ros/topics", [&] { return handle_list_topics(req); });
    });

    CROW_ROUTE(app, "/api/v1/ros/services")
    ([this](const crow::request& req) {
        return try_handle("GET /ros/services", [&] { return handle_list_services(req); });
    });

    CROW_ROUTE(app, "/api/v1/ros/actions")
    ([this](const crow::request& req) {
        return try_handle("GET /ros/actions", [&] { return handle_list_actions(req); });
    });

    CROW_ROUTE(app, "/api/v1/ros/params")
    ([this](const crow::request& req) {
        return try_handle("GET /ros/params", [&] { return handle_list_params(req); });
    });

    CROW_ROUTE(app, "/api/v1/ros/params").methods("PUT"_method)
    ([this](const crow::request& req) {
        return try_handle("PUT /ros/params", [&] { return handle_set_param(req); });
    });

    CROW_ROUTE(app, "/api/v1/ros/interfaces")
    ([this](const crow::request& req) {
        return try_handle("GET /ros/interfaces", [&] { return handle_list_interfaces(req); });
    });

    CROW_ROUTE(app, "/api/v1/ros/interface-detail")
    ([this](const crow::request& req) {
        return try_handle("GET /ros/interface-detail", [&] { return handle_get_interface_detail(req); });
    });
}

auto RosController::try_handle(std::string_view /*endpoint_name*/,
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

auto RosController::handle_list_nodes(const crow::request& /*req*/) -> std::string {
    auto result = ros_unwrap(ros_manager_->list_nodes());
    nlohmann::json j = result;
    return make_success(j);
}

auto RosController::handle_list_topics(const crow::request& req) -> std::string {
    bool include_hidden = false;
    const char* hidden_param = req.url_params.get("includeHidden");
    if (hidden_param) {
        std::string val(hidden_param);
        include_hidden = (val == "true" || val == "1");
    }
    auto result = ros_unwrap(ros_manager_->list_topics(include_hidden));
    nlohmann::json j = result;
    return make_success(j);
}

auto RosController::handle_list_services(const crow::request& /*req*/) -> std::string {
    auto result = ros_unwrap(ros_manager_->list_services());
    nlohmann::json j = result;
    return make_success(j);
}

auto RosController::handle_list_actions(const crow::request& /*req*/) -> std::string {
    auto result = ros_unwrap(ros_manager_->list_actions());
    nlohmann::json j = result;
    return make_success(j);
}

auto RosController::handle_list_params(const crow::request& req) -> std::string {
    const char* node = req.url_params.get("node");
    if (!node || std::strlen(node) == 0) {
        auto [status, body] = make_error(errors::ErrorCode::ROS_NODE_NOT_FOUND,
                                          "Missing required parameter: node");
        return body;
    }
    auto result = ros_unwrap(ros_manager_->list_params(node), node);
    nlohmann::json j = result;
    return make_success(j);
}

auto RosController::handle_set_param(const crow::request& req) -> std::string {
    auto body = nlohmann::json::parse(req.body);
    auto param_req = body.get<models::RosParamSetRequest>();
    auto result = ros_unwrap(ros_manager_->set_param(param_req));
    nlohmann::json j = result;
    return make_success(j);
}

auto RosController::handle_list_interfaces(const crow::request& req) -> std::string {
    std::string kind = "all";
    std::string filter;
    const char* kind_param = req.url_params.get("kind");
    if (kind_param && std::strlen(kind_param) > 0) {
        kind = kind_param;
    }
    const char* filter_param = req.url_params.get("filter");
    if (filter_param) {
        filter = filter_param;
    }
    auto result = ros_unwrap(ros_manager_->list_interfaces(kind, filter));
    nlohmann::json j = result;
    return make_success(j);
}

auto RosController::handle_get_interface_detail(const crow::request& req) -> std::string {
    const char* type_param = req.url_params.get("type");
    if (!type_param || std::strlen(type_param) == 0) {
        auto [status, body] = make_error(errors::ErrorCode::ROS_INVALID_MESSAGE,
                                          "Missing required parameter: type");
        return body;
    }
    auto result = ros_unwrap(ros_manager_->get_interface_detail(type_param), type_param);
    nlohmann::json j = result;
    return make_success(j);
}

}  // namespace rosweb::api
