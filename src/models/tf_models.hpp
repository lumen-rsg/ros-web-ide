#pragma once

#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

namespace rosweb::models {

// --- Request models (client → server) ---

struct TfSubscribeRequest {
    std::string subscription_id;
    std::optional<std::vector<std::string>> frames;
    std::optional<int> throttle_rate;
};

// --- Response/push models (server → client) ---

struct TfSubscribedPayload {
    std::string subscription_id;
};

struct TfFrame {
    std::string name;
    std::optional<std::string> parent;
    std::vector<std::string> children;
};

struct TfTreePayload {
    std::vector<TfFrame> frames;
};

struct TfTranslation {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

struct TfRotation {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    double w = 1.0;
};

struct TfTransform {
    std::string parent;
    std::string child;
    TfTranslation translation;
    TfRotation rotation;
    std::string timestamp;
};

struct TfUpdatePayload {
    std::string subscription_id;
    std::vector<TfTransform> transforms;
};

// Serialization declarations
void from_json(const nlohmann::json& j, TfSubscribeRequest& v);

void to_json(nlohmann::json& j, const TfSubscribedPayload& v);
void to_json(nlohmann::json& j, const TfFrame& v);
void to_json(nlohmann::json& j, const TfTreePayload& v);
void to_json(nlohmann::json& j, const TfTranslation& v);
void to_json(nlohmann::json& j, const TfRotation& v);
void to_json(nlohmann::json& j, const TfTransform& v);
void to_json(nlohmann::json& j, const TfUpdatePayload& v);

}  // namespace rosweb::models
