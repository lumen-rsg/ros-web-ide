#include <doctest.h>
#include <nlohmann/json.hpp>

#include "models/filewatch_models.hpp"

using namespace rosweb::models;

TEST_SUITE("FileWatchModels") {
    TEST_CASE("file_change_kind_to_string") {
        CHECK_EQ(file_change_kind_to_string(FileChangeKind::modified), "modified");
        CHECK_EQ(file_change_kind_to_string(FileChangeKind::created), "created");
        CHECK_EQ(file_change_kind_to_string(FileChangeKind::deleted), "deleted");
        CHECK_EQ(file_change_kind_to_string(FileChangeKind::renamed), "renamed");
    }

    TEST_CASE("FileWatchConfirmPayload to_json") {
        FileWatchConfirmPayload p{.watch_id = "w_1", .path = "/some/path"};
        nlohmann::json j = p;
        CHECK_EQ(j["watchId"], "w_1");
        CHECK_EQ(j["path"], "/some/path");
    }

    TEST_CASE("FileChangePayload to_json without old_path") {
        FileChangePayload p{.watch_id = "w_1", .path = "/some/file.cpp",
                            .kind = FileChangeKind::modified};
        nlohmann::json j = p;
        CHECK_EQ(j["watchId"], "w_1");
        CHECK_EQ(j["path"], "/some/file.cpp");
        CHECK_EQ(j["kind"], "modified");
        CHECK_FALSE(j.contains("oldPath"));
    }

    TEST_CASE("FileChangePayload to_json with old_path") {
        FileChangePayload p{.watch_id = "w_1", .path = "/some/new.cpp",
                            .kind = FileChangeKind::renamed, .old_path = "/some/old.cpp"};
        nlohmann::json j = p;
        CHECK_EQ(j["kind"], "renamed");
        CHECK_EQ(j["oldPath"], "/some/old.cpp");
    }

    TEST_CASE("FileWatchRequest from_json") {
        nlohmann::json j = {{"watchId", "w_1"}, {"path", "/some/path"}, {"recursive", false}};
        auto req = j.get<FileWatchRequest>();
        CHECK_EQ(req.watch_id, "w_1");
        CHECK_EQ(req.path, "/some/path");
        CHECK_EQ(req.recursive, false);
    }

    TEST_CASE("FileWatchRequest from_json default recursive") {
        nlohmann::json j = {{"watchId", "w_2"}, {"path", "/other"}};
        auto req = j.get<FileWatchRequest>();
        CHECK_EQ(req.recursive, true);  // default
    }

    TEST_CASE("FileUnwatchRequest from_json") {
        nlohmann::json j = {{"watchId", "w_1"}};
        auto req = j.get<FileUnwatchRequest>();
        CHECK_EQ(req.watch_id, "w_1");
    }
}
