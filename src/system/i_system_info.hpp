#pragma once

#include "models/system_models.hpp"

#include <expected>

#include "errors/error_codes.hpp"

namespace rosweb::system {

class ISystemInfo {
public:
    virtual ~ISystemInfo() = default;
    virtual auto get_system_info() const -> models::SystemInfo = 0;
    virtual auto get_ros_env() const -> models::RosEnvInfo = 0;
    virtual auto set_domain_id(const std::optional<int>& domain_id)
        -> std::expected<void, errors::ErrorCode> = 0;
};

}  // namespace rosweb::system
