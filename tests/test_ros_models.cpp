#include <doctest.h>
#include <nlohmann/json.hpp>

#include "models/ros_models.hpp"

using namespace rosweb::models;

TEST_SUITE("RosModels") {
    TEST_CASE("RosNode to_json") {
        RosNode node{.name = "/talker", .node_namespace = "/", .pid = 12345};
        nlohmann::json j = node;
        CHECK_EQ(j["name"], "/talker");
        CHECK_EQ(j["namespace"], "/");
        CHECK_EQ(j["pid"], 12345);
    }

    TEST_CASE("RosNode to_json without pid") {
        RosNode node{.name = "/listener", .node_namespace = "/"};
        nlohmann::json j = node;
        CHECK_EQ(j["name"], "/listener");
        CHECK_FALSE(j.contains("pid"));
    }

    TEST_CASE("RosNodesResponse to_json") {
        RosNodesResponse resp;
        resp.nodes.push_back({.name = "/talker", .node_namespace = "/"});
        nlohmann::json j = resp;
        CHECK(j.contains("nodes"));
        CHECK_EQ(j["nodes"].size(), 1);
    }

    TEST_CASE("RosTopic to_json") {
        RosTopic topic{.name = "/chatter", .type = "std_msgs/msg/String",
                       .publisher_count = 1, .subscriber_count = 2};
        nlohmann::json j = topic;
        CHECK_EQ(j["name"], "/chatter");
        CHECK_EQ(j["type"], "std_msgs/msg/String");
        CHECK_EQ(j["publisherCount"], 1);
        CHECK_EQ(j["subscriberCount"], 2);
    }

    TEST_CASE("RosService to_json") {
        RosService svc{.name = "/set_parameters", .type = "rcl_interfaces/srv/SetParameters", .node = "/talker"};
        nlohmann::json j = svc;
        CHECK_EQ(j["name"], "/set_parameters");
        CHECK_EQ(j["type"], "rcl_interfaces/srv/SetParameters");
        CHECK_EQ(j["node"], "/talker");
    }

    TEST_CASE("RosAction to_json") {
        RosAction action{.name = "/navigate", .type = "nav2_msgs/action/NavigateToPose", .node = "/planner"};
        nlohmann::json j = action;
        CHECK_EQ(j["name"], "/navigate");
        CHECK_EQ(j["type"], "nav2_msgs/action/NavigateToPose");
        CHECK_EQ(j["node"], "/planner");
    }

    TEST_CASE("RosParameter to_json with value") {
        RosParameter param{.name = "rate", .type = "integer", .value = 10, .description = "Publish rate"};
        nlohmann::json j = param;
        CHECK_EQ(j["name"], "rate");
        CHECK_EQ(j["type"], "integer");
        CHECK_EQ(j["value"], 10);
        CHECK_EQ(j["description"], "Publish rate");
    }

    TEST_CASE("RosParameter to_json without optionals") {
        RosParameter param{.name = "rate", .type = "integer"};
        nlohmann::json j = param;
        CHECK_FALSE(j.contains("value"));
        CHECK_FALSE(j.contains("description"));
    }

    TEST_CASE("RosParamSetRequest from_json") {
        nlohmann::json j = {{"node", "/talker"}, {"name", "rate"}, {"value", 20}};
        auto req = j.get<RosParamSetRequest>();
        CHECK_EQ(req.node, "/talker");
        CHECK_EQ(req.name, "rate");
        CHECK_EQ(req.value, 20);
    }

    TEST_CASE("RosParamSetResponse to_json") {
        RosParamSetResponse resp{.node = "/talker", .name = "rate", .value = 20, .success = true};
        nlohmann::json j = resp;
        CHECK_EQ(j["node"], "/talker");
        CHECK_EQ(j["name"], "rate");
        CHECK_EQ(j["value"], 20);
        CHECK_EQ(j["success"], true);
    }

    TEST_CASE("RosInterface to_json") {
        RosInterface iface{.kind = "msg", .package = "std_msgs", .name = "String"};
        nlohmann::json j = iface;
        CHECK_EQ(j["kind"], "msg");
        CHECK_EQ(j["package"], "std_msgs");
        CHECK_EQ(j["name"], "String");
    }

    TEST_CASE("RosInterfaceField to_json with children") {
        RosInterfaceField field{
            .name = "linear", .type = "geometry_msgs/msg/Vector3",
            .is_array = false, .default_value = nullptr,
            .children = std::vector<RosInterfaceField>{
                {.name = "x", .type = "float64", .is_array = false},
            }
        };
        nlohmann::json j = field;
        CHECK_EQ(j["name"], "linear");
        CHECK_EQ(j["isArray"], false);
        CHECK(j.contains("children"));
        CHECK_EQ(j["children"].size(), 1);
        CHECK_EQ(j["children"][0]["name"], "x");
    }

    TEST_CASE("RosInterfaceDetailResponse to_json") {
        RosInterfaceDetailResponse resp;
        resp.type = "geometry_msgs/msg/Twist";
        resp.fields.push_back({.name = "linear", .type = "geometry_msgs/msg/Vector3"});
        nlohmann::json j = resp;
        CHECK_EQ(j["type"], "geometry_msgs/msg/Twist");
        CHECK_EQ(j["fields"].size(), 1);
    }
}
