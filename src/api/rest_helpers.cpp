#include "api/rest_helpers.hpp"

namespace rosweb::api {

auto make_success(const nlohmann::json& data) -> std::string {
    nlohmann::json envelope;
    envelope["ok"] = true;
    envelope["data"] = data;
    return envelope.dump();
}

auto make_error(errors::ErrorCode code, const std::string& message,
                const nlohmann::json& details) -> std::pair<int, std::string> {
    nlohmann::json envelope;
    envelope["ok"] = false;
    envelope["error"]["code"] = errors::error_code_to_string(code);
    envelope["error"]["message"] = message;
    if (!details.is_null()) {
        envelope["error"]["details"] = details;
    }
    return {errors::error_code_to_http_status(code), envelope.dump()};
}

}  // namespace rosweb::api
