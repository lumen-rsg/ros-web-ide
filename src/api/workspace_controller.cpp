#include "api/workspace_controller.hpp"

#include <crow.h>
#include <nlohmann/json.hpp>

#include <cstdlib>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include "api/rest_helpers.hpp"
#include "errors/error_codes.hpp"
#include "errors/api_exception.hpp"
#include "models/workspace_models.hpp"

namespace rosweb::api {

WorkspaceController::WorkspaceController(
    std::shared_ptr<fs::PathValidator> validator,
    std::shared_ptr<fs::IFileSystem> filesystem,
    std::vector<std::shared_ptr<workspace::IWorkspaceAware>> workspace_aware)
    : validator_(std::move(validator)),
      fs_(std::move(filesystem)),
      workspace_aware_(std::move(workspace_aware)) {}

void WorkspaceController::add_workspace_aware(
    std::shared_ptr<workspace::IWorkspaceAware> component) {
    workspace_aware_.push_back(std::move(component));
}

void WorkspaceController::replace_workspace_aware(
    std::shared_ptr<workspace::IWorkspaceAware> old_component,
    std::shared_ptr<workspace::IWorkspaceAware> new_component) {
    for (auto& comp : workspace_aware_) {
        if (comp.get() == old_component.get()) {
            comp = std::move(new_component);
            return;
        }
    }
}

void WorkspaceController::register_routes(crow::App<crow::CORSHandler>& app) {
    CROW_ROUTE(app, "/api/v1/workspace")
    ([this](const crow::request& req) {
        return try_handle("GET /workspace", [&] { return handle_get_workspace(req); });
    });

    CROW_ROUTE(app, "/api/v1/workspace/open").methods("POST"_method)
    ([this](const crow::request& req) {
        return try_handle("POST /workspace/open", [&] { return handle_open_workspace(req); });
    });
}

auto WorkspaceController::try_handle(std::string_view endpoint_name,
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

auto WorkspaceController::handle_get_workspace(const crow::request& /*req*/) -> std::string {
    auto info = build_workspace_info();
    nlohmann::json j = info;
    return make_success(j);
}

auto WorkspaceController::handle_open_workspace(const crow::request& req) -> std::string {
    auto body = nlohmann::json::parse(req.body);
    std::string path = body.at("path").get<std::string>();

    // Validate that the path exists and is a directory
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::is_directory(path, ec)) {
        throw errors::FsException(errors::ErrorCode::WS_INVALID_PATH, path,
                                  "Path does not exist or is not a directory");
    }

    // Switch workspace root
    validator_->set_workspace_root(path);
    for (const auto& component : workspace_aware_) {
        component->set_workspace_root(validator_->workspace_root());
    }

    auto info = build_workspace_info();
    nlohmann::json j = info;
    return make_success(j);
}

auto WorkspaceController::build_workspace_info() const -> models::WorkspaceInfo {
    models::WorkspaceInfo info;
    info.rootPath = validator_->workspace_root();

    // Extract workspace name from path
    namespace fs = std::filesystem;
    info.name = fs::path(info.rootPath).filename().string();
    if (info.name.empty()) {
        // Trailing slash case — get parent filename
        info.name = fs::path(info.rootPath).parent_path().filename().string();
    }

    // Detect ROS distro from environment
    const char* distro = std::getenv("ROS_DISTRO");
    if (distro && distro[0] != '\0') {
        info.rosDistro = distro;
    }

    // Discover ROS2 packages
    info.packages = package_discovery_.discover_packages(info.rootPath);

    return info;
}

}  // namespace rosweb::api
