#include <doctest.h>
#include <filesystem>
#include <unistd.h>

#include "fs/path_validator.hpp"

using namespace rosweb::fs;

namespace {

auto make_temp_dir() -> std::string {
    char tmpl[] = "/tmp/rosweb_test_XXXXXX";
    auto* dir = mkdtemp(tmpl);
    REQUIRE(dir != nullptr);
    return std::string(dir);
}

void remove_temp_dir(const std::string& path) {
    std::filesystem::remove_all(path);
}

}  // namespace

TEST_CASE("PathValidator: canonicalizes workspace root") {
    auto tmp = make_temp_dir();
    PathValidator validator(tmp);
    // /tmp is a symlink to /private/tmp on macOS
    CHECK(validator.workspace_root().find("rosweb_test_") != std::string::npos);
    remove_temp_dir(tmp);
}

TEST_CASE("PathValidator: accepts paths inside workspace") {
    auto tmp = make_temp_dir();
    PathValidator validator(tmp);

    auto result = validator.validate_and_resolve(tmp);
    REQUIRE(result.has_value());
    CHECK(result.value() == validator.workspace_root());
    remove_temp_dir(tmp);
}

TEST_CASE("PathValidator: accepts nested paths inside workspace") {
    auto tmp = make_temp_dir();
    PathValidator validator(tmp);

    auto result = validator.validate_and_resolve(tmp + "/subdir/file.txt");
    CHECK_FALSE(result.has_value());  // doesn't exist yet

    auto result2 = validator.validate_and_resolve(tmp + "/subdir/file.txt", false);
    REQUIRE(result2.has_value());
    CHECK(result2.value().find("rosweb_test_") != std::string::npos);
    remove_temp_dir(tmp);
}

TEST_CASE("PathValidator: rejects paths outside workspace") {
    auto tmp = make_temp_dir();
    PathValidator validator(tmp);

    auto result = validator.validate_and_resolve("/etc/passwd");
    CHECK_FALSE(result.has_value());
    CHECK(result.error() == rosweb::errors::ErrorCode::FS_PERMISSION_DENIED);
    remove_temp_dir(tmp);
}

TEST_CASE("PathValidator: rejects traversal attacks") {
    auto tmp = make_temp_dir();
    PathValidator validator(tmp);

    auto result = validator.validate_and_resolve(tmp + "/../../etc/passwd");
    CHECK_FALSE(result.has_value());
    CHECK(result.error() == rosweb::errors::ErrorCode::FS_PERMISSION_DENIED);
    remove_temp_dir(tmp);
}

TEST_CASE("PathValidator: resolves relative paths against workspace") {
    auto tmp = make_temp_dir();
    PathValidator validator(tmp);

    // Relative path should be resolved against workspace root
    auto result = validator.validate_and_resolve("some_dir", false);
    REQUIRE(result.has_value());
    // Should end with /some_dir and start with workspace root
    CHECK(result.value().ends_with("/some_dir"));
    CHECK(result.value().starts_with(validator.workspace_root()));
    remove_temp_dir(tmp);
}

TEST_CASE("PathValidator: empty path resolves to workspace root") {
    auto tmp = make_temp_dir();
    PathValidator validator(tmp);

    auto result = validator.validate_and_resolve("");
    REQUIRE(result.has_value());
    CHECK(result.value() == validator.workspace_root());
    remove_temp_dir(tmp);
}

TEST_CASE("PathValidator: set_workspace_root updates root") {
    auto tmp1 = make_temp_dir();
    auto tmp2 = make_temp_dir();
    PathValidator validator(tmp1);
    CHECK(validator.workspace_root().find("rosweb_test_") != std::string::npos);

    validator.set_workspace_root(tmp2);

    auto result = validator.validate_and_resolve(tmp2);
    REQUIRE(result.has_value());
    CHECK(result.value() == validator.workspace_root());

    // Old workspace should now be rejected
    auto old_result = validator.validate_and_resolve(tmp1);
    CHECK_FALSE(old_result.has_value());
    CHECK(old_result.error() == rosweb::errors::ErrorCode::FS_PERMISSION_DENIED);

    remove_temp_dir(tmp1);
    remove_temp_dir(tmp2);
}
