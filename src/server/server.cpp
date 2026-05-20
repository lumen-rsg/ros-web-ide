#include "server/server.hpp"

#include "fs/local_filesystem.hpp"
#include "system/local_system_info.hpp"
#include "terminal/local_pty_manager.hpp"
#include "build/local_build_manager.hpp"
#include "ros/local_ros_manager.hpp"
#include "filewatch/local_filewatch_manager.hpp"
#include "errors/error_response.hpp"

namespace rosweb::server {

Server::Server(const std::string& workspace_root)
    : validator_(std::make_shared<fs::PathValidator>(workspace_root)),
      filesystem_(std::make_shared<fs::LocalFileSystem>(validator_)),
      system_info_(std::make_shared<system::LocalSystemInfo>()),
      pty_manager_(std::make_shared<terminal::LocalPtyManager>()),
      build_manager_(std::make_shared<build::LocalBuildManager>(workspace_root)),
      ros_manager_(std::make_shared<ros::LocalRosManager>()),
      ros_stream_manager_(std::make_shared<ros::LocalRosStreamManager>()),
      filewatch_manager_(std::make_shared<filewatch::LocalFileWatchManager>()),
      fs_controller_(std::make_unique<api::FsController>(filesystem_, validator_)),
      workspace_controller_(std::make_unique<api::WorkspaceController>(validator_, filesystem_)),
      system_controller_(std::make_unique<api::SystemController>(system_info_)),
      build_controller_(std::make_unique<api::BuildController>(build_manager_)),
      ros_controller_(std::make_unique<api::RosController>(ros_manager_)) {
    auto ws_router = std::make_unique<ws::WsRouter>();
    ws_router->register_channel(
        std::make_shared<ws::TerminalChannel>(pty_manager_));

    build_channel_ = std::make_shared<ws::BuildChannel>(build_manager_);
    build_manager_->add_listener(build_channel_);
    ws_router->register_channel(build_channel_);

    filewatch_channel_ = std::make_shared<ws::FileWatchChannel>(filewatch_manager_);
    filewatch_manager_->add_listener(filewatch_channel_);
    ws_router->register_channel(filewatch_channel_);

    ros_channel_ = std::make_shared<ws::RosChannel>(ros_stream_manager_);
    ros_stream_manager_->add_listener(ros_channel_);
    ws_router->register_channel(ros_channel_);

    tf_manager_ = std::make_shared<tf::LocalTfManager>();
    tf_channel_ = std::make_shared<ws::TfChannel>(tf_manager_);
    tf_manager_->add_listener(tf_channel_);
    ws_router->register_channel(tf_channel_);

    ws_controller_ = std::make_unique<api::WsController>(std::move(ws_router));
}

Server::~Server() {
    tf_manager_->shutdown();
    ros_stream_manager_->shutdown();
    filewatch_manager_->shutdown();
    build_manager_->shutdown();
}

void Server::start(uint16_t port) {
    fs_controller_->register_routes(app_);
    workspace_controller_->register_routes(app_);
    system_controller_->register_routes(app_);
    build_controller_->register_routes(app_);
    ros_controller_->register_routes(app_);
    ws_controller_->register_routes(app_);

    CROW_ROUTE(app_, "/api/v1/health")([] {
        nlohmann::json data;
        data["status"] = "running";
        return errors::make_success_json(data);
    });

    app_.port(port).multithreaded().run();
}

}  // namespace rosweb::server
