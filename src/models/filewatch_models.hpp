#pragma once

#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

namespace rosweb::models {

enum class FileChangeKind { modified, created, deleted, renamed };

auto file_change_kind_to_string(FileChangeKind k) -> std::string;

struct FileWatchRequest {
    std::string watch_id;
    std::string path;
    bool recursive = true;
};

struct FileUnwatchRequest {
    std::string watch_id;
};

struct FileWatchConfirmPayload {
    std::string watch_id;
    std::string path;
};

struct FileChangePayload {
    std::string watch_id;
    std::string path;
    FileChangeKind kind;
    std::optional<std::string> old_path;
};

// Serialization
void to_json(nlohmann::json& j, const FileWatchConfirmPayload& v);
void to_json(nlohmann::json& j, const FileChangePayload& v);
void from_json(const nlohmann::json& j, FileWatchRequest& v);
void from_json(const nlohmann::json& j, FileUnwatchRequest& v);

}  // namespace rosweb::models
