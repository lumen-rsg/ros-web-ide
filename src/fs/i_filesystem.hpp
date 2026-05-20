#pragma once

#include <expected>
#include <string>

#include "errors/error_codes.hpp"
#include "models/fs_models.hpp"

namespace rosweb::fs {

class IFileSystem {
public:
    virtual ~IFileSystem() = default;

    virtual auto get_tree(const std::string& path, int depth) const
        -> std::expected<models::FileEntry, errors::ErrorCode> = 0;

    virtual auto read_file(const std::string& path) const
        -> std::expected<models::FileInfo, errors::ErrorCode> = 0;

    virtual auto write_file(const std::string& path,
                            const std::string& content,
                            bool create_parents)
        -> std::expected<models::WriteResult, errors::ErrorCode> = 0;

    virtual auto delete_path(const std::string& path, bool recursive)
        -> std::expected<models::DeleteResult, errors::ErrorCode> = 0;

    virtual auto rename(const std::string& old_path,
                        const std::string& new_path)
        -> std::expected<models::RenameResult, errors::ErrorCode> = 0;

    virtual auto mkdir(const std::string& path, bool create_parents)
        -> std::expected<models::MkdirResult, errors::ErrorCode> = 0;

    virtual auto search(const models::SearchQuery& query)
        -> std::expected<models::SearchResult, errors::ErrorCode> = 0;
};

}  // namespace rosweb::fs
