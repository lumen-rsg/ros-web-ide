#include "fs/file_search.hpp"

#include <filesystem>
#include <fstream>
#include <fnmatch.h>
#include <string_view>

namespace rosweb::fs {

namespace {

auto path_to_entry_type(const std::filesystem::directory_entry& entry)
    -> models::EntryType {
    if (entry.is_symlink()) return models::EntryType::symlink;
    if (entry.is_directory()) return models::EntryType::directory;
    return models::EntryType::file;
}

auto should_skip(const std::filesystem::path& p) -> bool {
    std::string name = p.filename().string();
    // Skip hidden files/dirs and common noise
    return name == ".git" || name == ".cache" || name == "build";
}

}  // namespace

auto FileSearch::search_by_name(const std::string& root,
                                const std::string& pattern,
                                int max_results) const
    -> std::vector<models::SearchResultEntry> {
    std::vector<models::SearchResultEntry> results;
    namespace fs = std::filesystem;

    std::error_code ec;
    fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec);
    if (ec) return results;

    for (const auto& entry : it) {
        if (static_cast<int>(results.size()) >= max_results) break;
        if (should_skip(entry.path())) {
            it.disable_recursion_pending();
            continue;
        }

        std::string filename = entry.path().filename().string();
        if (fnmatch(pattern.c_str(), filename.c_str(), 0) == 0) {
            results.push_back(models::SearchResultEntry{
                .path = entry.path().string(),
                .type = path_to_entry_type(entry),
                .matches = {},
            });
        }
    }

    return results;
}

auto FileSearch::search_by_content(const std::string& root,
                                   const std::string& query,
                                   int max_results) const
    -> std::vector<models::SearchResultEntry> {
    std::vector<models::SearchResultEntry> results;
    namespace fs = std::filesystem;

    std::error_code ec;
    fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec);
    if (ec) return results;

    for (const auto& entry : it) {
        if (static_cast<int>(results.size()) >= max_results) break;
        if (!entry.is_regular_file()) continue;
        if (should_skip(entry.path())) {
            it.disable_recursion_pending();
            continue;
        }

        std::ifstream file(entry.path(), std::ios::binary);
        if (!file.is_open()) continue;

        std::vector<models::SearchMatch> matches;
        std::string line;
        int line_num = 0;

        while (std::getline(file, line)) {
            line_num++;
            auto pos = line.find(query);
            while (pos != std::string::npos) {
                int col = static_cast<int>(pos);
                matches.push_back(models::SearchMatch{
                    .line = line_num,
                    .column = col + 1,  // 1-based column
                    .text = line.substr(pos, query.length()),
                    .length = static_cast<int>(query.length()),
                });
                pos = line.find(query, pos + 1);
                // Limit matches per file
                if (matches.size() >= 20) break;
            }
            if (matches.size() >= 20) break;
        }

        if (!matches.empty()) {
            results.push_back(models::SearchResultEntry{
                .path = entry.path().string(),
                .type = models::EntryType::file,
                .matches = std::move(matches),
            });
        }
    }

    return results;
}

auto FileSearch::search_all(const std::string& root,
                            const std::string& query,
                            int max_results) const
    -> std::vector<models::SearchResultEntry> {
    auto name_results = search_by_name(root, query, max_results);
    auto content_results = search_by_content(root, query, max_results);

    // Merge, avoiding duplicates by path
    std::unordered_map<std::string, size_t> seen;
    std::vector<models::SearchResultEntry> merged;

    for (auto& r : name_results) {
        if (static_cast<int>(merged.size()) >= max_results) break;
        if (seen.find(r.path) == seen.end()) {
            seen[r.path] = merged.size();
            merged.push_back(std::move(r));
        }
    }
    for (auto& r : content_results) {
        if (static_cast<int>(merged.size()) >= max_results) break;
        auto it = seen.find(r.path);
        if (it == seen.end()) {
            seen[r.path] = merged.size();
            merged.push_back(std::move(r));
        } else {
            // Merge matches into existing entry
            auto& existing = merged[it->second];
            existing.matches.insert(existing.matches.end(),
                                    r.matches.begin(), r.matches.end());
        }
    }

    return merged;
}

}  // namespace rosweb::fs
