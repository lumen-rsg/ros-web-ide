#pragma once

#include <optional>
#include <string>
#include <vector>

#include "models/workspace_models.hpp"

namespace rosweb::workspace {

class PackageDiscovery {
public:
    auto discover_packages(const std::string& workspace_root) const
        -> std::vector<models::RosPackage>;

private:
    auto parse_package_xml(const std::string& xml_path) const
        -> std::optional<models::RosPackage>;

    auto find_executables(const std::string& package_path,
                          const std::string& package_name,
                          const std::string& package_type) const
        -> std::vector<std::string>;

    static auto extract_xml_tag(const std::string& content,
                                const std::string& tag) -> std::optional<std::string>;

    static auto should_skip_directory(const std::string& name) -> bool;
};

}  // namespace rosweb::workspace
