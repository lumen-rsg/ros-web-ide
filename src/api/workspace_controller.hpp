#pragma once

#include <crow.h>
#include <crow/middlewares/cors.h>
#include <memory>
#include <string>
#include <functional>

#include "fs/i_filesystem.hpp"
#include "fs/path_validator.hpp"
#include "workspace/package_discovery.hpp"

namespace rosweb::api {

class WorkspaceController {
public:
    WorkspaceController(std::shared_ptr<fs::PathValidator> validator,
                        std::shared_ptr<fs::IFileSystem> filesystem);

    void register_routes(crow::App<crow::CORSHandler>& app);

private:
    std::shared_ptr<fs::PathValidator> validator_;
    std::shared_ptr<fs::IFileSystem> fs_;
    workspace::PackageDiscovery package_discovery_;

    auto handle_get_workspace(const crow::request& req) -> std::string;
    auto handle_open_workspace(const crow::request& req) -> std::string;

    auto try_handle(std::string_view endpoint_name,
                    std::function<std::string()> handler) -> std::string;

    auto build_workspace_info() const -> models::WorkspaceInfo;
};

}  // namespace rosweb::api
