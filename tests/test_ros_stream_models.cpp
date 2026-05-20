#include <doctest.h>
#include <nlohmann/json.hpp>

#include "models/ros_stream_models.hpp"

using namespace rosweb::models;

TEST_SUITE("RosStreamModels") {

    // --- from_json tests ---

    TEST_CASE("TopicSubscribeRequest from_json required fields") {
        auto j = nlohmann::json::parse(R"({
            "subscriptionId": "sub_1",
            "topic": "/chatter"
        })");
        TopicSubscribeRequest req;
        j.get_to(req);
        CHECK(req.subscription_id == "sub_1");
        CHECK(req.topic == "/chatter");
        CHECK_FALSE(req.type.has_value());
        CHECK_FALSE(req.throttle_rate.has_value());
        CHECK_FALSE(req.queue_length.has_value());
    }

    TEST_CASE("TopicSubscribeRequest from_json all fields") {
        auto j = nlohmann::json::parse(R"({
            "subscriptionId": "sub_2",
            "topic": "/cmd_vel",
            "type": "geometry_msgs/msg/Twist",
            "throttleRate": 10,
            "queueLength": 5
        })");
        TopicSubscribeRequest req;
        j.get_to(req);
        CHECK(req.subscription_id == "sub_2");
        CHECK(req.topic == "/cmd_vel");
        CHECK(req.type.value() == "geometry_msgs/msg/Twist");
        CHECK(req.throttle_rate.value() == 10);
        CHECK(req.queue_length.value() == 5);
    }

    TEST_CASE("TopicUnsubscribeRequest from_json") {
        auto j = nlohmann::json::parse(R"({"subscriptionId": "sub_1"})");
        TopicUnsubscribeRequest req;
        j.get_to(req);
        CHECK(req.subscription_id == "sub_1");
    }

    TEST_CASE("TopicPublishRequest from_json") {
        auto j = nlohmann::json::parse(R"({
            "topic": "/cmd_vel",
            "type": "geometry_msgs/msg/Twist",
            "message": {"linear": {"x": 1.0}}
        })");
        TopicPublishRequest req;
        j.get_to(req);
        CHECK(req.topic == "/cmd_vel");
        CHECK(req.type == "geometry_msgs/msg/Twist");
        CHECK(req.message["linear"]["x"] == 1.0);
    }

    TEST_CASE("ServiceCallRequest from_json") {
        auto j = nlohmann::json::parse(R"({
            "callId": "call_1",
            "service": "/set_parameters",
            "type": "rcl_interfaces/srv/SetParameters",
            "request": {"parameters": []},
            "timeout": 5000
        })");
        ServiceCallRequest req;
        j.get_to(req);
        CHECK(req.call_id == "call_1");
        CHECK(req.service == "/set_parameters");
        CHECK(req.timeout.value() == 5000);
    }

    TEST_CASE("ActionCallRequest from_json") {
        auto j = nlohmann::json::parse(R"({
            "callId": "act_1",
            "action": "/navigate",
            "type": "nav2_msgs/action/NavigateToPose",
            "goal": {"pose": {}},
            "timeout": 30000
        })");
        ActionCallRequest req;
        j.get_to(req);
        CHECK(req.call_id == "act_1");
        CHECK(req.action == "/navigate");
        CHECK(req.timeout.value() == 30000);
    }

    TEST_CASE("CancelActionRequest from_json") {
        auto j = nlohmann::json::parse(R"({"callId": "act_1"})");
        CancelActionRequest req;
        j.get_to(req);
        CHECK(req.call_id == "act_1");
    }

    TEST_CASE("StartBagRequest from_json all fields") {
        auto j = nlohmann::json::parse(R"({
            "bagId": "bag_1",
            "topics": ["/chatter", "/tf"],
            "path": "/tmp/recording",
            "format": "sqlite3"
        })");
        StartBagRequest req;
        j.get_to(req);
        CHECK(req.bag_id == "bag_1");
        CHECK(req.topics.has_value());
        CHECK(req.topics->size() == 2);
        CHECK(req.path == "/tmp/recording");
        CHECK(req.format.value() == "sqlite3");
    }

    TEST_CASE("StartBagRequest from_json minimal") {
        auto j = nlohmann::json::parse(R"({
            "bagId": "bag_2",
            "path": "/tmp/bag2"
        })");
        StartBagRequest req;
        j.get_to(req);
        CHECK(req.bag_id == "bag_2");
        CHECK_FALSE(req.topics.has_value());
        CHECK_FALSE(req.format.has_value());
    }

    TEST_CASE("StopBagRequest from_json") {
        auto j = nlohmann::json::parse(R"({"bagId": "bag_1"})");
        StopBagRequest req;
        j.get_to(req);
        CHECK(req.bag_id == "bag_1");
    }

    // --- to_json tests ---

    TEST_CASE("TopicSubscribedPayload to_json") {
        TopicSubscribedPayload p{.subscription_id = "sub_1", .topic = "/chatter"};
        nlohmann::json j = p;
        CHECK(j["subscriptionId"] == "sub_1");
        CHECK(j["topic"] == "/chatter");
    }

    TEST_CASE("TopicMessagePayload to_json") {
        TopicMessagePayload p{
            .subscription_id = "sub_1",
            .topic = "/chatter",
            .timestamp = "1716201000000000000",
            .message = nlohmann::json::parse(R"({"data": "Hello!"})"),
        };
        nlohmann::json j = p;
        CHECK(j["subscriptionId"] == "sub_1");
        CHECK(j["topic"] == "/chatter");
        CHECK(j["timestamp"] == "1716201000000000000");
        CHECK(j["message"]["data"] == "Hello!");
    }

    TEST_CASE("ServiceResultPayload to_json success") {
        ServiceResultPayload p{
            .call_id = "call_1",
            .success = true,
            .result = nlohmann::json::parse(R"({"results": []})"),
            .error = std::nullopt,
        };
        nlohmann::json j = p;
        CHECK(j["callId"] == "call_1");
        CHECK(j["success"] == true);
        CHECK(j.contains("result"));
        CHECK_FALSE(j.contains("error"));
    }

    TEST_CASE("ServiceResultPayload to_json failure") {
        ServiceResultPayload p{
            .call_id = "call_2",
            .success = false,
            .result = std::nullopt,
            .error = "Timed out",
        };
        nlohmann::json j = p;
        CHECK(j["success"] == false);
        CHECK(j["error"] == "Timed out");
        CHECK_FALSE(j.contains("result"));
    }

    TEST_CASE("ActionFeedbackPayload to_json") {
        ActionFeedbackPayload p{
            .call_id = "act_1",
            .feedback = nlohmann::json::parse(R"({"distance_remaining": 12.5})"),
        };
        nlohmann::json j = p;
        CHECK(j["callId"] == "act_1");
        CHECK(j["feedback"]["distance_remaining"] == 12.5);
    }

    TEST_CASE("ActionResultPayload to_json") {
        ActionResultPayload p{
            .call_id = "act_1",
            .status = "succeeded",
            .result = nlohmann::json::parse(R"({"code": 0})"),
        };
        nlohmann::json j = p;
        CHECK(j["callId"] == "act_1");
        CHECK(j["status"] == "succeeded");
        CHECK(j["result"]["code"] == 0);
    }

    TEST_CASE("ActionResultPayload to_json no result") {
        ActionResultPayload p{
            .call_id = "act_2",
            .status = "cancelled",
            .result = std::nullopt,
        };
        nlohmann::json j = p;
        CHECK(j["status"] == "cancelled");
        CHECK_FALSE(j.contains("result"));
    }

    TEST_CASE("NodeEventPayload to_json") {
        NodeEventPayload p{.event = "started", .node = "/talker"};
        nlohmann::json j = p;
        CHECK(j["event"] == "started");
        CHECK(j["node"] == "/talker");
    }

    TEST_CASE("BagStatusPayload to_json all fields") {
        BagStatusPayload p{
            .bag_id = "bag_1",
            .status = "recording",
            .duration = 120.5,
            .message_count = 5000,
            .size_bytes = 10485760.0,
        };
        nlohmann::json j = p;
        CHECK(j["bagId"] == "bag_1");
        CHECK(j["status"] == "recording");
        CHECK(j["duration"] == doctest::Approx(120.5));
        CHECK(j["messageCount"] == 5000);
        CHECK(j["sizeBytes"] == doctest::Approx(10485760.0));
    }

    TEST_CASE("BagStatusPayload to_json minimal") {
        BagStatusPayload p{
            .bag_id = "bag_1",
            .status = "stopped",
        };
        nlohmann::json j = p;
        CHECK(j["status"] == "stopped");
        CHECK_FALSE(j.contains("duration"));
        CHECK_FALSE(j.contains("messageCount"));
        CHECK_FALSE(j.contains("sizeBytes"));
    }
}
