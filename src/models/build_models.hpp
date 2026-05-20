#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace rosweb::models {

enum class BuildStatus { running, completed, failed, cancelled };
enum class LaunchStatus { running, stopped };

// --- REST request models ---

struct BuildRequest {
    std::optional<std::vector<std::string>> targets;
    std::optional<std::vector<std::string>> args;
    bool clean = false;
};

struct LaunchRequest {
    std::string file;
    std::optional<std::map<std::string, std::string>> arguments;
};

struct LaunchStopRequest {
    std::string launch_id;
};

// --- REST response models ---

struct BuildTargetStatus {
    std::string name;
    BuildStatus status;
    std::optional<int> return_code;
};

struct BuildResponse {
    std::string build_id;
    BuildStatus status;
};

struct BuildStatusResponse {
    std::string build_id;
    BuildStatus status;
    std::vector<BuildTargetStatus> targets;
};

struct LaunchResponse {
    std::string launch_id;
    LaunchStatus status;
    int pid;
};

struct LaunchStopResponse {
    std::string launch_id;
    LaunchStatus status;
};

// --- Launch file discovery ---

struct LaunchArgument {
    std::string name;
    std::string type;
    std::optional<std::string> default_value;
    std::optional<std::string> description;
};

struct LaunchFileInfo {
    std::string path;
    std::string package;
    std::vector<LaunchArgument> arguments;
};

struct LaunchFilesResponse {
    std::vector<LaunchFileInfo> files;
};

// --- WS event payload models ---

struct BuildOutputPayload {
    std::string build_id;
    std::optional<std::string> target;
    std::string stream;
    std::string data;
};

struct BuildStatusPayload {
    std::string build_id;
    BuildStatus status;
    std::vector<BuildTargetStatus> targets;
};

struct LaunchOutputPayload {
    std::string launch_id;
    std::optional<std::string> node;
    std::string stream;
    std::string data;
};

struct LaunchStatusPayload {
    std::string launch_id;
    LaunchStatus status;
    std::optional<int> exit_code;
};

// --- Enum helpers ---
auto build_status_to_string(BuildStatus s) -> std::string;
auto launch_status_to_string(LaunchStatus s) -> std::string;

// --- to_json declarations ---
void to_json(nlohmann::json& j, const BuildTargetStatus& t);
void to_json(nlohmann::json& j, const BuildResponse& r);
void to_json(nlohmann::json& j, const BuildStatusResponse& r);
void to_json(nlohmann::json& j, const LaunchResponse& r);
void to_json(nlohmann::json& j, const LaunchStopResponse& r);
void to_json(nlohmann::json& j, const LaunchArgument& a);
void to_json(nlohmann::json& j, const LaunchFileInfo& f);
void to_json(nlohmann::json& j, const LaunchFilesResponse& r);
void to_json(nlohmann::json& j, const BuildOutputPayload& p);
void to_json(nlohmann::json& j, const BuildStatusPayload& p);
void to_json(nlohmann::json& j, const LaunchOutputPayload& p);
void to_json(nlohmann::json& j, const LaunchStatusPayload& p);

// --- from_json declarations ---
void from_json(const nlohmann::json& j, BuildRequest& r);
void from_json(const nlohmann::json& j, LaunchRequest& r);
void from_json(const nlohmann::json& j, LaunchStopRequest& r);

}  // namespace rosweb::models
