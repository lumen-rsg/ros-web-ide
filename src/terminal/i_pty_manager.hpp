#pragma once

#include <expected>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

#include "errors/error_codes.hpp"

namespace rosweb::terminal {

struct PtyCreateParams {
    std::string terminal_id;
    std::optional<std::string> shell;
    std::optional<std::string> cwd;
    std::optional<std::unordered_map<std::string, std::string>> env;
    int cols = 80;
    int rows = 24;
};

class IPtyManager {
public:
    virtual ~IPtyManager() = default;

    virtual auto create(
        const PtyCreateParams& params,
        std::function<void(const std::string& /*id*/, std::string /*data*/)> on_output,
        std::function<void(const std::string& /*id*/, int /*exit_code*/)> on_exit
    ) -> std::expected<int, errors::ErrorCode> = 0;

    virtual auto write(const std::string& terminal_id, std::string_view data)
        -> std::expected<void, errors::ErrorCode> = 0;

    virtual auto resize(const std::string& terminal_id, int cols, int rows)
        -> std::expected<void, errors::ErrorCode> = 0;

    virtual auto kill(const std::string& terminal_id)
        -> std::expected<void, errors::ErrorCode> = 0;

    virtual auto close_all() -> void = 0;

    virtual auto active_count() const -> size_t = 0;

    static constexpr size_t MAX_TERMINALS = 10;
};

}  // namespace rosweb::terminal
