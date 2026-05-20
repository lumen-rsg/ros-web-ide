#include <doctest.h>
#include <nlohmann/json.hpp>

#include "models/tf_models.hpp"

using namespace rosweb::models;

TEST_SUITE("TfModels") {

    // --- from_json ---

    TEST_CASE("TfSubscribeRequest from_json required only") {
        auto j = nlohmann::json::parse(R"({"subscriptionId": "tf_1"})");
        TfSubscribeRequest req;
        j.get_to(req);
        CHECK(req.subscription_id == "tf_1");
        CHECK_FALSE(req.frames.has_value());
        CHECK_FALSE(req.throttle_rate.has_value());
    }

    TEST_CASE("TfSubscribeRequest from_json all fields") {
        auto j = nlohmann::json::parse(R"({
            "subscriptionId": "tf_2",
            "frames": ["base_link", "lidar"],
            "throttleRate": 10
        })");
        TfSubscribeRequest req;
        j.get_to(req);
        CHECK(req.subscription_id == "tf_2");
        REQUIRE(req.frames.has_value());
        CHECK(req.frames->size() == 2);
        CHECK(req.throttle_rate.value() == 10);
    }

    // --- to_json ---

    TEST_CASE("TfSubscribedPayload to_json") {
        TfSubscribedPayload p{.subscription_id = "tf_1"};
        nlohmann::json j = p;
        CHECK(j["subscriptionId"] == "tf_1");
        CHECK(j.size() == 1);
    }

    TEST_CASE("TfFrame to_json with parent") {
        TfFrame f;
        f.name = "lidar";
        f.parent = "base_link";
        f.children = {"camera"};
        nlohmann::json j = f;
        CHECK(j["name"] == "lidar");
        CHECK(j["parent"] == "base_link");
        CHECK(j["children"].size() == 1);
    }

    TEST_CASE("TfFrame to_json without parent") {
        TfFrame f;
        f.name = "base_link";
        f.children = {"lidar", "camera"};
        nlohmann::json j = f;
        CHECK(j["name"] == "base_link");
        CHECK(j["parent"].is_null());
        CHECK(j["children"].size() == 2);
    }

    TEST_CASE("TfTreePayload to_json") {
        TfTreePayload tree;
        TfFrame f1;
        f1.name = "base_link";
        f1.children = {"lidar"};
        TfFrame f2;
        f2.name = "lidar";
        f2.parent = "base_link";
        tree.frames.push_back(f1);
        tree.frames.push_back(f2);

        nlohmann::json j = tree;
        CHECK(j["frames"].is_array());
        CHECK(j["frames"].size() == 2);
        CHECK(j["frames"][0]["name"] == "base_link");
        CHECK(j["frames"][1]["parent"] == "base_link");
    }

    TEST_CASE("TfTranslation to_json") {
        TfTranslation t{.x = 0.5, .y = 0.1, .z = 0.3};
        nlohmann::json j = t;
        CHECK(j["x"] == doctest::Approx(0.5));
        CHECK(j["y"] == doctest::Approx(0.1));
        CHECK(j["z"] == doctest::Approx(0.3));
    }

    TEST_CASE("TfRotation to_json") {
        TfRotation r{.x = 0.0, .y = 0.0, .z = 0.0, .w = 1.0};
        nlohmann::json j = r;
        CHECK(j["x"] == doctest::Approx(0.0));
        CHECK(j["w"] == doctest::Approx(1.0));
        CHECK(j.size() == 4);
    }

    TEST_CASE("TfTransform to_json") {
        TfTransform t;
        t.parent = "base_link";
        t.child = "lidar";
        t.translation = {.x = 0.5, .y = 0.0, .z = 0.3};
        t.rotation = {.x = 0.0, .y = 0.0, .z = 0.0, .w = 1.0};
        t.timestamp = "1716201000000000000";
        nlohmann::json j = t;
        CHECK(j["parent"] == "base_link");
        CHECK(j["child"] == "lidar");
        CHECK(j["timestamp"] == "1716201000000000000");
        CHECK(j["translation"]["x"] == doctest::Approx(0.5));
        CHECK(j["rotation"]["w"] == doctest::Approx(1.0));
    }

    TEST_CASE("TfUpdatePayload to_json") {
        TfTransform t;
        t.parent = "base_link";
        t.child = "lidar";
        t.translation = {.x = 0.5, .y = 0.0, .z = 0.3};
        t.rotation = {.x = 0.0, .y = 0.0, .z = 0.0, .w = 1.0};
        t.timestamp = "1716201000000000000";

        TfUpdatePayload p;
        p.subscription_id = "tf_1";
        p.transforms.push_back(t);
        nlohmann::json j = p;
        CHECK(j["subscriptionId"] == "tf_1");
        CHECK(j["transforms"].size() == 1);
        CHECK(j["transforms"][0]["parent"] == "base_link");
    }
}
