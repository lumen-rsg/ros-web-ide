#include "system/local_system_info.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <thread>
#include <unistd.h>

#ifdef LINUX
#include <sys/utsname.h>
#include <fstream>
#include <sstream>
#include <numeric>
#include <vector>
#endif

#ifdef MACOS
#include <sys/utsname.h>
#include <mach/mach.h>
#include <sys/sysctl.h>
#include <fstream>
#endif

namespace rosweb::system {

auto LocalSystemInfo::get_system_info() const -> models::SystemInfo {
    models::SystemInfo info;
    info.hostname = get_hostname();
    info.platform = get_platform();
    info.os = get_os_name();
    info.cpu = get_cpu_info();
    info.memory = get_memory_info();
    info.disk = get_disk_info();
    return info;
}

auto LocalSystemInfo::get_ros_env() const -> models::RosEnvInfo {
    models::RosEnvInfo info;

    if (auto v = get_env_var("ROS_DISTRO")) {
        info.rosDistro = *v;
    }
    if (auto v = get_env_var("ROS_VERSION")) {
        info.rosVersion = *v;
    }
    if (auto v = get_env_var("ROS_DOMAIN_ID")) {
        try {
            info.domainId = std::stoi(*v);
        } catch (...) {}
    }

    static const char* ros_vars[] = {
        "ROS_DOMAIN_ID", "AMENT_PREFIX_PATH", "LD_LIBRARY_PATH",
        "PATH", "PYTHONPATH", "CMAKE_PREFIX_PATH",
        "ROS_ROOT", "ROS_PACKAGE_PATH",
    };
    for (const char* var : ros_vars) {
        if (auto val = get_env_var(var)) {
            info.variables[var] = *val;
        }
    }

    return info;
}

auto LocalSystemInfo::get_hostname() -> std::string {
    char buf[256];
    if (gethostname(buf, sizeof(buf)) == 0) {
        return std::string(buf);
    }
    return "unknown";
}

auto LocalSystemInfo::get_platform() -> std::string {
    struct utsname info;
    if (uname(&info) == 0) {
        return std::string(info.machine);
    }
    return "unknown";
}

auto LocalSystemInfo::get_os_name() -> std::string {
#ifdef LINUX
    std::ifstream file("/etc/os-release");
    std::string line;
    while (std::getline(file, line)) {
        if (line.starts_with("PRETTY_NAME=")) {
            auto val = line.substr(12);
            // Strip quotes
            if (val.size() >= 2 && val.front() == '"' && val.back() == '"') {
                val = val.substr(1, val.size() - 2);
            }
            return val;
        }
    }
    return "Linux";
#endif

#ifdef MACOS
    struct utsname info;
    if (uname(&info) == 0) {
        return std::string("macOS ") + info.release;
    }
    return "macOS";
#endif

    return "unknown";
}

auto LocalSystemInfo::get_cpu_info() -> models::CpuInfo {
    models::CpuInfo info;
    info.cores = static_cast<int>(std::thread::hardware_concurrency());
    info.usagePercent = 0.0;

#ifdef LINUX
    // Read CPU model from /proc/cpuinfo
    {
        std::ifstream file("/proc/cpuinfo");
        std::string line;
        while (std::getline(file, line)) {
            if (line.starts_with("model name")) {
                auto pos = line.find(':');
                if (pos != std::string::npos) {
                    info.model = line.substr(pos + 2);
                }
                break;
            }
        }
    }
    // CPU usage from /proc/stat (cumulative since boot)
    {
        std::ifstream file("/proc/stat");
        std::string label;
        unsigned long user, nice, system, idle, iowait, irq, softirq, steal;
        if (file >> label >> user >> nice >> system >> idle >> iowait >> irq >> softirq >> steal) {
            unsigned long total = user + nice + system + idle + iowait + irq + softirq + steal;
            if (total > 0) {
                info.usagePercent = 100.0 * (1.0 - static_cast<double>(idle + iowait) / static_cast<double>(total));
            }
        }
    }
#endif

#ifdef MACOS
    // CPU model from sysctl
    {
        char buf[256];
        size_t len = sizeof(buf);
        if (sysctlbyname("machdep.cpu.brand_string", buf, &len, nullptr, 0) == 0) {
            info.model = std::string(buf, len > 0 ? len - 1 : 0);
        }
    }
    // CPU usage from host_processor_info (cumulative)
    {
        natural_t num_cpus = 0;
        processor_info_array_t cpu_info_arr = nullptr;
        mach_msg_type_number_t num_cpu_info = 0;
        kern_return_t kr = host_processor_info(mach_host_self(), PROCESSOR_CPU_LOAD_INFO,
                                                &num_cpus, &cpu_info_arr, &num_cpu_info);
        if (kr == KERN_SUCCESS) {
            unsigned long long total_ticks = 0;
            unsigned long long idle_ticks = 0;
            for (natural_t i = 0; i < num_cpus; ++i) {
                unsigned long long user     = cpu_info_arr[CPU_STATE_MAX * i + CPU_STATE_USER];
                unsigned long long system   = cpu_info_arr[CPU_STATE_MAX * i + CPU_STATE_SYSTEM];
                unsigned long long nice     = cpu_info_arr[CPU_STATE_MAX * i + CPU_STATE_NICE];
                unsigned long long idle     = cpu_info_arr[CPU_STATE_MAX * i + CPU_STATE_IDLE];
                total_ticks += user + system + nice + idle;
                idle_ticks += idle;
            }
            if (total_ticks > 0) {
                info.usagePercent = 100.0 * (1.0 - static_cast<double>(idle_ticks) / static_cast<double>(total_ticks));
            }
            // Deallocate vm memory from host_processor_info
            vm_deallocate(mach_task_self(), reinterpret_cast<vm_address_t>(cpu_info_arr),
                          num_cpu_info * sizeof(integer_t));
        }
    }
#endif

    return info;
}

auto LocalSystemInfo::get_memory_info() -> models::MemoryInfo {
    models::MemoryInfo info{};

#ifdef LINUX
    unsigned long total = 0, available = 0;
    std::ifstream file("/proc/meminfo");
    std::string line;
    while (std::getline(file, line)) {
        if (line.starts_with("MemTotal:")) {
            auto pos = line.find(':');
            if (pos != std::string::npos) {
                total = std::stoul(line.substr(pos + 1)) * 1024;  // kB to bytes
            }
        } else if (line.starts_with("MemAvailable:")) {
            auto pos = line.find(':');
            if (pos != std::string::npos) {
                available = std::stoul(line.substr(pos + 1)) * 1024;
            }
        }
        if (total > 0 && available > 0) break;
    }
    info.totalBytes = total;
    info.availableBytes = available;
    info.usedBytes = total - available;
#endif

#ifdef MACOS
    // Total memory from sysctl
    {
        int64_t memsize = 0;
        size_t len = sizeof(memsize);
        if (sysctlbyname("hw.memsize", &memsize, &len, nullptr, 0) == 0) {
            info.totalBytes = static_cast<uint64_t>(memsize);
        }
    }
    // Available memory from host_statistics64
    {
        vm_statistics64_data_t vm_stats;
        mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
        if (host_statistics64(mach_host_self(), HOST_VM_INFO64,
                              reinterpret_cast<host_info64_t>(&vm_stats), &count) == KERN_SUCCESS) {
            // page_size is available from the host
            vm_size_t page_size_val = 0;
            host_page_size(mach_host_self(), &page_size_val);
            info.availableBytes = static_cast<uint64_t>(vm_stats.free_count) * page_size_val;
            info.usedBytes = info.totalBytes - info.availableBytes;
        }
    }
#endif

    return info;
}

auto LocalSystemInfo::get_disk_info() -> models::DiskInfo {
    models::DiskInfo info{};
    info.mountPoint = "/";
    try {
        auto space = std::filesystem::space(info.mountPoint);
        info.totalBytes = space.capacity;
        info.availableBytes = space.available;
        info.usedBytes = space.capacity - space.free;
    } catch (...) {}
    return info;
}

auto LocalSystemInfo::get_env_var(const char* name) -> std::optional<std::string> {
    const char* val = std::getenv(name);
    if (val && val[0] != '\0') {
        return std::string(val);
    }
    return std::nullopt;
}

auto LocalSystemInfo::set_domain_id(const std::optional<int>& domain_id)
    -> std::expected<void, errors::ErrorCode> {
    // Resolve ~/.zshrc path
    const char* home = std::getenv("HOME");
    if (!home || home[0] == '\0') {
        return std::unexpected(errors::ErrorCode::INTERNAL_ERROR);
    }

    auto zshrc_path = std::filesystem::path(home) / ".zshrc";

    // Read existing content
    std::vector<std::string> lines;
    if (std::filesystem::exists(zshrc_path)) {
        std::ifstream file(zshrc_path);
        std::string line;
        while (std::getline(file, line)) {
            lines.push_back(line);
        }
    }

    const std::string marker = "export ROS_DOMAIN_ID=";
    bool found = false;

    if (domain_id.has_value()) {
        std::string new_line = marker + std::to_string(domain_id.value());
        for (auto& l : lines) {
            if (l.starts_with(marker) || l.find("ROS_DOMAIN_ID=") != std::string::npos) {
                // Replace existing line that sets ROS_DOMAIN_ID
                auto eq_pos = l.find("ROS_DOMAIN_ID=");
                auto prefix = l.substr(0, eq_pos);
                // Check if it's an export line
                if (prefix.find("export") != std::string::npos || l.starts_with("ROS_DOMAIN_ID=")) {
                    l = new_line;
                    found = true;
                }
            }
        }
        if (!found) {
            lines.push_back(new_line);
        }

        // Update current process environment
        std::string val_str = std::to_string(domain_id.value());
        ::setenv("ROS_DOMAIN_ID", val_str.c_str(), 1);
    } else {
        // Unset: remove the line
        lines.erase(
            std::remove_if(lines.begin(), lines.end(),
                [&marker](const std::string& l) {
                    return l.starts_with(marker);
                }),
            lines.end());

        ::unsetenv("ROS_DOMAIN_ID");
    }

    // Write back
    std::ofstream file(zshrc_path, std::ios::trunc);
    if (!file.is_open()) {
        return std::unexpected(errors::ErrorCode::FS_WRITE_FAILED);
    }
    for (size_t i = 0; i < lines.size(); ++i) {
        file << lines[i];
        if (i + 1 < lines.size()) file << '\n';
    }
    if (!lines.empty()) file << '\n';

    return {};
}

}  // namespace rosweb::system
