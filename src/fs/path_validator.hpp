#pragma once

#include <expected>
#include <string>

#include "errors/error_codes.hpp"

namespace rosweb::fs {

class PathValidator {
public:
    explicit PathValidator(const std::string& workspace_root);

    auto validate_and_resolve(const std::string& path, bool must_exist = true) const
        -> std::expected<std::string, errors::ErrorCode>;

    auto is_within_workspace(const std::string& path) const -> bool;

    auto workspace_root() const -> const std::string&;

    void set_workspace_root(const std::string& new_root);

private:
    std::string workspace_root_;

    static auto canonicalize(const std::string& path) -> std::expected<std::string, errors::ErrorCode>;
};

}  // namespace rosweb::fs
