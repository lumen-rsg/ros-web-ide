# ROS Web IDE — Backend Progress

## Phase 1: Project Scaffolding + Filesystem Layer

- [x] Step 1.1 — Directory structure & git submodules
- [x] Step 1.2 — Makefile
- [x] Step 1.3 — Error & model infrastructure
- [x] Step 1.4 — IFileSystem abstract interface
- [x] Step 1.5 — PathValidator, BinaryDetector, FileSearch
- [x] Step 1.6 — LocalFileSystem implementation
- [x] Step 1.7 — FsController, Server, main.cpp
- [x] Step 1.8 — Unit tests (22 test cases, 100 assertions — all passing)
- [x] Step 1.9 — Build, test, verify (all 15 REST endpoints working)

## Phase 2: Workspace Management + System Info

- [x] Step 2.1 — WS_INVALID_PATH error code
- [x] Step 2.2 — Workspace models (RosPackage, WorkspaceInfo)
- [x] Step 2.3 — System models (CpuInfo, MemoryInfo, DiskInfo, SystemInfo, RosEnvInfo)
- [x] Step 2.4 — ISystemInfo interface
- [x] Step 2.5 — LocalSystemInfo (macOS + Linux platform-specific)
- [x] Step 2.6 — PackageDiscovery (package.xml parsing, executable discovery)
- [x] Step 2.7 — SystemController (GET /system/info, GET /system/ros-env)
- [x] Step 2.8 — WorkspaceController (GET /workspace, POST /workspace/open)
- [x] Step 2.9 — Wire controllers in Server
- [x] Step 2.10 — Tests (42 test cases, 180 assertions — all passing)
- [x] Step 2.11 — Build, test, verify (all 19 REST endpoints working)

## Phase 3: WebSocket Infrastructure + Terminal PTY

- [x] Step 3.1 — Terminal/WebSocket error codes (TERMINAL_LIMIT_REACHED, TERMINAL_NOT_FOUND, INVALID_PAYLOAD) + TerminalException
- [x] Step 3.2 — Terminal models (8 payload structs, to_json/from_json)
- [x] Step 3.3 — WsMessage envelope (channel/type/payload/seq, serialization)
- [x] Step 3.4 — IPtyManager interface + LocalPtyManager (POSIX PTY, fork/exec, reader threads, select+pipe shutdown)
- [x] Step 3.5 — IWsChannel interface + TerminalChannel (create/input/resize/close dispatch, on_output/on_exit callbacks)
- [x] Step 3.6 — WsRouter (channel registry, message dispatch, unknown-channel ignore)
- [x] Step 3.7 — WsController (CROW_WEBSOCKET_ROUTE /ws) + Server wiring
- [x] Step 3.8 — Tests (83 test cases, 332 assertions — all passing)
- [x] Step 3.9 — Build, test, verify (all 20 REST endpoints + WebSocket endpoint working)

## Phase 4: Build & Launch

- [x] Step 4.1 — Error codes (LAUNCH_IN_PROGRESS, LAUNCH_FAILED)
- [x] Step 4.2 — Build models (structs + JSON serialization: BuildRequest/Response, LaunchRequest/Response, WS payloads)
- [x] Step 4.3 — IBuildListener interface (observer for build/launch events)
- [x] Step 4.4 — IBuildManager interface (start_build, get_build_status, start_launch, stop_launch, discover_launch_files)
- [x] Step 4.5 — WsRouter shared_ptr change (enable shared ownership for BuildChannel)
- [x] Step 4.6 — LocalBuildManager (pipe+fork+execvp subprocess management, reader threads, listener notification)
- [x] Step 4.7 — BuildController (POST /build, GET /build/status, POST /launch, POST /launch/stop, GET /launch-files)
- [x] Step 4.8 — BuildChannel (IWsChannel + IBuildListener, subscriber tracking, event fan-out)
- [x] Step 4.9 — Server wiring (BuildManager, BuildChannel, BuildController integrated)
- [x] Step 4.10 — Tests (126 test cases, 451 assertions — all passing)
- [x] Step 4.11 — Build, test, verify (all 25 REST endpoints + 2 WebSocket channels working)

## Phase 5: Error Codes + Subprocess Utility

- [x] Step 5.1 — Missing error codes (SUBSCRIPTION_NOT_FOUND, BAG_WRITE_ERROR, BAG_NOT_RECORDING, ACTION_NOT_FOUND)
- [x] Step 5.2 — SubprocessExecutor utility (one-shot `execute()` + streaming `start_streaming()`/`stop_streaming()`)
- [x] Step 5.3 — Tests

## Phase 6: ROS2 REST Endpoints

- [x] Step 6.1 — ROS2 models (RosNode, RosTopic, RosService, RosAction, RosParameter, RosInterface, RosInterfaceField)
- [x] Step 6.2 — IRosManager interface + LocalRosManager (CLI subprocess via SubprocessExecutor)
- [x] Step 6.3 — RosController (8 REST endpoints under /api/v1/ros/*)
- [x] Step 6.4 — Server wiring
- [x] Step 6.5 — Tests

## Phase 7: File Watch WebSocket Channel

- [x] Step 7.1 — FileWatch models (FileChangeKind, payloads, serialization)
- [x] Step 7.2 — IFileWatchManager interface + LocalFileWatchManager (FSEvents on macOS, inotify on Linux)
- [x] Step 7.3 — FileWatchChannel (IWsChannel + IFileWatchListener, subscribe/broadcast)
- [x] Step 7.4 — Server wiring + Makefile (CoreServices framework on macOS)
- [x] Step 7.5 — Tests

## Phase 8: ROS2 WebSocket Channel

- [x] Step 8.1 — ROS2 stream models (request/response payloads, to_json/from_json)
- [x] Step 8.2 — IRosStreamListener interface (topic_message, service_result, action_feedback, action_result, node_event, bag_status callbacks)
- [x] Step 8.3 — IRosStreamManager interface (subscribe_topic, unsubscribe_topic, publish_topic, call_service, call_action, cancel_action, start_bag, stop_bag, start/stop_node_monitor)
- [x] Step 8.4 — LocalRosStreamManager (SubprocessExecutor streaming, topic echo/publish, service/action calls, bag recording, node polling, YAML parsing)
- [x] Step 8.5 — RosChannel (IWsChannel + IRosStreamListener, 8 message types, subscriber maps, broadcast)
- [x] Step 8.6 — Server wiring
- [x] Step 8.7 — Tests (245 test cases, 783 assertions — all passing)

## Phase 9: TF WebSocket Channel

- [x] Step 9.1 — TF models (TfFrame, TfTransform, TfTreePayload, TfUpdatePayload)
- [x] Step 9.2 — ITfListener + ITfManager interfaces
- [x] Step 9.3 — LocalTfManager (ros2 topic echo /tf streaming, /tf_static tree building, frame filtering, throttle)
- [x] Step 9.4 — TfChannel (IWsChannel + ITfListener, subscribe-tf, get-tf-tree)
- [x] Step 9.5 — Server wiring
- [x] Step 9.6 — Tests
