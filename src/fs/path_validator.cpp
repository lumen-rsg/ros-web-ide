#include "fs/path_validator.hpp"

#include <filesystem>

namespace rosweb::fs {

PathValidator::PathValidator(const std::string& workspace_root) {
    auto resolved = canonicalize(workspace_root);
    if (resolved.has_value()) {
        workspace_root_ = resolved.value();
    } else {
        // If the workspace root doesn't exist yet, try weakly_canonical
        namespace fs = std::filesystem;
        fs::path p = fs::absolute(workspace_root);
        workspace_root_ = fs::weakly_canonical(p).string();
    }
    // Ensure no trailing slash for consistent prefix checking
    if (!workspace_root_.empty() && workspace_root_.back() == '/') {
        workspace_root_.pop_back();
    }
}

auto PathValidator::validate_and_resolve(const std::string& path,
                                         bool must_exist) const
    -> std::expected<std::string, errors::ErrorCode> {
    namespace fs = std::filesystem;

    fs::path resolved;
    if (path.empty() || !fs::path(path).is_absolute()) {
        // Relative path: resolve against workspace root
        resolved = fs::path(workspace_root_) / path;
    } else {
        resolved = fs::path(path);
    }

    // Canonicalize
    std::string canonical;
    if (must_exist) {
        auto result = canonicalize(resolved.string());
        if (!result.has_value()) {
            return std::unexpected(result.error());
        }
        canonical = result.value();
    } else {
        // For paths that may not exist (write targets), use weakly_canonical
        // which resolves what exists and leaves the rest as-is
        canonical = fs::weakly_canonical(resolved).string();
    }

    return canonical;
}

auto PathValidator::is_within_workspace(const std::string& path) const -> bool {
    return path.starts_with(workspace_root_ + "/") || path == workspace_root_;
}

auto PathValidator::workspace_root() const -> const std::string& {
    return workspace_root_;
}

void PathValidator::set_workspace_root(const std::string& new_root) {
    auto resolved = canonicalize(new_root);
    if (resolved.has_value()) {
        workspace_root_ = resolved.value();
        if (!workspace_root_.empty() && workspace_root_.back() == '/') {
            workspace_root_.pop_back();
        }
    }
}

auto PathValidator::canonicalize(const std::string& path)
    -> std::expected<std::string, errors::ErrorCode> {
    namespace fs = std::filesystem;
    std::error_code ec;
    auto canonical = fs::canonical(path, ec);
    if (ec) {
        return std::unexpected(errors::ErrorCode::FS_PATH_NOT_FOUND);
    }
    return canonical.string();
}

}  // namespace rosweb::fs
