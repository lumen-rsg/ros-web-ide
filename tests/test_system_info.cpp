#include <doctest.h>
#include <nlohmann/json.hpp>

#include "system/local_system_info.hpp"
#include "models/system_models.hpp"

using namespace rosweb::system;

TEST_CASE("LocalSystemInfo: get_system_info returns reasonable values") {
    LocalSystemInfo info;
    auto result = info.get_system_info();

    CHECK_FALSE(result.hostname.empty());
    CHECK_FALSE(result.platform.empty());
    CHECK_FALSE(result.os.empty());
    CHECK(result.cpu.cores > 0);
    CHECK(result.cpu.usagePercent >= 0.0);
    CHECK(result.cpu.usagePercent <= 100.0);
    CHECK(result.memory.totalBytes > 0);
    CHECK(result.disk.totalBytes > 0);
    CHECK(result.disk.mountPoint == "/");
}

TEST_CASE("LocalSystemInfo: memory values are consistent") {
    LocalSystemInfo info;
    auto result = info.get_system_info();

    CHECK(result.memory.usedBytes <= result.memory.totalBytes);
    CHECK(result.memory.availableBytes <= result.memory.totalBytes);
    CHECK(result.memory.usedBytes + result.memory.availableBytes <= result.memory.totalBytes + 1024 * 1024);  // allow 1MB rounding
}

TEST_CASE("LocalSystemInfo: disk values are consistent") {
    LocalSystemInfo info;
    auto result = info.get_system_info();

    CHECK(result.disk.usedBytes <= result.disk.totalBytes);
    CHECK(result.disk.availableBytes <= result.disk.totalBytes);
}

TEST_CASE("LocalSystemInfo: get_ros_env returns structure") {
    LocalSystemInfo info;
    auto result = info.get_ros_env();

    // rosDistro and rosVersion may be empty if ROS is not installed
    // domainId may be nullopt if ROS_DOMAIN_ID is not set
    // Just check the structure is valid — no crash
    (void)result.variables;
}
