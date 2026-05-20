#pragma once

#include <memory>

#include "fs/i_filesystem.hpp"
#include "fs/path_validator.hpp"
#include "fs/binary_detector.hpp"
#include "fs/file_search.hpp"

namespace rosweb::fs {

class LocalFileSystem : public IFileSystem {
public:
    explicit LocalFileSystem(std::shared_ptr<PathValidator> validator);

    auto get_tree(const std::string& path, int depth) const
        -> std::expected<models::FileEntry, errors::ErrorCode> override;

    auto read_file(const std::string& path) const
        -> std::expected<models::FileInfo, errors::ErrorCode> override;

    auto write_file(const std::string& path,
                    const std::string& content,
                    bool create_parents)
        -> std::expected<models::WriteResult, errors::ErrorCode> override;

    auto delete_path(const std::string& path, bool recursive)
        -> std::expected<models::DeleteResult, errors::ErrorCode> override;

    auto rename(const std::string& old_path,
                const std::string& new_path)
        -> std::expected<models::RenameResult, errors::ErrorCode> override;

    auto mkdir(const std::string& path, bool create_parents)
        -> std::expected<models::MkdirResult, errors::ErrorCode> override;

    auto search(const models::SearchQuery& query)
        -> std::expected<models::SearchResult, errors::ErrorCode> override;

private:
    std::shared_ptr<PathValidator> validator_;
    BinaryDetector binary_detector_;
    FileSearch file_search_;

    auto build_tree_entry(const std::filesystem::path& p,
                          int current_depth,
                          int max_depth) const
        -> std::expected<models::FileEntry, errors::ErrorCode>;

    static auto map_fs_error(const std::filesystem::filesystem_error& e,
                             const std::string& path)
        -> errors::ErrorCode;

    static auto last_write_time_to_iso8601(const std::filesystem::path& p)
        -> std::expected<std::string, errors::ErrorCode>;
};

}  // namespace rosweb::fs
