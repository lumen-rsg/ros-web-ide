#include "models/workspace_models.hpp"

namespace rosweb::models {

void to_json(nlohmann::json& j, const RosPackage& p) {
    j = nlohmann::json{
        {"name",         p.name},
        {"path",         p.path},
        {"type",         p.type},
        {"executables",  p.executables},
    };
}

void to_json(nlohmann::json& j, const WorkspaceInfo& w) {
    j = nlohmann::json{
        {"rootPath",  w.rootPath},
        {"name",      w.name},
        {"rosDistro", w.rosDistro},
        {"packages",  w.packages},
    };
}

}  // namespace rosweb::models
