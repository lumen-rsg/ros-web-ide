#include <doctest.h>
#include <filesystem>
#include <fstream>
#include <unistd.h>

#include "workspace/package_discovery.hpp"

using namespace rosweb::workspace;

namespace {

auto make_temp_dir() -> std::string {
    char tmpl[] = "/tmp/rosweb_ws_test_XXXXXX";
    auto* dir = mkdtemp(tmpl);
    REQUIRE(dir != nullptr);
    return std::string(dir);
}

void remove_temp_dir(const std::string& path) {
    std::filesystem::remove_all(path);
}

void write_file(const std::string& path, const std::string& content) {
    std::ofstream(path) << content;
}

}  // namespace

TEST_CASE("PackageDiscovery: finds ament_cmake package") {
    auto tmp = make_temp_dir();
    std::filesystem::create_directories(tmp + "/src/my_pkg");

    write_file(tmp + "/src/my_pkg/package.xml",
        "<?xml version=\"1.0\"?>\n"
        "<package format=\"3\">\n"
        "  <name>my_pkg</name>\n"
        "  <buildtool_depend>ament_cmake</buildtool_depend>\n"
        "</package>\n");

    PackageDiscovery discovery;
    auto packages = discovery.discover_packages(tmp + "/src");

    REQUIRE(packages.size() == 1);
    CHECK(packages[0].name == "my_pkg");
    CHECK(packages[0].type == "ament_cmake");
    CHECK(packages[0].path == tmp + "/src/my_pkg");

    remove_temp_dir(tmp);
}

TEST_CASE("PackageDiscovery: finds ament_python package") {
    auto tmp = make_temp_dir();
    std::filesystem::create_directories(tmp + "/src/py_pkg");

    write_file(tmp + "/src/py_pkg/package.xml",
        "<?xml version=\"1.0\"?>\n"
        "<package format=\"3\">\n"
        "  <name>py_pkg</name>\n"
        "  <buildtool_depend>ament_python</buildtool_depend>\n"
        "</package>\n");

    PackageDiscovery discovery;
    auto packages = discovery.discover_packages(tmp + "/src");

    REQUIRE(packages.size() == 1);
    CHECK(packages[0].name == "py_pkg");
    CHECK(packages[0].type == "ament_python");

    remove_temp_dir(tmp);
}

TEST_CASE("PackageDiscovery: finds multiple packages") {
    auto tmp = make_temp_dir();
    std::filesystem::create_directories(tmp + "/src/pkg_a");
    std::filesystem::create_directories(tmp + "/src/pkg_b");

    write_file(tmp + "/src/pkg_a/package.xml",
        "<package><name>pkg_a</name>"
        "<buildtool_depend>ament_cmake</buildtool_depend></package>");

    write_file(tmp + "/src/pkg_b/package.xml",
        "<package><name>pkg_b</name>"
        "<buildtool_depend>ament_cmake</buildtool_depend></package>");

    PackageDiscovery discovery;
    auto packages = discovery.discover_packages(tmp + "/src");

    REQUIRE(packages.size() == 2);
    // Sorted by name
    CHECK(packages[0].name == "pkg_a");
    CHECK(packages[1].name == "pkg_b");

    remove_temp_dir(tmp);
}

TEST_CASE("PackageDiscovery: skips .git, build, install, log directories") {
    auto tmp = make_temp_dir();
    std::filesystem::create_directories(tmp + "/src/.git/pkg_hidden");
    std::filesystem::create_directories(tmp + "/src/build/pkg_hidden");
    std::filesystem::create_directories(tmp + "/src/install/pkg_hidden");
    std::filesystem::create_directories(tmp + "/src/log/pkg_hidden");
    std::filesystem::create_directories(tmp + "/src/visible_pkg");

    // These should be skipped
    write_file(tmp + "/src/.git/pkg_hidden/package.xml",
        "<package><name>hidden1</name></package>");
    write_file(tmp + "/src/build/pkg_hidden/package.xml",
        "<package><name>hidden2</name></package>");
    write_file(tmp + "/src/install/pkg_hidden/package.xml",
        "<package><name>hidden3</name></package>");
    write_file(tmp + "/src/log/pkg_hidden/package.xml",
        "<package><name>hidden4</name></package>");

    // This should be found
    write_file(tmp + "/src/visible_pkg/package.xml",
        "<package><name>visible_pkg</name>"
        "<buildtool_depend>ament_cmake</buildtool_depend></package>");

    PackageDiscovery discovery;
    auto packages = discovery.discover_packages(tmp + "/src");

    REQUIRE(packages.size() == 1);
    CHECK(packages[0].name == "visible_pkg");

    remove_temp_dir(tmp);
}

TEST_CASE("PackageDiscovery: ignores malformed package.xml without name") {
    auto tmp = make_temp_dir();
    std::filesystem::create_directories(tmp + "/src/bad_pkg");

    write_file(tmp + "/src/bad_pkg/package.xml",
        "<package><description>No name tag</description></package>");

    PackageDiscovery discovery;
    auto packages = discovery.discover_packages(tmp + "/src");

    CHECK(packages.empty());

    remove_temp_dir(tmp);
}

TEST_CASE("PackageDiscovery: returns empty for nonexistent directory") {
    PackageDiscovery discovery;
    auto packages = discovery.discover_packages("/nonexistent/path");
    CHECK(packages.empty());
}

TEST_CASE("PackageDiscovery: finds executables from CMakeLists.txt fallback") {
    auto tmp = make_temp_dir();
    std::filesystem::create_directories(tmp + "/src/my_pkg");

    write_file(tmp + "/src/my_pkg/package.xml",
        "<package><name>my_pkg</name>"
        "<buildtool_depend>ament_cmake</buildtool_depend></package>");

    write_file(tmp + "/src/my_pkg/CMakeLists.txt",
        "cmake_minimum_required(VERSION 3.8)\n"
        "add_executable(talker src/talker.cpp)\n"
        "add_executable(listener src/listener.cpp)\n"
        "install(TARGETS talker listener DESTINATION lib/${PROJECT_NAME})\n");

    PackageDiscovery discovery;
    auto packages = discovery.discover_packages(tmp + "/src");

    REQUIRE(packages.size() == 1);
    CHECK(packages[0].name == "my_pkg");
    REQUIRE(packages[0].executables.size() == 2);
    CHECK(packages[0].executables[0] == "listener");
    CHECK(packages[0].executables[1] == "talker");

    remove_temp_dir(tmp);
}

TEST_CASE("PackageDiscovery: detects build_type from export tag") {
    auto tmp = make_temp_dir();
    std::filesystem::create_directories(tmp + "/src/modern_pkg");

    write_file(tmp + "/src/modern_pkg/package.xml",
        "<?xml version=\"1.0\"?>\n"
        "<package format=\"3\">\n"
        "  <name>modern_pkg</name>\n"
        "  <buildtool_depend>ament_cmake</buildtool_depend>\n"
        "  <export><build_type>ament_python</build_type></export>\n"
        "</package>\n");

    PackageDiscovery discovery;
    auto packages = discovery.discover_packages(tmp + "/src");

    REQUIRE(packages.size() == 1);
    // ament_python wins because <build_type> tag is checked
    CHECK(packages[0].type == "ament_python");

    remove_temp_dir(tmp);
}
