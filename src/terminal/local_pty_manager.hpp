#pragma once

#include <memory>
#include <mutex>
#include <unordered_map>

#include "terminal/i_pty_manager.hpp"
#include "terminal/pty_session.hpp"

namespace rosweb::terminal {

class LocalPtyManager : public IPtyManager {
public:
    LocalPtyManager() = default;
    ~LocalPtyManager() override;

    LocalPtyManager(const LocalPtyManager&) = delete;
    auto operator=(const LocalPtyManager&) -> LocalPtyManager& = delete;

    auto create(
        const PtyCreateParams& params,
        std::function<void(const std::string&, std::string)> on_output,
        std::function<void(const std::string&, int)> on_exit
    ) -> std::expected<int, errors::ErrorCode> override;

    auto write(const std::string& terminal_id, std::string_view data)
        -> std::expected<void, errors::ErrorCode> override;

    auto resize(const std::string& terminal_id, int cols, int rows)
        -> std::expected<void, errors::ErrorCode> override;

    auto kill(const std::string& terminal_id)
        -> std::expected<void, errors::ErrorCode> override;

    auto close_all() -> void override;

    auto active_count() const -> size_t override;

private:
    void reader_loop(PtySession* session);
    void shutdown_session(std::unique_ptr<PtySession> session);
    auto find_session(const std::string& terminal_id) -> PtySession*;

    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::unique_ptr<PtySession>> sessions_;
};

}  // namespace rosweb::terminal
