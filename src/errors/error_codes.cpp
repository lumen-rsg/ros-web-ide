#include "errors/error_codes.hpp"

#include <unordered_map>

namespace rosweb::errors {

auto error_code_to_string(ErrorCode code) -> std::string_view {
    static const std::unordered_map<ErrorCode, std::string_view> names = {
        {ErrorCode::FS_PATH_NOT_FOUND,       "FS_PATH_NOT_FOUND"},
        {ErrorCode::FS_PATH_EXISTS,          "FS_PATH_EXISTS"},
        {ErrorCode::FS_PERMISSION_DENIED,    "FS_PERMISSION_DENIED"},
        {ErrorCode::FS_IS_DIRECTORY,         "FS_IS_DIRECTORY"},
        {ErrorCode::FS_IS_FILE,              "FS_IS_FILE"},
        {ErrorCode::FS_NOT_EMPTY,            "FS_NOT_EMPTY"},
        {ErrorCode::FS_WRITE_FAILED,         "FS_WRITE_FAILED"},
        {ErrorCode::WS_NOT_OPEN,             "WS_NOT_OPEN"},
        {ErrorCode::WS_INVALID_PATH,         "WS_INVALID_PATH"},
        {ErrorCode::ROS_NODE_NOT_FOUND,      "ROS_NODE_NOT_FOUND"},
        {ErrorCode::ROS_SERVICE_UNAVAILABLE, "ROS_SERVICE_UNAVAILABLE"},
        {ErrorCode::ROS_SERVICE_TIMEOUT,     "ROS_SERVICE_TIMEOUT"},
        {ErrorCode::ROS_TOPIC_TYPE_MISMATCH, "ROS_TOPIC_TYPE_MISMATCH"},
        {ErrorCode::ROS_INVALID_MESSAGE,     "ROS_INVALID_MESSAGE"},
        {ErrorCode::ROS_PARAM_SET_FAILED,    "ROS_PARAM_SET_FAILED"},
        {ErrorCode::TERMINAL_LIMIT_REACHED,  "TERMINAL_LIMIT_REACHED"},
        {ErrorCode::TERMINAL_NOT_FOUND,      "TERMINAL_NOT_FOUND"},
        {ErrorCode::SUBSCRIPTION_NOT_FOUND,  "SUBSCRIPTION_NOT_FOUND"},
        {ErrorCode::BAG_WRITE_ERROR,         "BAG_WRITE_ERROR"},
        {ErrorCode::BAG_NOT_RECORDING,       "BAG_NOT_RECORDING"},
        {ErrorCode::ACTION_NOT_FOUND,        "ACTION_NOT_FOUND"},
        {ErrorCode::INVALID_PAYLOAD,         "INVALID_PAYLOAD"},
        {ErrorCode::BUILD_IN_PROGRESS,       "BUILD_IN_PROGRESS"},
        {ErrorCode::BUILD_NOT_FOUND,         "BUILD_NOT_FOUND"},
        {ErrorCode::LAUNCH_NOT_FOUND,        "LAUNCH_NOT_FOUND"},
        {ErrorCode::LAUNCH_IN_PROGRESS,      "LAUNCH_IN_PROGRESS"},
        {ErrorCode::LAUNCH_FAILED,           "LAUNCH_FAILED"},
        {ErrorCode::INTERNAL_ERROR,          "INTERNAL_ERROR"},
    };
    return names.at(code);
}

auto error_code_to_http_status(ErrorCode code) -> int {
    static const std::unordered_map<ErrorCode, int> statuses = {
        {ErrorCode::FS_PATH_NOT_FOUND,       404},
        {ErrorCode::FS_PATH_EXISTS,          409},
        {ErrorCode::FS_PERMISSION_DENIED,    403},
        {ErrorCode::FS_IS_DIRECTORY,         400},
        {ErrorCode::FS_IS_FILE,              400},
        {ErrorCode::FS_NOT_EMPTY,            400},
        {ErrorCode::FS_WRITE_FAILED,         500},
        {ErrorCode::WS_NOT_OPEN,             400},
        {ErrorCode::WS_INVALID_PATH,         400},
        {ErrorCode::ROS_NODE_NOT_FOUND,      404},
        {ErrorCode::ROS_SERVICE_UNAVAILABLE, 503},
        {ErrorCode::ROS_SERVICE_TIMEOUT,     504},
        {ErrorCode::ROS_TOPIC_TYPE_MISMATCH, 400},
        {ErrorCode::ROS_INVALID_MESSAGE,     400},
        {ErrorCode::ROS_PARAM_SET_FAILED,    500},
        {ErrorCode::TERMINAL_LIMIT_REACHED,  400},
        {ErrorCode::TERMINAL_NOT_FOUND,      404},
        {ErrorCode::SUBSCRIPTION_NOT_FOUND,  404},
        {ErrorCode::BAG_WRITE_ERROR,         500},
        {ErrorCode::BAG_NOT_RECORDING,       400},
        {ErrorCode::ACTION_NOT_FOUND,        404},
        {ErrorCode::INVALID_PAYLOAD,         400},
        {ErrorCode::BUILD_IN_PROGRESS,       409},
        {ErrorCode::BUILD_NOT_FOUND,         404},
        {ErrorCode::LAUNCH_NOT_FOUND,        404},
        {ErrorCode::LAUNCH_IN_PROGRESS,      409},
        {ErrorCode::LAUNCH_FAILED,           500},
        {ErrorCode::INTERNAL_ERROR,          500},
    };
    return statuses.at(code);
}

}  // namespace rosweb::errors
