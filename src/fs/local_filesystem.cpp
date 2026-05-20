#include "fs/local_filesystem.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>

namespace rosweb::fs {

namespace {

constexpr int MAX_TREE_DEPTH = 50;

auto format_iso8601(std::filesystem::file_time_type ftime) -> std::string {
    auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        ftime - std::filesystem::file_time_type::clock::now()
        + std::chrono::system_clock::now()
    );
    auto time_t_val = std::chrono::system_clock::to_time_t(sctp);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&time_t_val));
    return std::string(buf);
}

auto read_file_content(const std::filesystem::path& p) -> std::string {
    std::ifstream file(p, std::ios::binary | std::ios::ate);
    auto size = file.tellg();
    file.seekg(0);
    std::string content(size, '\0');
    file.read(content.data(), size);
    return content;
}

}  // namespace

LocalFileSystem::LocalFileSystem(std::shared_ptr<PathValidator> validator)
    : validator_(std::move(validator)) {}

auto LocalFileSystem::get_tree(const std::string& path, int depth) const
    -> std::expected<models::FileEntry, errors::ErrorCode> {
    auto resolved = validator_->validate_and_resolve(path);
    if (!resolved.has_value()) {
        return std::unexpected(resolved.error());
    }

    namespace fs = std::filesystem;
    fs::path p(resolved.value());

    std::error_code ec;
    auto status = fs::status(p, ec);
    if (ec) {
        return std::unexpected(errors::ErrorCode::FS_PATH_NOT_FOUND);
    }

    if (!fs::is_directory(status)) {
        return std::unexpected(errors::ErrorCode::FS_IS_FILE);
    }

    // depth: 1 = immediate children, 0 = infinite
    int max_depth = (depth == 0) ? MAX_TREE_DEPTH : depth;
    return build_tree_entry(p, 0, max_depth);
}

auto LocalFileSystem::read_file(const std::string& path) const
    -> std::expected<models::FileInfo, errors::ErrorCode> {
    auto resolved = validator_->validate_and_resolve(path);
    if (!resolved.has_value()) {
        return std::unexpected(resolved.error());
    }

    namespace fs = std::filesystem;
    fs::path p(resolved.value());

    std::error_code ec;
    if (!fs::exists(p, ec) || ec) {
        return std::unexpected(errors::ErrorCode::FS_PATH_NOT_FOUND);
    }
    if (fs::is_directory(p)) {
        return std::unexpected(errors::ErrorCode::FS_IS_DIRECTORY);
    }
    if (!fs::is_regular_file(p)) {
        return std::unexpected(errors::ErrorCode::FS_PATH_NOT_FOUND);
    }

    auto content = read_file_content(p);
    auto size = fs::file_size(p);

    std::string encoding = "utf-8";
    if (binary_detector_.is_binary(content) || binary_detector_.has_binary_extension(p.string())) {
        content = BinaryDetector::base64_encode(content);
        encoding = "base64";
    }

    auto modified_result = last_write_time_to_iso8601(p);
    std::string modified = modified_result.has_value()
        ? modified_result.value()
        : "";

    return models::FileInfo{
        .path = p.string(),
        .content = std::move(content),
        .encoding = encoding,
        .size = size,
        .modified = std::move(modified),
    };
}

auto LocalFileSystem::write_file(const std::string& path,
                                 const std::string& content,
                                 bool create_parents)
    -> std::expected<models::WriteResult, errors::ErrorCode> {
    // Resolve with must_exist=false since we're creating
    auto resolved = validator_->validate_and_resolve(path, false);
    if (!resolved.has_value()) {
        return std::unexpected(resolved.error());
    }

    namespace fs = std::filesystem;
    fs::path p(resolved.value());

    // Check if parent exists or create it
    fs::path parent = p.parent_path();
    if (!fs::exists(parent)) {
        if (!create_parents) {
            return std::unexpected(errors::ErrorCode::FS_PATH_NOT_FOUND);
        }
        std::error_code ec;
        fs::create_directories(parent, ec);
        if (ec) {
            return std::unexpected(errors::ErrorCode::FS_WRITE_FAILED);
        }
    }

    bool created = !fs::exists(p);

    // Write to temp file then rename for atomicity
    fs::path temp_path = p;
    temp_path += ".tmp." + std::to_string(reinterpret_cast<uintptr_t>(&p));

    {
        std::ofstream file(temp_path, std::ios::binary);
        if (!file.is_open()) {
            return std::unexpected(errors::ErrorCode::FS_WRITE_FAILED);
        }
        file.write(content.data(), static_cast<std::streamsize>(content.size()));
        if (!file.good()) {
            fs::remove(temp_path);
            return std::unexpected(errors::ErrorCode::FS_WRITE_FAILED);
        }
    }

    std::error_code ec;
    fs::rename(temp_path, p, ec);
    if (ec) {
        fs::remove(temp_path);
        return std::unexpected(errors::ErrorCode::FS_WRITE_FAILED);
    }

    auto size = fs::file_size(p, ec);
    if (ec) size = content.size();

    return models::WriteResult{
        .path = p.string(),
        .size = size,
        .created = created,
    };
}

auto LocalFileSystem::delete_path(const std::string& path, bool recursive)
    -> std::expected<models::DeleteResult, errors::ErrorCode> {
    auto resolved = validator_->validate_and_resolve(path);
    if (!resolved.has_value()) {
        return std::unexpected(resolved.error());
    }

    namespace fs = std::filesystem;
    fs::path p(resolved.value());

    if (!fs::exists(p)) {
        return std::unexpected(errors::ErrorCode::FS_PATH_NOT_FOUND);
    }

    std::error_code ec;
    if (fs::is_directory(p)) {
        if (!recursive) {
            // Check if directory is empty
            fs::directory_iterator it(p, ec);
            if (ec) {
                return std::unexpected(errors::ErrorCode::FS_PERMISSION_DENIED);
            }
            if (it != fs::directory_iterator{}) {
                return std::unexpected(errors::ErrorCode::FS_NOT_EMPTY);
            }
            fs::remove(p, ec);
        } else {
            fs::remove_all(p, ec);
        }
    } else {
        fs::remove(p, ec);
    }

    if (ec) {
        return std::unexpected(errors::ErrorCode::FS_PERMISSION_DENIED);
    }

    return models::DeleteResult{
        .path = p.string(),
        .deleted = true,
    };
}

auto LocalFileSystem::rename(const std::string& old_path,
                             const std::string& new_path)
    -> std::expected<models::RenameResult, errors::ErrorCode> {
    auto resolved_old = validator_->validate_and_resolve(old_path);
    if (!resolved_old.has_value()) {
        return std::unexpected(resolved_old.error());
    }

    auto resolved_new = validator_->validate_and_resolve(new_path, false);
    if (!resolved_new.has_value()) {
        return std::unexpected(resolved_new.error());
    }

    namespace fs = std::filesystem;
    fs::path from(resolved_old.value());
    fs::path to(resolved_new.value());

    if (!fs::exists(from)) {
        return std::unexpected(errors::ErrorCode::FS_PATH_NOT_FOUND);
    }

    if (fs::exists(to)) {
        return std::unexpected(errors::ErrorCode::FS_PATH_EXISTS);
    }

    // Ensure parent of destination exists
    if (!fs::exists(to.parent_path())) {
        return std::unexpected(errors::ErrorCode::FS_PATH_NOT_FOUND);
    }

    std::error_code ec;
    fs::rename(from, to, ec);
    if (ec) {
        return std::unexpected(errors::ErrorCode::FS_WRITE_FAILED);
    }

    return models::RenameResult{
        .old_path = from.string(),
        .new_path = to.string(),
    };
}

auto LocalFileSystem::mkdir(const std::string& path, bool create_parents)
    -> std::expected<models::MkdirResult, errors::ErrorCode> {
    auto resolved = validator_->validate_and_resolve(path, false);
    if (!resolved.has_value()) {
        return std::unexpected(resolved.error());
    }

    namespace fs = std::filesystem;
    fs::path p(resolved.value());

    if (fs::exists(p)) {
        return std::unexpected(errors::ErrorCode::FS_PATH_EXISTS);
    }

    std::error_code ec;
    bool created;
    if (create_parents) {
        created = fs::create_directories(p, ec);
    } else {
        created = fs::create_directory(p, ec);
    }

    if (ec) {
        return std::unexpected(errors::ErrorCode::FS_WRITE_FAILED);
    }

    return models::MkdirResult{
        .path = p.string(),
        .created = created,
    };
}

auto LocalFileSystem::search(const models::SearchQuery& query)
    -> std::expected<models::SearchResult, errors::ErrorCode> {
    std::string root = query.path;
    if (root.empty()) {
        root = validator_->workspace_root();
    }

    auto resolved = validator_->validate_and_resolve(root);
    if (!resolved.has_value()) {
        return std::unexpected(resolved.error());
    }

    std::vector<models::SearchResultEntry> entries;
    switch (query.type) {
        case models::SearchType::filename:
            entries = file_search_.search_by_name(resolved.value(), query.query, query.max_results);
            break;
        case models::SearchType::content:
            entries = file_search_.search_by_content(resolved.value(), query.query, query.max_results);
            break;
        case models::SearchType::all:
            entries = file_search_.search_all(resolved.value(), query.query, query.max_results);
            break;
    }

    bool truncated = static_cast<int>(entries.size()) >= query.max_results;

    return models::SearchResult{
        .results = std::move(entries),
        .total = static_cast<int>(entries.size()),
        .truncated = truncated,
    };
}

auto LocalFileSystem::build_tree_entry(const std::filesystem::path& p,
                                       int current_depth,
                                       int max_depth) const
    -> std::expected<models::FileEntry, errors::ErrorCode> {
    namespace fs = std::filesystem;

    std::error_code ec;
    auto status = fs::status(p, ec);
    if (ec) {
        return std::unexpected(errors::ErrorCode::FS_PATH_NOT_FOUND);
    }

    models::EntryType type = models::EntryType::file;
    if (fs::is_symlink(p)) type = models::EntryType::symlink;
    else if (fs::is_directory(status)) type = models::EntryType::directory;
    else if (fs::is_regular_file(status)) type = models::EntryType::file;

    models::FileEntry entry{
        .path = p.string(),
        .name = p.filename().string(),
        .type = type,
    };

    if (fs::is_regular_file(status)) {
        entry.size = fs::file_size(p);
        auto mod_result = last_write_time_to_iso8601(p);
        if (mod_result.has_value()) {
            entry.modified = mod_result.value();
        }
    }

    if (type == models::EntryType::directory && current_depth < max_depth) {
        std::vector<models::FileEntry> children;
        fs::directory_iterator it(p, fs::directory_options::skip_permission_denied, ec);
        if (ec) {
            entry.children = children;
            return entry;
        }

        for (const auto& child : it) {
            auto child_entry = build_tree_entry(child.path(), current_depth + 1, max_depth);
            if (child_entry.has_value()) {
                children.push_back(std::move(child_entry.value()));
            }
            // Skip entries that fail (permission denied, etc.)
        }

        entry.children = std::move(children);
    }

    return entry;
}

auto LocalFileSystem::map_fs_error(const std::filesystem::filesystem_error& e,
                                   const std::string& path)
    -> errors::ErrorCode {
    const auto& ec = e.code();
    if (ec == std::errc::no_such_file_or_directory) {
        return errors::ErrorCode::FS_PATH_NOT_FOUND;
    }
    if (ec == std::errc::permission_denied) {
        return errors::ErrorCode::FS_PERMISSION_DENIED;
    }
    if (ec == std::errc::file_exists) {
        return errors::ErrorCode::FS_PATH_EXISTS;
    }
    if (ec == std::errc::directory_not_empty) {
        return errors::ErrorCode::FS_NOT_EMPTY;
    }
    if (ec == std::errc::is_a_directory) {
        return errors::ErrorCode::FS_IS_DIRECTORY;
    }
    if (ec == std::errc::not_a_directory) {
        return errors::ErrorCode::FS_IS_FILE;
    }
    return errors::ErrorCode::FS_WRITE_FAILED;
}

auto LocalFileSystem::last_write_time_to_iso8601(const std::filesystem::path& p)
    -> std::expected<std::string, errors::ErrorCode> {
    std::error_code ec;
    auto ftime = std::filesystem::last_write_time(p, ec);
    if (ec) {
        return std::unexpected(errors::ErrorCode::FS_PATH_NOT_FOUND);
    }
    return format_iso8601(ftime);
}

}  // namespace rosweb::fs
