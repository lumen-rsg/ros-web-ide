#pragma once

#include <string>
#include <vector>

#include "models/build_models.hpp"

namespace rosweb::build {

class IBuildListener {
public:
    virtual ~IBuildListener() = default;

    virtual void on_build_output(const std::string& build_id,
                                  const std::string& target,
                                  const std::string& stream,
                                  const std::string& data) = 0;

    virtual void on_build_status_changed(const std::string& build_id,
                                          models::BuildStatus status,
                                          const std::vector<models::BuildTargetStatus>& targets) = 0;

    virtual void on_launch_output(const std::string& launch_id,
                                   const std::string& node,
                                   const std::string& stream,
                                   const std::string& data) = 0;

    virtual void on_launch_status_changed(const std::string& launch_id,
                                           models::LaunchStatus status,
                                           int exit_code) = 0;
};

}  // namespace rosweb::build
