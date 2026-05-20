#pragma once

#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

namespace rosweb::models {

struct RosNode {
    std::string name;
    std::string node_namespace;
    std::optional<int> pid;
};

struct RosNodesResponse {
    std::vector<RosNode> nodes;
};

struct RosTopic {
    std::string name;
    std::string type;
    int publisher_count = 0;
    int subscriber_count = 0;
};

struct RosTopicsResponse {
    std::vector<RosTopic> topics;
};

struct RosService {
    std::string name;
    std::string type;
    std::string node;
};

struct RosServicesResponse {
    std::vector<RosService> services;
};

struct RosAction {
    std::string name;
    std::string type;
    std::string node;
};

struct RosActionsResponse {
    std::vector<RosAction> actions;
};

struct RosParameter {
    std::string name;
    std::string type;
    std::optional<nlohmann::json> value;
    std::optional<std::string> description;
};

struct RosParamsResponse {
    std::string node;
    std::vector<RosParameter> parameters;
};

struct RosParamSetRequest {
    std::string node;
    std::string name;
    nlohmann::json value;
};

struct RosParamSetResponse {
    std::string node;
    std::string name;
    nlohmann::json value;
    bool success = false;
};

struct RosInterface {
    std::string kind;     // "msg", "srv", "action"
    std::string package;
    std::string name;
};

struct RosInterfacesResponse {
    std::vector<RosInterface> interfaces;
};

struct RosInterfaceField {
    std::string name;
    std::string type;
    bool is_array = false;
    std::optional<nlohmann::json> default_value;
    std::optional<std::vector<RosInterfaceField>> children;
};

struct RosInterfaceDetailResponse {
    std::string type;
    std::vector<RosInterfaceField> fields;
};

// Serialization
void to_json(nlohmann::json& j, const RosNode& v);
void to_json(nlohmann::json& j, const RosNodesResponse& v);
void to_json(nlohmann::json& j, const RosTopic& v);
void to_json(nlohmann::json& j, const RosTopicsResponse& v);
void to_json(nlohmann::json& j, const RosService& v);
void to_json(nlohmann::json& j, const RosServicesResponse& v);
void to_json(nlohmann::json& j, const RosAction& v);
void to_json(nlohmann::json& j, const RosActionsResponse& v);
void to_json(nlohmann::json& j, const RosParameter& v);
void to_json(nlohmann::json& j, const RosParamsResponse& v);
void from_json(const nlohmann::json& j, RosParamSetRequest& v);
void to_json(nlohmann::json& j, const RosParamSetResponse& v);
void to_json(nlohmann::json& j, const RosInterface& v);
void to_json(nlohmann::json& j, const RosInterfacesResponse& v);
void to_json(nlohmann::json& j, const RosInterfaceField& v);
void to_json(nlohmann::json& j, const RosInterfaceDetailResponse& v);

}  // namespace rosweb::models
