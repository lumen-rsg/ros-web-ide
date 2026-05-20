#include "models/fs_models.hpp"

namespace rosweb::models {

static auto entry_type_to_string(EntryType type) -> std::string {
    switch (type) {
        case EntryType::file:      return "file";
        case EntryType::directory: return "directory";
        case EntryType::symlink:   return "symlink";
    }
    return "unknown";
}

void to_json(nlohmann::json& j, const FileEntry& e) {
    j = nlohmann::json{
        {"path", e.path},
        {"name", e.name},
        {"type", entry_type_to_string(e.type)},
    };
    if (e.size.has_value()) {
        j["size"] = e.size.value();
    }
    if (e.modified.has_value()) {
        j["modified"] = e.modified.value();
    }
    if (e.children.has_value()) {
        j["children"] = e.children.value();
    }
}

void to_json(nlohmann::json& j, const FileInfo& f) {
    j = nlohmann::json{
        {"path",      f.path},
        {"content",   f.content},
        {"encoding",  f.encoding},
        {"size",      f.size},
        {"modified",  f.modified},
    };
}

void to_json(nlohmann::json& j, const WriteResult& w) {
    j = nlohmann::json{
        {"path",    w.path},
        {"size",    w.size},
        {"created", w.created},
    };
}

void to_json(nlohmann::json& j, const DeleteResult& d) {
    j = nlohmann::json{
        {"path",    d.path},
        {"deleted", d.deleted},
    };
}

void to_json(nlohmann::json& j, const RenameResult& r) {
    j = nlohmann::json{
        {"oldPath", r.old_path},
        {"newPath", r.new_path},
    };
}

void to_json(nlohmann::json& j, const MkdirResult& m) {
    j = nlohmann::json{
        {"path",    m.path},
        {"created", m.created},
    };
}

void to_json(nlohmann::json& j, const SearchMatch& m) {
    j = nlohmann::json{
        {"line",   m.line},
        {"column", m.column},
        {"text",   m.text},
        {"length", m.length},
    };
}

void to_json(nlohmann::json& j, const SearchResultEntry& e) {
    j = nlohmann::json{
        {"path", e.path},
        {"type", entry_type_to_string(e.type)},
        {"matches", e.matches},
    };
}

void to_json(nlohmann::json& j, const SearchResult& s) {
    j = nlohmann::json{
        {"results",   s.results},
        {"total",     s.total},
        {"truncated", s.truncated},
    };
}

}  // namespace rosweb::models
