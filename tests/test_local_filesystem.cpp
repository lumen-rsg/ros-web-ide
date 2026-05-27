#include <doctest.h>
#include <filesystem>
#include <fstream>
#include <unistd.h>
#include <memory>

#include "fs/local_filesystem.hpp"
#include "fs/path_validator.hpp"

using namespace rosweb::fs;
using namespace rosweb::models;

namespace {

auto make_temp_workspace() -> std::pair<std::string, std::unique_ptr<LocalFileSystem>> {
    char tmpl[] = "/tmp/rosweb_fs_test_XXXXXX";
    auto* dir = mkdtemp(tmpl);
    REQUIRE(dir != nullptr);
    std::string workspace(dir);

    auto validator = std::make_shared<PathValidator>(workspace);
    auto fs = std::make_unique<LocalFileSystem>(validator);
    return {workspace, std::move(fs)};
}

void remove_workspace(const std::string& path) {
    std::filesystem::remove_all(path);
}

}  // namespace

TEST_CASE("LocalFileSystem: get_tree on root") {
    auto [ws, fs] = make_temp_workspace();
    auto result = fs->get_tree(ws, 1);
    REQUIRE(result.has_value());
    CHECK(result.value().type == EntryType::directory);
    CHECK(result.value().name.find("rosweb_fs_test_") != std::string::npos);
    remove_workspace(ws);
}

TEST_CASE("LocalFileSystem: get_tree with children") {
    auto [ws, fs] = make_temp_workspace();
    std::filesystem::create_directories(ws + "/src/pkg");
    { std::ofstream(ws + "/src/pkg/node.cpp") << "int main() {}"; }
    { std::ofstream(ws + "/README.md") << "# Test"; }

    auto result = fs->get_tree(ws, 3);
    REQUIRE(result.has_value());
    CHECK(result.value().children.has_value());
    CHECK(result.value().children->size() >= 2);

    remove_workspace(ws);
}

TEST_CASE("LocalFileSystem: read and write file") {
    auto [ws, fs] = make_temp_workspace();

    // Write
    auto write_result = fs->write_file(ws + "/test.txt", "hello world\n", false);
    REQUIRE(write_result.has_value());
    CHECK(write_result->created == true);
    CHECK(write_result->size == 12);
    CHECK(write_result->path.find("test.txt") != std::string::npos);

    // Read back
    auto read_result = fs->read_file(ws + "/test.txt");
    REQUIRE(read_result.has_value());
    CHECK(read_result->content == "hello world\n");
    CHECK(read_result->encoding == "utf-8");
    CHECK(read_result->size == 12);

    // Overwrite
    auto overwrite_result = fs->write_file(ws + "/test.txt", "new content", false);
    REQUIRE(overwrite_result.has_value());
    CHECK(overwrite_result->created == false);

    remove_workspace(ws);
}

TEST_CASE("LocalFileSystem: write with createParents") {
    auto [ws, fs] = make_temp_workspace();

    auto result = fs->write_file(ws + "/a/b/c/file.txt", "deep", true);
    REQUIRE(result.has_value());
    CHECK(result->created == true);

    auto read = fs->read_file(ws + "/a/b/c/file.txt");
    REQUIRE(read.has_value());
    CHECK(read->content == "deep");

    remove_workspace(ws);
}

TEST_CASE("LocalFileSystem: delete file") {
    auto [ws, fs] = make_temp_workspace();
    fs->write_file(ws + "/del.txt", "bye", false);

    auto result = fs->delete_path(ws + "/del.txt", false);
    REQUIRE(result.has_value());
    CHECK(result->deleted == true);

    // Verify it's gone
    auto read = fs->read_file(ws + "/del.txt");
    CHECK_FALSE(read.has_value());

    remove_workspace(ws);
}

TEST_CASE("LocalFileSystem: delete non-empty directory requires recursive") {
    auto [ws, fs] = make_temp_workspace();
    std::filesystem::create_directories(ws + "/dirwithfiles");
    { std::ofstream(ws + "/dirwithfiles/file.txt") << "content"; }

    // Non-recursive should fail
    auto result1 = fs->delete_path(ws + "/dirwithfiles", false);
    CHECK_FALSE(result1.has_value());
    CHECK(result1.error() == rosweb::errors::ErrorCode::FS_NOT_EMPTY);

    // Recursive should succeed
    auto result2 = fs->delete_path(ws + "/dirwithfiles", true);
    REQUIRE(result2.has_value());
    CHECK(result2->deleted == true);

    remove_workspace(ws);
}

TEST_CASE("LocalFileSystem: rename") {
    auto [ws, fs] = make_temp_workspace();
    fs->write_file(ws + "/old.txt", "data", false);

    auto result = fs->rename(ws + "/old.txt", ws + "/new.txt");
    REQUIRE(result.has_value());
    CHECK(result->old_path.find("old.txt") != std::string::npos);
    CHECK(result->new_path.find("new.txt") != std::string::npos);

    // Old file gone
    CHECK_FALSE(fs->read_file(ws + "/old.txt").has_value());
    // New file exists
    auto read = fs->read_file(ws + "/new.txt");
    REQUIRE(read.has_value());
    CHECK(read->content == "data");

    remove_workspace(ws);
}

TEST_CASE("LocalFileSystem: mkdir") {
    auto [ws, fs] = make_temp_workspace();

    SUBCASE("simple mkdir") {
        auto result = fs->mkdir(ws + "/newdir", false);
        REQUIRE(result.has_value());
        CHECK(result->created == true);
    }

    SUBCASE("mkdir with parents") {
        auto result = fs->mkdir(ws + "/a/b/c", true);
        REQUIRE(result.has_value());
        CHECK(result->created == true);
    }

    SUBCASE("mkdir already exists") {
        fs->mkdir(ws + "/existing", false);
        auto result = fs->mkdir(ws + "/existing", false);
        CHECK_FALSE(result.has_value());
        CHECK(result.error() == rosweb::errors::ErrorCode::FS_PATH_EXISTS);
    }

    remove_workspace(ws);
}

TEST_CASE("LocalFileSystem: search") {
    auto [ws, fs] = make_temp_workspace();
    std::filesystem::create_directories(ws + "/src");
    { std::ofstream(ws + "/src/main.cpp") << "int main() { return 0; }"; }
    { std::ofstream(ws + "/src/util.h") << "void helper();"; }
    { std::ofstream(ws + "/README.md") << "# Hello World"; }

    SUBCASE("search by filename") {
        SearchQuery q;
        q.query = "*.cpp";
        q.path = ws;
        q.type = SearchType::filename;
        auto result = fs->search(q);
        REQUIRE(result.has_value());
        CHECK(result->results.size() >= 1);
        bool found = false;
        for (const auto& r : result->results) {
            if (r.path.find("main.cpp") != std::string::npos) found = true;
        }
        CHECK(found);
    }

    SUBCASE("search by content") {
        SearchQuery q;
        q.query = "main";
        q.path = ws;
        q.type = SearchType::content;
        auto result = fs->search(q);
        REQUIRE(result.has_value());
        CHECK(result->results.size() >= 1);
    }

    remove_workspace(ws);
}

TEST_CASE("LocalFileSystem: reads files outside workspace") {
    auto [ws, fs] = make_temp_workspace();

    auto result = fs->read_file("/etc/passwd");
    REQUIRE(result.has_value());
    CHECK_FALSE(result->content.empty());

    remove_workspace(ws);
}
