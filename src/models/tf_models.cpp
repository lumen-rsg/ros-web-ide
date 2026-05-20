#include "models/tf_models.hpp"

namespace rosweb::models {

// --- from_json ---

void from_json(const nlohmann::json& j, TfSubscribeRequest& v) {
    v.subscription_id = j.at("subscriptionId").get<std::string>();
    if (j.contains("frames") && !j["frames"].is_null()) {
        v.frames = j["frames"].get<std::vector<std::string>>();
    }
    if (j.contains("throttleRate") && !j["throttleRate"].is_null()) {
        v.throttle_rate = j["throttleRate"].get<int>();
    }
}

// --- to_json ---

void to_json(nlohmann::json& j, const TfSubscribedPayload& v) {
    j = nlohmann::json{
        {"subscriptionId", v.subscription_id},
    };
}

void to_json(nlohmann::json& j, const TfFrame& v) {
    j = nlohmann::json{
        {"name", v.name},
        {"children", v.children},
    };
    if (v.parent.has_value()) {
        j["parent"] = v.parent.value();
    } else {
        j["parent"] = nullptr;
    }
}

void to_json(nlohmann::json& j, const TfTreePayload& v) {
    nlohmann::json frames_arr = nlohmann::json::array();
    for (const auto& frame : v.frames) {
        nlohmann::json fj;
        to_json(fj, frame);
        frames_arr.push_back(fj);
    }
    j = nlohmann::json{{"frames", frames_arr}};
}

void to_json(nlohmann::json& j, const TfTranslation& v) {
    j = nlohmann::json{
        {"x", v.x},
        {"y", v.y},
        {"z", v.z},
    };
}

void to_json(nlohmann::json& j, const TfRotation& v) {
    j = nlohmann::json{
        {"x", v.x},
        {"y", v.y},
        {"z", v.z},
        {"w", v.w},
    };
}

void to_json(nlohmann::json& j, const TfTransform& v) {
    j = nlohmann::json{
        {"parent", v.parent},
        {"child", v.child},
        {"translation", v.translation},
        {"rotation", v.rotation},
        {"timestamp", v.timestamp},
    };
}

void to_json(nlohmann::json& j, const TfUpdatePayload& v) {
    j = nlohmann::json{
        {"subscriptionId", v.subscription_id},
        {"transforms", v.transforms},
    };
}

}  // namespace rosweb::models
