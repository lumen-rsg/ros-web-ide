#include "errors/api_exception.hpp"

namespace rosweb::errors {

ApiException::ApiException(ErrorCode code, const std::string& message,
                           nlohmann::json details)
    : std::runtime_error(message), code_(code), details_(std::move(details)) {}

auto ApiException::code() const -> ErrorCode { return code_; }

auto ApiException::http_status() const -> int {
    return error_code_to_http_status(code_);
}

auto ApiException::code_string() const -> std::string_view {
    return error_code_to_string(code_);
}

auto ApiException::details() const -> const nlohmann::json& {
    return details_;
}

FsException::FsException(ErrorCode code, const std::string& path,
                         const std::string& message)
    : ApiException(code,
                   message.empty() ? std::string(error_code_to_string(code))
                                    + ": " + path
                                   : message + ": " + path) {}

RosException::RosException(ErrorCode code, const std::string& context,
                           const std::string& message)
    : ApiException(code,
                   message.empty() ? std::string(error_code_to_string(code))
                                    + ": " + context
                                   : message + ": " + context) {}

BuildException::BuildException(ErrorCode code, const std::string& build_id,
                               const std::string& message)
    : ApiException(code,
                   message.empty() ? std::string(error_code_to_string(code))
                                    + ": " + build_id
                                   : message + ": " + build_id) {}

TerminalException::TerminalException(ErrorCode code, const std::string& terminal_id,
                                     const std::string& message)
    : ApiException(code,
                   message.empty() ? std::string(error_code_to_string(code))
                                    + ": " + terminal_id
                                   : message + ": " + terminal_id) {}

}  // namespace rosweb::errors
