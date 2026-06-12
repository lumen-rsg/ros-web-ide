#pragma once

#include <crow.h>
#include <crow/middlewares/cors.h>
#include <memory>
#include <string>
#include <functional>

#include "system/i_system_info.hpp"

namespace rosweb::api {

class SystemController {
public:
    explicit SystemController(std::shared_ptr<system::ISystemInfo> system_info);

    void set_restart_callback(std::function<void()> callback);
    void register_routes(crow::App<crow::CORSHandler>& app);

private:
    std::shared_ptr<system::ISystemInfo> system_info_;
    std::function<void()> restart_callback_;

    auto handle_get_system_info(const crow::request& req) -> std::string;
    auto handle_get_ros_env(const crow::request& req) -> std::string;
    auto handle_set_domain_id(const crow::request& req) -> std::string;

    auto try_handle(std::string_view endpoint_name,
                    std::function<std::string()> handler) -> std::string;
};

}  // namespace rosweb::api
