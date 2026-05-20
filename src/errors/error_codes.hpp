#pragma once

#include <string>
#include <string_view>

namespace rosweb::errors {

enum class ErrorCode : int {
    // Filesystem errors
    FS_PATH_NOT_FOUND,
    FS_PATH_EXISTS,
    FS_PERMISSION_DENIED,
    FS_IS_DIRECTORY,
    FS_IS_FILE,
    FS_NOT_EMPTY,
    FS_WRITE_FAILED,

    // Workspace errors
    WS_NOT_OPEN,
    WS_INVALID_PATH,

    // ROS errors
    ROS_NODE_NOT_FOUND,
    ROS_SERVICE_UNAVAILABLE,
    ROS_SERVICE_TIMEOUT,
    ROS_TOPIC_TYPE_MISMATCH,
    ROS_INVALID_MESSAGE,
    ROS_PARAM_SET_FAILED,

    // Terminal/WebSocket errors
    TERMINAL_LIMIT_REACHED,
    TERMINAL_NOT_FOUND,
    SUBSCRIPTION_NOT_FOUND,
    BAG_WRITE_ERROR,
    BAG_NOT_RECORDING,
    ACTION_NOT_FOUND,
    INVALID_PAYLOAD,

    // Build errors
    BUILD_IN_PROGRESS,
    BUILD_NOT_FOUND,
    LAUNCH_NOT_FOUND,
    LAUNCH_IN_PROGRESS,
    LAUNCH_FAILED,

    // General
    INTERNAL_ERROR,
};

auto error_code_to_string(ErrorCode code) -> std::string_view;

auto error_code_to_http_status(ErrorCode code) -> int;

}  // namespace rosweb::errors
