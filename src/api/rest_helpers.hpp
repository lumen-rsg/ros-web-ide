#pragma once

#include <expected>
#include <string>
#include <nlohmann/json.hpp>

#include "errors/error_codes.hpp"
#include "errors/api_exception.hpp"

namespace rosweb::api {

auto make_success(const nlohmann::json& data) -> std::string;

auto make_error(errors::ErrorCode code, const std::string& message,
                const nlohmann::json& details = {}) -> std::pair<int, std::string>;

template<typename T>
auto unwrap(std::expected<T, errors::ErrorCode> result, const std::string& context = "")
    -> T {
    if (result.has_value()) {
        return std::move(result.value());
    }
    throw errors::FsException(result.error(), context);
}

}  // namespace rosweb::api
