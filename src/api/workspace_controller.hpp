#pragma once

#include <crow.h>
#include <crow/middlewares/cors.h>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "fs/i_filesystem.hpp"
#include "fs/path_validator.hpp"
#include "workspace/i_workspace_aware.hpp"
#include "workspace/package_discovery.hpp"

namespace rosweb::api {

class WorkspaceController {
public:
    WorkspaceController(std::shared_ptr<fs::PathValidator> validator,
                        std::shared_ptr<fs::IFileSystem> filesystem,
                        std::vector<std::shared_ptr<workspace::IWorkspaceAware>> workspace_aware);

    void register_routes(crow::App<crow::CORSHandler>& app);
    void add_workspace_aware(std::shared_ptr<workspace::IWorkspaceAware> component);
    void replace_workspace_aware(
        std::shared_ptr<workspace::IWorkspaceAware> old_component,
        std::shared_ptr<workspace::IWorkspaceAware> new_component);

private:
    std::shared_ptr<fs::PathValidator> validator_;
    std::shared_ptr<fs::IFileSystem> fs_;
    std::vector<std::shared_ptr<workspace::IWorkspaceAware>> workspace_aware_;
    workspace::PackageDiscovery package_discovery_;

    auto handle_get_workspace(const crow::request& req) -> std::string;
    auto handle_open_workspace(const crow::request& req) -> std::string;

    auto try_handle(std::string_view endpoint_name,
                    std::function<std::string()> handler) -> std::string;

    auto build_workspace_info() const -> models::WorkspaceInfo;
};

}  // namespace rosweb::api
