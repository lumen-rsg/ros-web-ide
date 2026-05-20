#include <doctest.h>
#include <nlohmann/json.hpp>

#include "ros/i_ros_manager.hpp"
#include "models/ros_models.hpp"

using namespace rosweb::ros;
using namespace rosweb::models;
using namespace rosweb::errors;

namespace {

class MockRosManager : public IRosManager {
public:
    bool should_fail = false;
    ErrorCode fail_code = ErrorCode::ROS_SERVICE_UNAVAILABLE;

    // Recorded state
    std::string last_node_param;
    RosParamSetRequest last_set_param_req;
    std::string last_interface_kind;
    std::string last_interface_filter;
    std::string last_interface_detail_type;

    auto list_nodes()
        -> std::expected<RosNodesResponse, ErrorCode> override {
        if (should_fail) return std::unexpected(fail_code);
        RosNodesResponse resp;
        resp.nodes.push_back({.name = "/talker", .node_namespace = "/", .pid = 12345});
        resp.nodes.push_back({.name = "/nav/planner", .node_namespace = "/nav"});
        return resp;
    }

    auto list_topics(bool include_hidden)
        -> std::expected<RosTopicsResponse, ErrorCode> override {
        if (should_fail) return std::unexpected(fail_code);
        RosTopicsResponse resp;
        resp.topics.push_back({.name = "/chatter", .type = "std_msgs/msg/String",
                               .publisher_count = 1, .subscriber_count = 2});
        if (include_hidden) {
            resp.topics.push_back({.name = "/_internal", .type = "std_msgs/msg/String",
                                   .publisher_count = 0, .subscriber_count = 1});
        }
        return resp;
    }

    auto list_services()
        -> std::expected<RosServicesResponse, ErrorCode> override {
        if (should_fail) return std::unexpected(fail_code);
        RosServicesResponse resp;
        resp.services.push_back({.name = "/set_parameters", .type = "rcl_interfaces/srv/SetParameters", .node = "/talker"});
        return resp;
    }

    auto list_actions()
        -> std::expected<RosActionsResponse, ErrorCode> override {
        if (should_fail) return std::unexpected(fail_code);
        RosActionsResponse resp;
        resp.actions.push_back({.name = "/navigate", .type = "nav2_msgs/action/NavigateToPose", .node = "/planner"});
        return resp;
    }

    auto list_params(const std::string& node)
        -> std::expected<RosParamsResponse, ErrorCode> override {
        last_node_param = node;
        if (should_fail) return std::unexpected(ErrorCode::ROS_NODE_NOT_FOUND);
        RosParamsResponse resp;
        resp.node = node;
        resp.parameters.push_back({.name = "rate", .type = "integer", .value = 10});
        return resp;
    }

    auto set_param(const RosParamSetRequest& req)
        -> std::expected<RosParamSetResponse, ErrorCode> override {
        last_set_param_req = req;
        if (should_fail) return std::unexpected(ErrorCode::ROS_PARAM_SET_FAILED);
        RosParamSetResponse resp{.node = req.node, .name = req.name, .value = req.value, .success = true};
        return resp;
    }

    auto list_interfaces(const std::string& kind, const std::string& filter)
        -> std::expected<RosInterfacesResponse, ErrorCode> override {
        last_interface_kind = kind;
        last_interface_filter = filter;
        if (should_fail) return std::unexpected(fail_code);
        RosInterfacesResponse resp;
        resp.interfaces.push_back({.kind = "msg", .package = "std_msgs", .name = "String"});
        resp.interfaces.push_back({.kind = "srv", .package = "example_interfaces", .name = "AddTwoInts"});
        return resp;
    }

    auto get_interface_detail(const std::string& type_name)
        -> std::expected<RosInterfaceDetailResponse, ErrorCode> override {
        last_interface_detail_type = type_name;
        if (should_fail) return std::unexpected(ErrorCode::ROS_INVALID_MESSAGE);
        RosInterfaceDetailResponse resp;
        resp.type = type_name;
        resp.fields.push_back({.name = "data", .type = "string"});
        return resp;
    }
};

}  // namespace

TEST_SUITE("RosManager") {
    TEST_CASE("list_nodes success") {
        MockRosManager mgr;
        auto result = mgr.list_nodes();
        REQUIRE(result.has_value());
        CHECK_EQ(result->nodes.size(), 2);
        CHECK_EQ(result->nodes[0].name, "/talker");
        CHECK_EQ(result->nodes[1].node_namespace, "/nav");
    }

    TEST_CASE("list_nodes failure") {
        MockRosManager mgr;
        mgr.should_fail = true;
        auto result = mgr.list_nodes();
        CHECK_FALSE(result.has_value());
        CHECK_EQ(result.error(), ErrorCode::ROS_SERVICE_UNAVAILABLE);
    }

    TEST_CASE("list_topics without hidden") {
        MockRosManager mgr;
        auto result = mgr.list_topics(false);
        REQUIRE(result.has_value());
        CHECK_EQ(result->topics.size(), 1);
        CHECK_EQ(result->topics[0].name, "/chatter");
    }

    TEST_CASE("list_topics with hidden") {
        MockRosManager mgr;
        auto result = mgr.list_topics(true);
        REQUIRE(result.has_value());
        CHECK_EQ(result->topics.size(), 2);
    }

    TEST_CASE("list_services success") {
        MockRosManager mgr;
        auto result = mgr.list_services();
        REQUIRE(result.has_value());
        CHECK_EQ(result->services.size(), 1);
        CHECK_EQ(result->services[0].type, "rcl_interfaces/srv/SetParameters");
    }

    TEST_CASE("list_actions success") {
        MockRosManager mgr;
        auto result = mgr.list_actions();
        REQUIRE(result.has_value());
        CHECK_EQ(result->actions.size(), 1);
        CHECK_EQ(result->actions[0].name, "/navigate");
    }

    TEST_CASE("list_params success") {
        MockRosManager mgr;
        auto result = mgr.list_params("/talker");
        CHECK_EQ(mgr.last_node_param, "/talker");
        REQUIRE(result.has_value());
        CHECK_EQ(result->parameters.size(), 1);
        CHECK_EQ(result->parameters[0].name, "rate");
    }

    TEST_CASE("list_params failure — node not found") {
        MockRosManager mgr;
        mgr.should_fail = true;
        auto result = mgr.list_params("/nonexistent");
        CHECK_FALSE(result.has_value());
        CHECK_EQ(result.error(), ErrorCode::ROS_NODE_NOT_FOUND);
    }

    TEST_CASE("set_param success") {
        MockRosManager mgr;
        RosParamSetRequest req{.node = "/talker", .name = "rate", .value = 20};
        auto result = mgr.set_param(req);
        CHECK_EQ(mgr.last_set_param_req.node, "/talker");
        CHECK_EQ(mgr.last_set_param_req.name, "rate");
        REQUIRE(result.has_value());
        CHECK_EQ(result->success, true);
        CHECK_EQ(result->value, 20);
    }

    TEST_CASE("set_param failure") {
        MockRosManager mgr;
        mgr.should_fail = true;
        RosParamSetRequest req{.node = "/talker", .name = "rate", .value = 20};
        auto result = mgr.set_param(req);
        CHECK_FALSE(result.has_value());
        CHECK_EQ(result.error(), ErrorCode::ROS_PARAM_SET_FAILED);
    }

    TEST_CASE("list_interfaces — all kinds") {
        MockRosManager mgr;
        auto result = mgr.list_interfaces("all", "");
        CHECK_EQ(mgr.last_interface_kind, "all");
        REQUIRE(result.has_value());
        CHECK_EQ(result->interfaces.size(), 2);
    }

    TEST_CASE("list_interfaces — filter by kind") {
        MockRosManager mgr;
        auto result = mgr.list_interfaces("msg", "std");
        CHECK_EQ(mgr.last_interface_kind, "msg");
        CHECK_EQ(mgr.last_interface_filter, "std");
    }

    TEST_CASE("get_interface_detail success") {
        MockRosManager mgr;
        auto result = mgr.get_interface_detail("std_msgs/msg/String");
        CHECK_EQ(mgr.last_interface_detail_type, "std_msgs/msg/String");
        REQUIRE(result.has_value());
        CHECK_EQ(result->type, "std_msgs/msg/String");
        CHECK_EQ(result->fields.size(), 1);
        CHECK_EQ(result->fields[0].name, "data");
    }

    TEST_CASE("get_interface_detail failure") {
        MockRosManager mgr;
        mgr.should_fail = true;
        auto result = mgr.get_interface_detail("nonexistent/Type");
        CHECK_FALSE(result.has_value());
        CHECK_EQ(result.error(), ErrorCode::ROS_INVALID_MESSAGE);
    }
}
