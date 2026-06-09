#pragma once

#include <mutex>
#include <string>
#include <vector>

#include "ros/i_ros_manager.hpp"
#include "subprocess/subprocess_executor.hpp"
#include "workspace/i_workspace_aware.hpp"

namespace rosweb::ros {

class LocalRosManager : public IRosManager, public workspace::IWorkspaceAware {
public:
    explicit LocalRosManager(std::string workspace_root = ".");

    LocalRosManager(const LocalRosManager&) = delete;
    auto operator=(const LocalRosManager&) -> LocalRosManager& = delete;

    void set_workspace_root(const std::string& root) override;

    auto list_nodes()
        -> std::expected<models::RosNodesResponse, errors::ErrorCode> override;

    auto list_topics(bool include_hidden)
        -> std::expected<models::RosTopicsResponse, errors::ErrorCode> override;

    auto list_services()
        -> std::expected<models::RosServicesResponse, errors::ErrorCode> override;

    auto list_actions()
        -> std::expected<models::RosActionsResponse, errors::ErrorCode> override;

    auto list_params(const std::string& node)
        -> std::expected<models::RosParamsResponse, errors::ErrorCode> override;

    auto set_param(const models::RosParamSetRequest& req)
        -> std::expected<models::RosParamSetResponse, errors::ErrorCode> override;

    auto list_interfaces(const std::string& kind, const std::string& filter)
        -> std::expected<models::RosInterfacesResponse, errors::ErrorCode> override;

    auto get_interface_detail(const std::string& type_name)
        -> std::expected<models::RosInterfaceDetailResponse, errors::ErrorCode> override;

private:
    auto run_command(const std::vector<std::string>& args)
        -> std::expected<std::string, errors::ErrorCode>;

    static auto split_lines(const std::string& output) -> std::vector<std::string>;
    static auto parse_namespace(const std::string& node_name) -> std::string;
    static auto json_value_to_param_type(const nlohmann::json& val) -> std::string;
    auto current_workspace_root() const -> std::string;

    subprocess::SubprocessExecutor executor_;
    mutable std::mutex workspace_mutex_;
    std::string workspace_root_;
};

}  // namespace rosweb::ros
