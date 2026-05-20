#pragma once

#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "errors/error_codes.hpp"
#include "models/filewatch_models.hpp"

namespace rosweb::filewatch {

class IFileWatchListener {
public:
    virtual ~IFileWatchListener() = default;
    virtual void on_file_changed(const std::string& watch_id,
                                  const std::string& path,
                                  models::FileChangeKind kind,
                                  const std::optional<std::string>& old_path) = 0;
};

class IFileWatchManager {
public:
    virtual ~IFileWatchManager() = default;

    virtual auto watch(const std::string& watch_id,
                        const std::string& path,
                        bool recursive)
        -> std::expected<void, errors::ErrorCode> = 0;

    virtual auto unwatch(const std::string& watch_id)
        -> std::expected<void, errors::ErrorCode> = 0;

    virtual auto add_listener(std::shared_ptr<IFileWatchListener> listener) -> void = 0;
    virtual auto remove_listener(std::shared_ptr<IFileWatchListener> listener) -> void = 0;
    virtual auto shutdown() -> void = 0;
};

}  // namespace rosweb::filewatch
