#pragma once

#include <string>

namespace rosweb::workspace {

class IWorkspaceAware {
public:
    virtual ~IWorkspaceAware() = default;
    virtual void set_workspace_root(const std::string& root) = 0;
};

}  // namespace rosweb::workspace
