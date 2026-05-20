#pragma once

#include <stdexcept>
#include <string>
#include <nlohmann/json.hpp>

#include "errors/error_codes.hpp"

namespace rosweb::errors {

class ApiException : public std::runtime_error {
public:
    explicit ApiException(ErrorCode code, const std::string& message,
                          nlohmann::json details = {});
    ~ApiException() override = default;

    auto code() const -> ErrorCode;
    auto http_status() const -> int;
    auto code_string() const -> std::string_view;
    auto details() const -> const nlohmann::json&;

private:
    ErrorCode code_;
    nlohmann::json details_;
};

class FsException : public ApiException {
public:
    explicit FsException(ErrorCode code, const std::string& path,
                         const std::string& message = "");
};

class RosException : public ApiException {
public:
    explicit RosException(ErrorCode code, const std::string& context,
                          const std::string& message = "");
};

class BuildException : public ApiException {
public:
    explicit BuildException(ErrorCode code, const std::string& build_id,
                            const std::string& message = "");
};

class TerminalException : public ApiException {
public:
    explicit TerminalException(ErrorCode code, const std::string& terminal_id,
                               const std::string& message = "");
};

}  // namespace rosweb::errors
