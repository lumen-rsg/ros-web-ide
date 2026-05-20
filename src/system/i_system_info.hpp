#pragma once

#include "models/system_models.hpp"

namespace rosweb::system {

class ISystemInfo {
public:
    virtual ~ISystemInfo() = default;
    virtual auto get_system_info() const -> models::SystemInfo = 0;
    virtual auto get_ros_env() const -> models::RosEnvInfo = 0;
};

}  // namespace rosweb::system
