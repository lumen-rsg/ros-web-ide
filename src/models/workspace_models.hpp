#pragma once

#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace rosweb::models {

struct RosPackage {
    std::string name;
    std::string path;
    std::string type;  // "ament_cmake" | "ament_python"
    std::vector<std::string> executables;
};

struct WorkspaceInfo {
    std::string rootPath;
    std::string name;
    std::string rosDistro;
    std::vector<RosPackage> packages;
};

void to_json(nlohmann::json& j, const RosPackage& p);
void to_json(nlohmann::json& j, const WorkspaceInfo& w);

}  // namespace rosweb::models
