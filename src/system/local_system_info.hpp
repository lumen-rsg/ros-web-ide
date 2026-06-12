#pragma once

#include "system/i_system_info.hpp"

namespace rosweb::system {

class LocalSystemInfo : public ISystemInfo {
public:
    auto get_system_info() const -> models::SystemInfo override;
    auto get_ros_env() const -> models::RosEnvInfo override;
    auto set_domain_id(const std::optional<int>& domain_id)
        -> std::expected<void, errors::ErrorCode> override;

private:
    static auto get_hostname() -> std::string;
    static auto get_platform() -> std::string;
    static auto get_os_name() -> std::string;
    static auto get_cpu_info() -> models::CpuInfo;
    static auto get_memory_info() -> models::MemoryInfo;
    static auto get_disk_info() -> models::DiskInfo;
    static auto get_env_var(const char* name) -> std::optional<std::string>;
};

}  // namespace rosweb::system
