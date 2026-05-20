#include "models/filewatch_models.hpp"

namespace rosweb::models {

auto file_change_kind_to_string(FileChangeKind k) -> std::string {
    switch (k) {
        case FileChangeKind::modified: return "modified";
        case FileChangeKind::created:  return "created";
        case FileChangeKind::deleted:  return "deleted";
        case FileChangeKind::renamed:  return "renamed";
    }
    return "modified";
}

void to_json(nlohmann::json& j, const FileWatchConfirmPayload& v) {
    j = nlohmann::json{
        {"watchId", v.watch_id},
        {"path", v.path},
    };
}

void to_json(nlohmann::json& j, const FileChangePayload& v) {
    j = nlohmann::json{
        {"watchId", v.watch_id},
        {"path", v.path},
        {"kind", file_change_kind_to_string(v.kind)},
    };
    if (v.old_path.has_value()) {
        j["oldPath"] = v.old_path.value();
    }
}

void from_json(const nlohmann::json& j, FileWatchRequest& v) {
    v.watch_id = j.at("watchId").get<std::string>();
    v.path = j.at("path").get<std::string>();
    v.recursive = j.value("recursive", true);
}

void from_json(const nlohmann::json& j, FileUnwatchRequest& v) {
    v.watch_id = j.at("watchId").get<std::string>();
}

}  // namespace rosweb::models
