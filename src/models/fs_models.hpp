#pragma once

#include <optional>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace rosweb::models {

enum class EntryType { file, directory, symlink };

struct FileEntry {
    std::string path;
    std::string name;
    EntryType type;
    std::optional<uintmax_t> size;
    std::optional<std::string> modified;
    std::optional<std::vector<FileEntry>> children;
};

struct FileInfo {
    std::string path;
    std::string content;
    std::string encoding;
    uintmax_t size;
    std::string modified;
};

struct WriteResult {
    std::string path;
    uintmax_t size;
    bool created;
};

struct DeleteResult {
    std::string path;
    bool deleted;
};

struct RenameResult {
    std::string old_path;
    std::string new_path;
};

struct MkdirResult {
    std::string path;
    bool created;
};

enum class SearchType { filename, content, all };

struct SearchQuery {
    std::string query;
    std::string path;
    SearchType type = SearchType::all;
    int max_results = 100;
};

struct SearchMatch {
    int line;
    int column;
    std::string text;
    int length;
};

struct SearchResultEntry {
    std::string path;
    EntryType type;
    std::vector<SearchMatch> matches;
};

struct SearchResult {
    std::vector<SearchResultEntry> results;
    int total;
    bool truncated;
};

void to_json(nlohmann::json& j, const FileEntry& e);
void to_json(nlohmann::json& j, const FileInfo& f);
void to_json(nlohmann::json& j, const WriteResult& w);
void to_json(nlohmann::json& j, const DeleteResult& d);
void to_json(nlohmann::json& j, const RenameResult& r);
void to_json(nlohmann::json& j, const MkdirResult& m);
void to_json(nlohmann::json& j, const SearchResult& s);
void to_json(nlohmann::json& j, const SearchResultEntry& e);
void to_json(nlohmann::json& j, const SearchMatch& m);

}  // namespace rosweb::models
