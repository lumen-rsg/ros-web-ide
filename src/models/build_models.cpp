#include "models/build_models.hpp"

namespace rosweb::models {

auto build_status_to_string(BuildStatus s) -> std::string {
    switch (s) {
        case BuildStatus::running:   return "running";
        case BuildStatus::completed: return "completed";
        case BuildStatus::failed:    return "failed";
        case BuildStatus::cancelled: return "cancelled";
    }
    return "unknown";
}

auto launch_status_to_string(LaunchStatus s) -> std::string {
    switch (s) {
        case LaunchStatus::running: return "running";
        case LaunchStatus::stopped: return "stopped";
    }
    return "unknown";
}

// --- to_json ---

void to_json(nlohmann::json& j, const BuildTargetStatus& t) {
    j = nlohmann::json{
        {"name", t.name},
        {"status", build_status_to_string(t.status)},
    };
    if (t.return_code.has_value()) {
        j["returnCode"] = t.return_code.value();
    }
}

void to_json(nlohmann::json& j, const BuildResponse& r) {
    j = nlohmann::json{
        {"buildId", r.build_id},
        {"status", build_status_to_string(r.status)},
    };
}

void to_json(nlohmann::json& j, const BuildStatusResponse& r) {
    j = nlohmann::json{
        {"buildId", r.build_id},
        {"status", build_status_to_string(r.status)},
    };
    nlohmann::json targets_json = nlohmann::json::object();
    for (const auto& t : r.targets) {
        nlohmann::json tj;
        to_json(tj, t);
        targets_json[t.name] = {{"status", tj["status"]}};
        if (t.return_code.has_value()) {
            targets_json[t.name]["returnCode"] = t.return_code.value();
        }
    }
    j["targets"] = targets_json;
}

void to_json(nlohmann::json& j, const LaunchResponse& r) {
    j = nlohmann::json{
        {"launchId", r.launch_id},
        {"status", launch_status_to_string(r.status)},
        {"pid", r.pid},
    };
}

void to_json(nlohmann::json& j, const LaunchStopResponse& r) {
    j = nlohmann::json{
        {"launchId", r.launch_id},
        {"status", launch_status_to_string(r.status)},
    };
}

void to_json(nlohmann::json& j, const LaunchArgument& a) {
    j = nlohmann::json{
        {"name", a.name},
        {"type", a.type},
    };
    if (a.default_value.has_value()) {
        j["default"] = a.default_value.value();
    }
    if (a.description.has_value()) {
        j["description"] = a.description.value();
    }
}

void to_json(nlohmann::json& j, const LaunchFileInfo& f) {
    j = nlohmann::json{
        {"path", f.path},
        {"package", f.package},
    };
    j["arguments"] = nlohmann::json::array();
    for (const auto& a : f.arguments) {
        nlohmann::json aj;
        to_json(aj, a);
        j["arguments"].push_back(aj);
    }
}

void to_json(nlohmann::json& j, const LaunchFilesResponse& r) {
    j = nlohmann::json{{"files", nlohmann::json::array()}};
    for (const auto& f : r.files) {
        nlohmann::json fj;
        to_json(fj, f);
        j["files"].push_back(fj);
    }
}

void to_json(nlohmann::json& j, const BuildOutputPayload& p) {
    j = nlohmann::json{
        {"buildId", p.build_id},
        {"stream", p.stream},
        {"data", p.data},
    };
    if (p.target.has_value()) {
        j["target"] = p.target.value();
    }
}

void to_json(nlohmann::json& j, const BuildStatusPayload& p) {
    j = nlohmann::json{
        {"buildId", p.build_id},
        {"status", build_status_to_string(p.status)},
    };
    nlohmann::json targets_json = nlohmann::json::object();
    for (const auto& t : p.targets) {
        nlohmann::json tj;
        to_json(tj, t);
        targets_json[t.name] = {{"status", tj["status"]}};
        if (t.return_code.has_value()) {
            targets_json[t.name]["returnCode"] = t.return_code.value();
        }
    }
    j["targets"] = targets_json;
}

void to_json(nlohmann::json& j, const LaunchOutputPayload& p) {
    j = nlohmann::json{
        {"launchId", p.launch_id},
        {"stream", p.stream},
        {"data", p.data},
    };
    if (p.node.has_value()) {
        j["node"] = p.node.value();
    }
}

void to_json(nlohmann::json& j, const LaunchStatusPayload& p) {
    j = nlohmann::json{
        {"launchId", p.launch_id},
        {"status", launch_status_to_string(p.status)},
    };
    if (p.exit_code.has_value()) {
        j["exitCode"] = p.exit_code.value();
    }
}

// --- from_json ---

void from_json(const nlohmann::json& j, BuildRequest& r) {
    if (j.contains("targets") && !j["targets"].is_null()) {
        r.targets = j["targets"].get<std::vector<std::string>>();
    }
    if (j.contains("args") && !j["args"].is_null()) {
        r.args = j["args"].get<std::vector<std::string>>();
    }
    r.clean = j.value("clean", false);
}

void from_json(const nlohmann::json& j, LaunchRequest& r) {
    j.at("file").get_to(r.file);
    if (j.contains("arguments") && !j["arguments"].is_null()) {
        r.arguments = j["arguments"].get<std::map<std::string, std::string>>();
    }
}

void from_json(const nlohmann::json& j, LaunchStopRequest& r) {
    j.at("launchId").get_to(r.launch_id);
}

}  // namespace rosweb::models
