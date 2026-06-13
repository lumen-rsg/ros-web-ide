#include "server/server.hpp"

#include "fs/local_filesystem.hpp"
#include "system/local_system_info.hpp"
#include "terminal/local_pty_manager.hpp"
#include "build/local_build_manager.hpp"
#include "ros/local_ros_manager.hpp"
#include "ros/local_ros_stream_manager.hpp"
#include "tf/local_tf_manager.hpp"
#include "filewatch/local_filewatch_manager.hpp"
#include "errors/error_response.hpp"

namespace rosweb::server {

Server::Server(const std::string& workspace_root)
    : workspace_root_(workspace_root),
      validator_(std::make_shared<fs::PathValidator>(workspace_root)),
      filesystem_(std::make_shared<fs::LocalFileSystem>(validator_)),
      system_info_(std::make_shared<system::LocalSystemInfo>()),
      pty_manager_(std::make_shared<terminal::LocalPtyManager>(workspace_root)),
      build_manager_(std::make_shared<build::LocalBuildManager>(workspace_root)),
      ros_manager_(std::make_shared<ros::LocalRosManager>(workspace_root)),
      ros_stream_manager_(std::make_shared<ros::LocalRosStreamManager>(workspace_root)),
      filewatch_manager_(std::make_shared<filewatch::LocalFileWatchManager>()),
      fs_controller_(std::make_unique<api::FsController>(filesystem_, validator_)),
      workspace_controller_(std::make_unique<api::WorkspaceController>(
          validator_,
          filesystem_,
          std::vector<std::shared_ptr<workspace::IWorkspaceAware>>{
              std::static_pointer_cast<workspace::IWorkspaceAware>(
                  std::static_pointer_cast<build::LocalBuildManager>(build_manager_)),
              std::static_pointer_cast<workspace::IWorkspaceAware>(
                  std::static_pointer_cast<terminal::LocalPtyManager>(pty_manager_)),
              std::static_pointer_cast<workspace::IWorkspaceAware>(
                  std::static_pointer_cast<ros::LocalRosManager>(ros_manager_)),
              std::static_pointer_cast<workspace::IWorkspaceAware>(
                  std::static_pointer_cast<ros::LocalRosStreamManager>(ros_stream_manager_)),
          })),
      system_controller_(std::make_unique<api::SystemController>(system_info_)),
      build_controller_(std::make_unique<api::BuildController>(build_manager_)),
      ros_controller_(std::make_unique<api::RosController>(ros_manager_)) {
    // Set restart callback for domain ID changes
    system_controller_->set_restart_callback([this]() { restart_ros_subsystems(); });
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

    tf_manager_ = std::make_shared<tf::LocalTfManager>(workspace_root);
    workspace_controller_->add_workspace_aware(
        std::static_pointer_cast<workspace::IWorkspaceAware>(
            std::static_pointer_cast<tf::LocalTfManager>(tf_manager_)));
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

void Server::restart_ros_subsystems() {
    // Save old manager pointers for replacement
    auto old_build = build_manager_;
    auto old_ros_stream = ros_stream_manager_;
    auto old_tf = tf_manager_;

    // 1. Shutdown old managers
    old_build->shutdown();
    old_ros_stream->shutdown();
    old_tf->shutdown();

    // 2. Remove channels as listeners from old managers
    old_build->remove_listener(build_channel_);
    old_ros_stream->remove_listener(ros_channel_);
    old_tf->remove_listener(tf_channel_);

    // 3. Create new managers with updated environment
    // Use validator's workspace_root() which stays in sync with workspace switches,
    // unlike Server::workspace_root_ which is only set at construction time.
    auto current_ws = validator_->workspace_root();
    build_manager_ = std::make_shared<build::LocalBuildManager>(current_ws);
    ros_stream_manager_ = std::make_shared<ros::LocalRosStreamManager>(current_ws);
    tf_manager_ = std::make_shared<tf::LocalTfManager>(current_ws);

    // 4. Update build controller's manager reference
    build_controller_->set_manager(build_manager_);

    // 5. Update channel manager references and re-add listeners
    build_channel_->set_manager(build_manager_);
    build_manager_->add_listener(build_channel_);

    ros_channel_->set_manager(ros_stream_manager_);
    ros_stream_manager_->add_listener(ros_channel_);

    tf_channel_->set_manager(tf_manager_);
    tf_manager_->add_listener(tf_channel_);

    // 6. Update workspace-aware components
    workspace_controller_->replace_workspace_aware(
        std::static_pointer_cast<workspace::IWorkspaceAware>(
            std::static_pointer_cast<build::LocalBuildManager>(old_build)),
        std::static_pointer_cast<workspace::IWorkspaceAware>(
            std::static_pointer_cast<build::LocalBuildManager>(build_manager_)));

    workspace_controller_->replace_workspace_aware(
        std::static_pointer_cast<workspace::IWorkspaceAware>(
            std::static_pointer_cast<ros::LocalRosStreamManager>(old_ros_stream)),
        std::static_pointer_cast<workspace::IWorkspaceAware>(
            std::static_pointer_cast<ros::LocalRosStreamManager>(ros_stream_manager_)));

    workspace_controller_->replace_workspace_aware(
        std::static_pointer_cast<workspace::IWorkspaceAware>(
            std::static_pointer_cast<tf::LocalTfManager>(old_tf)),
        std::static_pointer_cast<workspace::IWorkspaceAware>(
            std::static_pointer_cast<tf::LocalTfManager>(tf_manager_)));

    std::cerr << "[INFO] ROS subsystems restarted with updated ROS_DOMAIN_ID" << std::endl;
}

}  // namespace rosweb::server
