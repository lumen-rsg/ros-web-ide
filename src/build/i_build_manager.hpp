#pragma once

#include <expected>
#include <memory>
#include <string>
#include <vector>

#include "errors/error_codes.hpp"
#include "models/build_models.hpp"

namespace rosweb::build {

class IBuildListener;

class IBuildManager {
public:
    virtual ~IBuildManager() = default;

    virtual auto start_build(const models::BuildRequest& request)
        -> std::expected<models::BuildResponse, errors::ErrorCode> = 0;

    virtual auto get_build_status(const std::string& build_id) const
        -> std::expected<models::BuildStatusResponse, errors::ErrorCode> = 0;

    virtual auto start_launch(const models::LaunchRequest& request)
        -> std::expected<models::LaunchResponse, errors::ErrorCode> = 0;

    virtual auto stop_launch(const std::string& launch_id)
        -> std::expected<models::LaunchStopResponse, errors::ErrorCode> = 0;

    virtual auto discover_launch_files() const
        -> std::expected<models::LaunchFilesResponse, errors::ErrorCode> = 0;

    virtual auto add_listener(std::shared_ptr<IBuildListener> listener) -> void = 0;
    virtual auto remove_listener(std::shared_ptr<IBuildListener> listener) -> void = 0;

    virtual auto shutdown() -> void = 0;
};

}  // namespace rosweb::build
