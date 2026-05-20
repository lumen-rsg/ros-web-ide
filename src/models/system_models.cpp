#include "models/system_models.hpp"

namespace rosweb::models {

void to_json(nlohmann::json& j, const CpuInfo& c) {
    j = nlohmann::json{
        {"model",        c.model},
        {"cores",        c.cores},
        {"usagePercent", c.usagePercent},
    };
}

void to_json(nlohmann::json& j, const MemoryInfo& m) {
    j = nlohmann::json{
        {"totalBytes",     m.totalBytes},
        {"usedBytes",      m.usedBytes},
        {"availableBytes", m.availableBytes},
    };
}

void to_json(nlohmann::json& j, const DiskInfo& d) {
    j = nlohmann::json{
        {"totalBytes",     d.totalBytes},
        {"usedBytes",      d.usedBytes},
        {"availableBytes", d.availableBytes},
        {"mountPoint",     d.mountPoint},
    };
}

void to_json(nlohmann::json& j, const SystemInfo& s) {
    j = nlohmann::json{
        {"hostname", s.hostname},
        {"platform", s.platform},
        {"os",       s.os},
        {"cpu",      s.cpu},
        {"memory",   s.memory},
        {"disk",     s.disk},
    };
}

void to_json(nlohmann::json& j, const RosEnvInfo& r) {
    j = nlohmann::json{
        {"rosDistro", r.rosDistro},
        {"rosVersion", r.rosVersion},
        {"domainId", r.domainId.value_or(-1)},
        {"variables", r.variables},
    };
}

}  // namespace rosweb::models
