#include "errors/error_response.hpp"

namespace rosweb::errors {

auto make_success_json(const nlohmann::json& data) -> std::string {
    nlohmann::json envelope;
    envelope["ok"] = true;
    envelope["data"] = data;
    return envelope.dump();
}

auto make_error_json(ErrorCode code, const std::string& message,
                     const nlohmann::json& details) -> std::string {
    nlohmann::json envelope;
    envelope["ok"] = false;
    envelope["error"]["code"] = error_code_to_string(code);
    envelope["error"]["message"] = message;
    if (!details.is_null()) {
        envelope["error"]["details"] = details;
    }
    return envelope.dump();
}

}  // namespace rosweb::errors
