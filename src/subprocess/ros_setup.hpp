#pragma once

#include <string>
#include <vector>

namespace rosweb::subprocess {

auto shell_quote(const std::string& s) -> std::string;

/// Wrap a command to run in bash with ROS underlay and workspace install sourced.
auto wrap_with_ros_setup(const std::string& workspace_root,
                         const std::vector<std::string>& cmd) -> std::vector<std::string>;

}  // namespace rosweb::subprocess
