#pragma once

#include <string>
#include <vector>

#include "models/fs_models.hpp"

namespace rosweb::fs {

class FileSearch {
public:
    auto search_by_name(const std::string& root,
                        const std::string& pattern,
                        int max_results) const
        -> std::vector<models::SearchResultEntry>;

    auto search_by_content(const std::string& root,
                           const std::string& query,
                           int max_results) const
        -> std::vector<models::SearchResultEntry>;

    auto search_all(const std::string& root,
                    const std::string& query,
                    int max_results) const
        -> std::vector<models::SearchResultEntry>;
};

}  // namespace rosweb::fs
