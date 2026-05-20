#pragma once

#include <expected>
#include <string>

#include "errors/error_codes.hpp"
#include "models/ros_models.hpp"

namespace rosweb::ros {

class IRosManager {
public:
    virtual ~IRosManager() = default;

    virtual auto list_nodes()
        -> std::expected<models::RosNodesResponse, errors::ErrorCode> = 0;

    virtual auto list_topics(bool include_hidden)
        -> std::expected<models::RosTopicsResponse, errors::ErrorCode> = 0;

    virtual auto list_services()
        -> std::expected<models::RosServicesResponse, errors::ErrorCode> = 0;

    virtual auto list_actions()
        -> std::expected<models::RosActionsResponse, errors::ErrorCode> = 0;

    virtual auto list_params(const std::string& node)
        -> std::expected<models::RosParamsResponse, errors::ErrorCode> = 0;

    virtual auto set_param(const models::RosParamSetRequest& req)
        -> std::expected<models::RosParamSetResponse, errors::ErrorCode> = 0;

    virtual auto list_interfaces(const std::string& kind, const std::string& filter)
        -> std::expected<models::RosInterfacesResponse, errors::ErrorCode> = 0;

    virtual auto get_interface_detail(const std::string& type_name)
        -> std::expected<models::RosInterfaceDetailResponse, errors::ErrorCode> = 0;
};

}  // namespace rosweb::ros
