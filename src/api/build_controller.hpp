#pragma once

#include <crow.h>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

#include "build/i_build_manager.hpp"

namespace rosweb::api {

class BuildController {
public:
    explicit BuildController(std::shared_ptr<build::IBuildManager> build_manager);

    void register_routes(crow::SimpleApp& app);

private:
    std::shared_ptr<build::IBuildManager> build_manager_;

    auto handle_start_build(const crow::request& req) -> std::string;
    auto handle_get_build_status(const crow::request& req) -> std::string;
    auto handle_start_launch(const crow::request& req) -> std::string;
    auto handle_stop_launch(const crow::request& req) -> std::string;
    auto handle_get_launch_files(const crow::request& req) -> std::string;

    auto try_handle(std::string_view endpoint_name,
                    std::function<std::string()> handler) -> std::string;
};

}  // namespace rosweb::api
