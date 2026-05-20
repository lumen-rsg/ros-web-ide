#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <nlohmann/json.hpp>

namespace rosweb::models {

struct CpuInfo {
    std::string model;
    int cores;
    double usagePercent;
};

struct MemoryInfo {
    uint64_t totalBytes;
    uint64_t usedBytes;
    uint64_t availableBytes;
};

struct DiskInfo {
    uint64_t totalBytes;
    uint64_t usedBytes;
    uint64_t availableBytes;
    std::string mountPoint;
};

struct SystemInfo {
    std::string hostname;
    std::string platform;
    std::string os;
    CpuInfo cpu;
    MemoryInfo memory;
    DiskInfo disk;
};

struct RosEnvInfo {
    std::string rosDistro;
    std::string rosVersion;
    std::optional<int> domainId;
    std::unordered_map<std::string, std::string> variables;
};

void to_json(nlohmann::json& j, const CpuInfo& c);
void to_json(nlohmann::json& j, const MemoryInfo& m);
void to_json(nlohmann::json& j, const DiskInfo& d);
void to_json(nlohmann::json& j, const SystemInfo& s);
void to_json(nlohmann::json& j, const RosEnvInfo& r);

}  // namespace rosweb::models
