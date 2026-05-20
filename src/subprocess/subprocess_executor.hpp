#pragma once

#include <expected>
#include <memory>
#include <string>
#include <vector>

#include "errors/error_codes.hpp"
#include "subprocess/subprocess_types.hpp"

namespace rosweb::subprocess {

class SubprocessExecutor {
public:
    /// Run a command, capture all output, wait for completion.
    /// @param args  Command and arguments (e.g. {"ls", "-la"})
    /// @param timeout_ms  Maximum wait time in milliseconds (0 = no timeout)
    /// @param cwd  Working directory for the child (empty = inherit)
    auto execute(const std::vector<std::string>& args,
                 int timeout_ms = 10000,
                 const std::string& cwd = "")
        -> std::expected<CommandResult, errors::ErrorCode>;

    /// Start a long-running command with line-by-line output callbacks.
    auto start_streaming(const std::vector<std::string>& args,
                          StreamCallbacks callbacks,
                          const std::string& cwd = "")
        -> std::expected<std::unique_ptr<StreamingHandle>, errors::ErrorCode>;

    /// Stop a streaming subprocess: SIGTERM, grace period, SIGKILL, join threads.
    static auto stop_streaming(std::unique_ptr<StreamingHandle> handle) -> void;

    /// Check if a command exists on PATH.
    static auto command_exists(const std::string& cmd) -> bool;

private:
    static auto build_argv(const std::vector<std::string>& args) -> std::vector<char*>;
    static void reader_loop(StreamingHandle* handle, int fd,
                             std::function<void(std::string_view)> callback);
};

}  // namespace rosweb::subprocess
