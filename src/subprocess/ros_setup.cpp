#include "subprocess/ros_setup.hpp"

#include <cstdlib>
#include <filesystem>

namespace rosweb::subprocess {

auto shell_quote(const std::string& s) -> std::string {
    std::string out = "'";
    for (char c : s) {
        if (c == '\'') {
            out += "'\\''";
        } else {
            out += c;
        }
    }
    out += "'";
    return out;
}

auto wrap_with_ros_setup(const std::string& workspace_root,
                         const std::vector<std::string>& cmd) -> std::vector<std::string> {
    if (cmd.empty()) {
        return cmd;
    }

    std::string script;
    if (const char* distro = std::getenv("ROS_DISTRO"); distro != nullptr && distro[0] != '\0') {
        script += "source /opt/ros/";
        script += distro;
        script += "/setup.bash && ";
    }

    namespace fs = std::filesystem;
    auto install_setup = fs::path(workspace_root) / "install" / "setup.bash";
    if (fs::exists(install_setup)) {
        script += "source ";
        script += shell_quote(install_setup.string());
        script += " && ";
    }

    script += "exec";
    for (const auto& arg : cmd) {
        script += " ";
        script += shell_quote(arg);
    }

    return {"bash", "-lc", script};
}

}  // namespace rosweb::subprocess
