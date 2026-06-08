#pragma once

#include <atomic>
#include <functional>
#include <string>
#include <string_view>
#include <thread>

namespace rosweb::subprocess {

struct CommandResult {
    int exit_code = -1;
    std::string stdout_output;
    std::string stderr_output;
};

struct StreamCallbacks {
    std::function<void(std::string_view)> on_stdout;
    std::function<void(std::string_view)> on_stderr;
    std::function<void(int)> on_exit;
};

struct StreamingHandle {
    pid_t pid = -1;
    int stdout_pipe[2] = {-1, -1};
    int stderr_pipe[2] = {-1, -1};
    int shutdown_pipe[2] = {-1, -1};
    std::thread stdout_reader;
    std::thread stderr_reader;
    std::atomic<bool> running{false};
    std::atomic<bool> cleaned{false};  // guards against double-join / double-close
};

}  // namespace rosweb::subprocess
