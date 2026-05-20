#include <doctest.h>
#include <nlohmann/json.hpp>

#include "models/system_models.hpp"

using namespace rosweb::models;

TEST_CASE("to_json: CpuInfo") {
    CpuInfo cpu;
    cpu.model = "ARM Cortex-A57";
    cpu.cores = 4;
    cpu.usagePercent = 23.5;

    nlohmann::json j = cpu;
    CHECK(j["model"] == "ARM Cortex-A57");
    CHECK(j["cores"] == 4);
    CHECK(j["usagePercent"] == doctest::Approx(23.5));
}

TEST_CASE("to_json: MemoryInfo") {
    MemoryInfo mem;
    mem.totalBytes = 4294967296ULL;
    mem.usedBytes = 2147483648ULL;
    mem.availableBytes = 2147483648ULL;

    nlohmann::json j = mem;
    CHECK(j["totalBytes"] == 4294967296ULL);
    CHECK(j["usedBytes"] == 2147483648ULL);
    CHECK(j["availableBytes"] == 2147483648ULL);
}

TEST_CASE("to_json: DiskInfo") {
    DiskInfo disk;
    disk.totalBytes = 322122547200ULL;
    disk.usedBytes = 107374182400ULL;
    disk.availableBytes = 214748182800ULL;
    disk.mountPoint = "/";

    nlohmann::json j = disk;
    CHECK(j["totalBytes"] == 322122547200ULL);
    CHECK(j["usedBytes"] == 107374182400ULL);
    CHECK(j["availableBytes"] == 214748182800ULL);
    CHECK(j["mountPoint"] == "/");
}

TEST_CASE("to_json: SystemInfo") {
    SystemInfo info;
    info.hostname = "jetson-nano";
    info.platform = "aarch64";
    info.os = "Ubuntu 22.04";
    info.cpu = {"ARM Cortex-A57", 4, 23.5};
    info.memory = {4294967296ULL, 2147483648ULL, 2147483648ULL};
    info.disk = {322122547200ULL, 107374182400ULL, 214748182800ULL, "/"};

    nlohmann::json j = info;
    CHECK(j["hostname"] == "jetson-nano");
    CHECK(j["platform"] == "aarch64");
    CHECK(j["os"] == "Ubuntu 22.04");
    CHECK(j["cpu"]["model"] == "ARM Cortex-A57");
    CHECK(j["cpu"]["cores"] == 4);
    CHECK(j["cpu"]["usagePercent"] == doctest::Approx(23.5));
    CHECK(j["memory"]["totalBytes"] == 4294967296ULL);
    CHECK(j["disk"]["mountPoint"] == "/");
}

TEST_CASE("to_json: RosEnvInfo with all fields") {
    RosEnvInfo env;
    env.rosDistro = "humble";
    env.rosVersion = "2";
    env.domainId = 0;
    env.variables = {
        {"ROS_DOMAIN_ID", "0"},
        {"AMENT_PREFIX_PATH", "/opt/ros/humble"},
    };

    nlohmann::json j = env;
    CHECK(j["rosDistro"] == "humble");
    CHECK(j["rosVersion"] == "2");
    CHECK(j["domainId"] == 0);
    CHECK(j["variables"]["ROS_DOMAIN_ID"] == "0");
    CHECK(j["variables"]["AMENT_PREFIX_PATH"] == "/opt/ros/humble");
}

TEST_CASE("to_json: RosEnvInfo with no ROS env") {
    RosEnvInfo env;
    // No distro, no version, no domainId

    nlohmann::json j = env;
    CHECK(j["rosDistro"] == "");
    CHECK(j["rosVersion"] == "");
    CHECK(j["domainId"] == -1);
    CHECK(j["variables"].empty());
}
