#pragma once

#include <crow.h>
#include <crow/middlewares/cors.h>
#include <memory>
#include <string>

#include "fs/i_filesystem.hpp"
#include "fs/path_validator.hpp"
#include "api/fs_controller.hpp"
#include "api/workspace_controller.hpp"
#include "api/system_controller.hpp"
#include "api/build_controller.hpp"
#include "api/ros_controller.hpp"
#include "api/ws_controller.hpp"
#include "system/i_system_info.hpp"
#include "terminal/i_pty_manager.hpp"
#include "build/i_build_manager.hpp"
#include "build/local_build_manager.hpp"
#include "ws/ws_router.hpp"
#include "ws/terminal_channel.hpp"
#include "ws/build_channel.hpp"
#include "ros/i_ros_manager.hpp"
#include "ros/local_ros_manager.hpp"
#include "ros/i_ros_stream_manager.hpp"
#include "ros/local_ros_stream_manager.hpp"
#include "filewatch/i_filewatch_manager.hpp"
#include "filewatch/local_filewatch_manager.hpp"
#include "ws/filewatch_channel.hpp"
#include "ws/ros_channel.hpp"
#include "tf/i_tf_manager.hpp"
#include "tf/local_tf_manager.hpp"
#include "ws/tf_channel.hpp"

namespace rosweb::server {

class Server {
public:
    explicit Server(const std::string& workspace_root = ".");
    ~Server();

    Server(const Server&) = delete;
    auto operator=(const Server&) -> Server& = delete;

    void start(uint16_t port = 8080);

private:
    void restart_ros_subsystems();

    std::string workspace_root_;
    crow::App<crow::CORSHandler> app_;
    std::shared_ptr<fs::PathValidator> validator_;
    std::shared_ptr<fs::IFileSystem> filesystem_;
    std::shared_ptr<system::ISystemInfo> system_info_;
    std::shared_ptr<terminal::IPtyManager> pty_manager_;
    std::shared_ptr<build::IBuildManager> build_manager_;
    std::shared_ptr<ros::IRosManager> ros_manager_;
    std::shared_ptr<ros::IRosStreamManager> ros_stream_manager_;
    std::shared_ptr<filewatch::IFileWatchManager> filewatch_manager_;
    std::shared_ptr<ws::BuildChannel> build_channel_;
    std::shared_ptr<ws::FileWatchChannel> filewatch_channel_;
    std::shared_ptr<ws::RosChannel> ros_channel_;
    std::shared_ptr<tf::ITfManager> tf_manager_;
    std::shared_ptr<ws::TfChannel> tf_channel_;
    std::unique_ptr<api::FsController> fs_controller_;
    std::unique_ptr<api::WorkspaceController> workspace_controller_;
    std::unique_ptr<api::SystemController> system_controller_;
    std::unique_ptr<api::BuildController> build_controller_;
    std::unique_ptr<api::RosController> ros_controller_;
    std::unique_ptr<api::WsController> ws_controller_;
};

}  // namespace rosweb::server
