#include "models/ros_models.hpp"

namespace rosweb::models {

void to_json(nlohmann::json& j, const RosNode& v) {
    j = nlohmann::json{
        {"name", v.name},
        {"namespace", v.node_namespace},
    };
    if (v.pid.has_value()) {
        j["pid"] = v.pid.value();
    }
}

void to_json(nlohmann::json& j, const RosNodesResponse& v) {
    j = nlohmann::json{{"nodes", v.nodes}};
}

void to_json(nlohmann::json& j, const RosTopic& v) {
    j = nlohmann::json{
        {"name", v.name},
        {"type", v.type},
        {"publisherCount", v.publisher_count},
        {"subscriberCount", v.subscriber_count},
    };
}

void to_json(nlohmann::json& j, const RosTopicsResponse& v) {
    j = nlohmann::json{{"topics", v.topics}};
}

void to_json(nlohmann::json& j, const RosService& v) {
    j = nlohmann::json{
        {"name", v.name},
        {"type", v.type},
        {"node", v.node},
    };
}

void to_json(nlohmann::json& j, const RosServicesResponse& v) {
    j = nlohmann::json{{"services", v.services}};
}

void to_json(nlohmann::json& j, const RosAction& v) {
    j = nlohmann::json{
        {"name", v.name},
        {"type", v.type},
        {"node", v.node},
    };
}

void to_json(nlohmann::json& j, const RosActionsResponse& v) {
    j = nlohmann::json{{"actions", v.actions}};
}

void to_json(nlohmann::json& j, const RosParameter& v) {
    j = nlohmann::json{
        {"name", v.name},
        {"type", v.type},
    };
    if (v.value.has_value()) {
        j["value"] = v.value.value();
    }
    if (v.description.has_value()) {
        j["description"] = v.description.value();
    }
}

void to_json(nlohmann::json& j, const RosParamsResponse& v) {
    j = nlohmann::json{
        {"node", v.node},
        {"parameters", v.parameters},
    };
}

void from_json(const nlohmann::json& j, RosParamSetRequest& v) {
    v.node = j.at("node").get<std::string>();
    v.name = j.at("name").get<std::string>();
    v.value = j.at("value");
}

void to_json(nlohmann::json& j, const RosParamSetResponse& v) {
    j = nlohmann::json{
        {"node", v.node},
        {"name", v.name},
        {"value", v.value},
        {"success", v.success},
    };
}

void to_json(nlohmann::json& j, const RosInterface& v) {
    j = nlohmann::json{
        {"kind", v.kind},
        {"package", v.package},
        {"name", v.name},
    };
}

void to_json(nlohmann::json& j, const RosInterfacesResponse& v) {
    j = nlohmann::json{{"interfaces", v.interfaces}};
}

void to_json(nlohmann::json& j, const RosInterfaceField& v) {
    j = nlohmann::json{
        {"name", v.name},
        {"type", v.type},
        {"isArray", v.is_array},
    };
    if (v.default_value.has_value()) {
        j["defaultValue"] = v.default_value.value();
    }
    if (v.children.has_value()) {
        j["children"] = v.children.value();
    }
}

void to_json(nlohmann::json& j, const RosInterfaceDetailResponse& v) {
    j = nlohmann::json{
        {"type", v.type},
        {"fields", v.fields},
    };
}

}  // namespace rosweb::models
