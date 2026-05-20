#include "workspace/package_discovery.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace rosweb::workspace {

auto PackageDiscovery::discover_packages(const std::string& workspace_root) const
    -> std::vector<models::RosPackage> {
    std::vector<models::RosPackage> packages;
    namespace fs = std::filesystem;

    std::error_code ec;
    fs::path root(workspace_root);
    if (!fs::is_directory(root, ec)) {
        return packages;
    }

    for (auto it = fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied, ec);
         it != fs::recursive_directory_iterator(); ) {
        if (ec) {
            it.increment(ec);
            continue;
        }

        const auto& entry = *it;
        if (entry.is_directory()) {
            auto dirname = entry.path().filename().string();
            if (should_skip_directory(dirname)) {
                it.disable_recursion_pending();
                it.increment(ec);
                continue;
            }
        }

        if (entry.is_regular_file() && entry.path().filename() == "package.xml") {
            auto pkg = parse_package_xml(entry.path().string());
            if (pkg.has_value()) {
                pkg->path = entry.path().parent_path().string();
                pkg->executables = find_executables(pkg->path, pkg->name, pkg->type);
                packages.push_back(std::move(*pkg));
            }
        }

        it.increment(ec);
    }

    // Sort packages by name for deterministic output
    std::sort(packages.begin(), packages.end(),
              [](const models::RosPackage& a, const models::RosPackage& b) {
                  return a.name < b.name;
              });

    return packages;
}

auto PackageDiscovery::parse_package_xml(const std::string& xml_path) const
    -> std::optional<models::RosPackage> {
    std::ifstream file(xml_path);
    if (!file.is_open()) {
        return std::nullopt;
    }

    std::ostringstream ss;
    ss << file.rdbuf();
    std::string content = ss.str();

    // Extract package name
    auto name = extract_xml_tag(content, "name");
    if (!name.has_value() || name->empty()) {
        return std::nullopt;
    }

    models::RosPackage pkg;
    pkg.name = *name;

    // Determine build type: <build_type> in <export> takes precedence (format 3),
    // then fall back to <buildtool_depend>
    if (content.find("<build_type>ament_cmake</build_type>") != std::string::npos) {
        pkg.type = "ament_cmake";
    } else if (content.find("<build_type>ament_python</build_type>") != std::string::npos) {
        pkg.type = "ament_python";
    } else if (content.find("<buildtool_depend>ament_cmake</buildtool_depend>") != std::string::npos) {
        pkg.type = "ament_cmake";
    } else if (content.find("<buildtool_depend>ament_python</buildtool_depend>") != std::string::npos) {
        pkg.type = "ament_python";
    } else {
        // Default to ament_cmake for unrecognized packages
        pkg.type = "ament_cmake";
    }

    return pkg;
}

auto PackageDiscovery::find_executables(const std::string& package_path,
                                         const std::string& package_name,
                                         const std::string& package_type) const
    -> std::vector<std::string> {
    std::vector<std::string> executables;
    namespace fs = std::filesystem;

    // Primary: check install/<pkg>/lib/<pkg>/ for executables
    {
        fs::path install_lib = fs::path(package_path).parent_path().parent_path() /
                               "install" / package_name / "lib" / package_name;
        std::error_code ec;
        if (fs::is_directory(install_lib, ec)) {
            for (const auto& entry : fs::directory_iterator(install_lib, ec)) {
                if (entry.is_regular_file() && (entry.status().permissions() & fs::perms::owner_exec) != fs::perms::none) {
                    executables.push_back(entry.path().filename().string());
                }
            }
        }
    }

    // Fallback: parse build files if install dir not found
    if (executables.empty()) {
        if (package_type == "ament_cmake") {
            fs::path cmake_path = fs::path(package_path) / "CMakeLists.txt";
            std::ifstream file(cmake_path);
            if (file.is_open()) {
                std::string line;
                while (std::getline(file, line)) {
                    // Match add_executable(name ... or add_executable(name...
                    auto pos = line.find("add_executable(");
                    if (pos != std::string::npos) {
                        auto start = pos + 15;
                        // Skip whitespace
                        while (start < line.size() && (line[start] == ' ' || line[start] == '\t')) {
                            ++start;
                        }
                        auto end = start;
                        while (end < line.size() && line[end] != ' ' && line[end] != '\t' && line[end] != ')' && line[end] != '\n') {
                            ++end;
                        }
                        if (end > start) {
                            executables.push_back(line.substr(start, end - start));
                        }
                    }
                }
            }
        } else if (package_type == "ament_python") {
            fs::path setup_path = fs::path(package_path) / "setup.py";
            std::ifstream file(setup_path);
            if (file.is_open()) {
                std::string line;
                bool in_console_scripts = false;
                while (std::getline(file, line)) {
                    if (line.find("'console_scripts'") != std::string::npos ||
                        line.find("\"console_scripts\"") != std::string::npos) {
                        in_console_scripts = true;
                        continue;
                    }
                    if (in_console_scripts) {
                        // Look for entries like 'name = module:function'
                        // Find content between single quotes
                        auto q1 = line.find('\'');
                        if (q1 == std::string::npos) {
                            q1 = line.find('"');
                        }
                        if (q1 != std::string::npos) {
                            auto eq = line.find('=', q1);
                            if (eq != std::string::npos) {
                                std::string name = line.substr(q1 + 1, eq - q1 - 1);
                                // Trim whitespace
                                auto end = name.find_last_not_of(" \t");
                                if (end != std::string::npos) {
                                    name = name.substr(0, end + 1);
                                }
                                if (!name.empty()) {
                                    executables.push_back(name);
                                }
                            }
                        }
                        if (line.find(']') != std::string::npos) {
                            break;
                        }
                    }
                }
            }
        }
    }

    std::sort(executables.begin(), executables.end());
    return executables;
}

auto PackageDiscovery::extract_xml_tag(const std::string& content,
                                        const std::string& tag) -> std::optional<std::string> {
    std::string open_tag = "<" + tag + ">";
    std::string close_tag = "</" + tag + ">";

    auto start = content.find(open_tag);
    if (start == std::string::npos) {
        return std::nullopt;
    }
    start += open_tag.size();

    auto end = content.find(close_tag, start);
    if (end == std::string::npos) {
        return std::nullopt;
    }

    return content.substr(start, end - start);
}

auto PackageDiscovery::should_skip_directory(const std::string& name) -> bool {
    static const std::string skip_dirs[] = {
        ".git", "build", "install", "log", ".cache", ".vscode"
    };
    for (const auto& d : skip_dirs) {
        if (name == d) return true;
    }
    return false;
}

}  // namespace rosweb::workspace
