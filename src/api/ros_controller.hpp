#pragma once

#include <crow.h>
#include <crow/middlewares/cors.h>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

#include "ros/i_ros_manager.hpp"

namespace rosweb::api {

class RosController {
public:
    explicit RosController(std::shared_ptr<ros::IRosManager> ros_manager);

    void register_routes(crow::App<crow::CORSHandler>& app);

private:
    std::shared_ptr<ros::IRosManager> ros_manager_;

    auto handle_list_nodes(const crow::request& req) -> std::string;
    auto handle_list_topics(const crow::request& req) -> std::string;
    auto handle_list_services(const crow::request& req) -> std::string;
    auto handle_list_actions(const crow::request& req) -> std::string;
    auto handle_list_params(const crow::request& req) -> std::string;
    auto handle_set_param(const crow::request& req) -> std::string;
    auto handle_list_interfaces(const crow::request& req) -> std::string;
    auto handle_get_interface_detail(const crow::request& req) -> std::string;

    auto try_handle(std::string_view endpoint_name,
                    std::function<std::string()> handler) -> std::string;
};

}  // namespace rosweb::api
