#pragma once

#include <atomic>
#include <functional>
#include <string>
#include <thread>

namespace rosweb::terminal {

struct PtySession {
    std::string terminal_id;
    int master_fd = -1;
    int child_pid = -1;
    int shutdown_pipe[2] = {-1, -1};  // [0]=read, [1]=write
    std::thread reader_thread;
    std::atomic<bool> running{false};
    std::function<void(const std::string&, std::string)> on_output;
    std::function<void(const std::string&, int)> on_exit;
};

}  // namespace rosweb::terminal
