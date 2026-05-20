#include <doctest.h>
#include <nlohmann/json.hpp>

#include "models/workspace_models.hpp"

using namespace rosweb::models;

TEST_CASE("to_json: RosPackage") {
    RosPackage pkg;
    pkg.name = "my_pkg";
    pkg.path = "/home/user/ros_ws/src/my_pkg";
    pkg.type = "ament_cmake";
    pkg.executables = {"talker", "listener"};

    nlohmann::json j = pkg;
    CHECK(j["name"] == "my_pkg");
    CHECK(j["path"] == "/home/user/ros_ws/src/my_pkg");
    CHECK(j["type"] == "ament_cmake");
    CHECK(j["executables"].size() == 2);
    CHECK(j["executables"][0] == "talker");
    CHECK(j["executables"][1] == "listener");
}

TEST_CASE("to_json: WorkspaceInfo") {
    WorkspaceInfo info;
    info.rootPath = "/home/user/ros_ws";
    info.name = "ros_ws";
    info.rosDistro = "humble";
    info.packages = {RosPackage{"pkg1", "/home/user/ros_ws/src/pkg1", "ament_cmake", {}}};

    nlohmann::json j = info;
    CHECK(j["rootPath"] == "/home/user/ros_ws");
    CHECK(j["name"] == "ros_ws");
    CHECK(j["rosDistro"] == "humble");
    CHECK(j["packages"].size() == 1);
    CHECK(j["packages"][0]["name"] == "pkg1");
}
