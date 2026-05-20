#pragma once

#include <string>
#include <nlohmann/json.hpp>

#include "errors/error_codes.hpp"

namespace rosweb::errors {

auto make_success_json(const nlohmann::json& data) -> std::string;

auto make_error_json(ErrorCode code, const std::string& message,
                     const nlohmann::json& details = {}) -> std::string;

}  // namespace rosweb::errors
