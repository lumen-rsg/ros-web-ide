# ROS Web IDE — Backend / Frontend API Contract

> **Version**: 1.0.0
> **Last updated**: 2026-05-20
> **Status**: Draft

This document defines the complete communication contract between the **C++ backend** and the **TypeScript frontend** of the ROS Web IDE. Both teams must treat this as the single source of truth. Any ambiguity here is a bug in the contract — open a discussion before guessing.

---

## Table of Contents

1. [Protocol Overview](#1-protocol-overview)
2. [REST API](#2-rest-api)
3. [WebSocket Protocol](#3-websocket-protocol)
4. [Shared Type Definitions](#4-shared-type-definitions)
5. [Error Handling](#5-error-handling)
6. [Extensibility & Versioning](#6-extensibility--versioning)
7. [Appendix: Changelog](#7-appendix-changelog)

---

## 1. Protocol Overview

### 1.1 Transport Summary

| Concern | Transport | When Used |
|---|---|---|
| File CRUD, search, workspace config | REST (HTTP/1.1) | Request-response operations |
| ROS2 entity listing, parameter access | REST | Request-response operations |
| System info, build triggers | REST | Request-response operations |
| Terminal I/O | WebSocket | Bidirectional streaming |
| File change notifications | WebSocket | Server-push events |
| ROS2 topic echo, service/action calls | WebSocket | Bidirectional streaming |
| Build/launch output | WebSocket | Server-push streams |

### 1.2 Base URLs

```
REST:       http://{host}:{port}/api/v1
WebSocket:  ws://{host}:{port}/ws
```

- Default port: `8080`
- The frontend MUST support configurable `host` and `port`.

### 1.3 REST Envelope

Every REST response uses this JSON envelope:

```jsonc
// Success
{
  "ok": true,
  "data": { /* domain-specific payload */ }
}

// Error
{
  "ok": false,
  "error": {
    "code": "FS_PATH_NOT_FOUND",
    "message": "Human-readable description",
    "details": {}  // optional, domain-specific
  }
}
```

### 1.4 REST Request Headers

| Header | Value | Required |
|---|---|---|
| `Content-Type` | `application/json` | Yes (for request bodies) |
| `Accept` | `application/json` | Yes |

### 1.5 WebSocket Connection

- Single persistent connection per browser tab.
- The server MUST respond to `ping` frames with `pong`.
- If the connection drops, the frontend MUST attempt reconnection with exponential backoff (1s, 2s, 4s, 8s, max 30s).
- On reconnect, the frontend MUST re-subscribe to all previously active channels (the server does not remember subscriptions across connections).

### 1.6 WebSocket Message Envelope

Every WebSocket frame (in both directions) uses this JSON envelope:

```jsonc
{
  "channel": "terminal",        // string — which subsystem this message belongs to
  "type": "output",             // string — specific message type within the channel
  "payload": { /* ... */ },     // object — type-specific data
  "seq": 42                     // optional integer — sequence number for request-response correlation
}
```

**Rules**:
- `channel` and `type` are always lowercase strings with hyphens (e.g. `"subscribe-topic"`).
- `seq` is client-assigned for requests. The server echoes the same `seq` in its response. For unsolicited server pushes, `seq` is omitted.
- Unknown `channel` or `type` values MUST be silently ignored (forward compatibility).

---

## 2. REST API

All endpoints are relative to the REST base URL: `/api/v1`

---

### 2.1 File System — `/fs`

#### `GET /fs/tree`

List directory contents as a tree.

**Query Parameters**:

| Param | Type | Default | Description |
|---|---|---|---|
| `path` | string | workspace root | Absolute or workspace-relative path |
| `depth` | integer | 1 | Depth of recursion (1 = immediate children only, 0 = infinite) |

**Response `data`**:

```jsonc
{
  "path": "/home/user/ros_ws/src",
  "name": "src",
  "type": "directory",
  "children": [
    {
      "path": "/home/user/ros_ws/src/my_package",
      "name": "my_package",
      "type": "directory",
      "children": [
        { "path": "/home/user/ros_ws/src/my_package/package.xml", "name": "package.xml", "type": "file", "size": 1234 }
      ]
    }
  ]
}
```

**Type reference**: `FileEntry` (see [section 4](#fileentry))

---

#### `GET /fs/file`

Read a single file's content.

**Query Parameters**:

| Param | Type | Required | Description |
|---|---|---|---|
| `path` | string | Yes | File path |

**Response `data`**:

```jsonc
{
  "path": "/home/user/ros_ws/src/my_pkg/node.cpp",
  "content": "// file content here\n",
  "encoding": "utf-8",
  "size": 234,
  "modified": "2026-05-20T10:30:00Z"
}
```

> **Note**: Binary files return `"encoding": "base64"` with base64-encoded `content`.

---

#### `PUT /fs/file`

Create or overwrite a file.

**Request Body**:

```jsonc
{
  "path": "/home/user/ros_ws/src/my_pkg/new_file.cpp",
  "content": "#include <iostream>\n",
  "createParents": true   // optional, default false — create intermediate directories
}
```

**Response `data`**:

```jsonc
{
  "path": "/home/user/ros_ws/src/my_pkg/new_file.cpp",
  "size": 20,
  "created": true  // true if the file was newly created, false if overwritten
}
```

---

#### `DELETE /fs/file`

Delete a file or directory.

**Query Parameters**:

| Param | Type | Required | Description |
|---|---|---|---|
| `path` | string | Yes | Path to delete |
| `recursive` | boolean | No | Required if path is a non-empty directory (default false) |

**Response `data`**:

```jsonc
{
  "path": "/home/user/ros_ws/src/my_pkg/old_file.cpp",
  "deleted": true
}
```

---

#### `POST /fs/rename`

Rename or move a file or directory.

**Request Body**:

```jsonc
{
  "oldPath": "/home/user/ros_ws/src/my_pkg/a.cpp",
  "newPath": "/home/user/ros_ws/src/my_pkg/b.cpp"
}
```

**Response `data`**:

```jsonc
{
  "oldPath": "/home/user/ros_ws/src/my_pkg/a.cpp",
  "newPath": "/home/user/ros_ws/src/my_pkg/b.cpp"
}
```

---

#### `POST /fs/mkdir`

Create a directory.

**Request Body**:

```jsonc
{
  "path": "/home/user/ros_ws/src/my_pkg/include/my_pkg",
  "createParents": true   // optional, default false
}
```

**Response `data`**:

```jsonc
{
  "path": "/home/user/ros_ws/src/my_pkg/include/my_pkg",
  "created": true
}
```

---

#### `GET /fs/search`

Search for files by name pattern or content.

**Query Parameters**:

| Param | Type | Default | Description |
|---|---|---|---|
| `query` | string | (required) | Search query (filename glob or text pattern) |
| `path` | string | workspace root | Root directory to search in |
| `type` | string | `"all"` | `"filename"`, `"content"`, or `"all"` |
| `maxResults` | integer | 100 | Maximum number of results |

**Response `data`**:

```jsonc
{
  "results": [
    {
      "path": "/home/user/ros_ws/src/my_pkg/node.cpp",
      "type": "file",
      "matches": [
        { "line": 42, "column": 10, "text": "rclcpp::Publisher", "length": 16 }
      ]
    }
  ],
  "total": 1,
  "truncated": false
}
```

---

### 2.2 Workspace — `/workspace`

#### `GET /workspace`

Get current workspace information.

**Response `data`**:

```jsonc
{
  "rootPath": "/home/user/ros_ws",
  "name": "ros_ws",
  "rosDistro": "humble",         // detected from environment
  "packages": [                   // discovered ROS2 packages in workspace
    {
      "name": "my_pkg",
      "path": "/home/user/ros_ws/src/my_pkg",
      "type": "ament_cmake",      // "ament_cmake" | "ament_python"
      "executables": ["talker", "listener"]
    }
  ]
}
```

---

#### `POST /workspace/open`

Change the workspace root.

**Request Body**:

```jsonc
{
  "path": "/home/user/other_ws"
}
```

**Response**: Same as `GET /workspace`.

---

### 2.3 ROS2 — `/ros`

#### `GET /ros/nodes`

List all active ROS2 nodes.

**Response `data`**:

```jsonc
{
  "nodes": [
    {
      "name": "/talker",
      "namespace": "/",
      "pid": 12345
    }
  ]
}
```

---

#### `GET /ros/topics`

List all active topics.

**Query Parameters**:

| Param | Type | Default | Description |
|---|---|---|---|
| `includeHidden` | boolean | false | Include topics starting with `_` or `/_` |

**Response `data`**:

```jsonc
{
  "topics": [
    {
      "name": "/chatter",
      "type": "std_msgs/msg/String",
      "publisherCount": 1,
      "subscriberCount": 2
    }
  ]
}
```

---

#### `GET /ros/services`

List all available services.

**Response `data`**:

```jsonc
{
  "services": [
    {
      "name": "/set_parameters",
      "type": "rcl_interfaces/srv/SetParameters",
      "node": "/talker"
    }
  ]
}
```

---

#### `GET /ros/actions`

List all available actions.

**Response `data`**:

```jsonc
{
  "actions": [
    {
      "name": "/navigate",
      "type": "nav2_msgs/action/NavigateToPose",
      "node": "/planner"
    }
  ]
}
```

---

#### `GET /ros/params`

Get parameters for a specific node.

**Query Parameters**:

| Param | Type | Required | Description |
|---|---|---|---|
| `node` | string | Yes | Fully qualified node name |

**Response `data`**:

```jsonc
{
  "node": "/talker",
  "parameters": [
    {
      "name": "publish_rate",
      "type": "integer",     // "integer" | "double" | "string" | "boolean" | "array" | "object"
      "value": 10,
      "description": ""       // optional, if available from descriptor
    }
  ]
}
```

---

#### `PUT /ros/params`

Set a parameter on a node.

**Request Body**:

```jsonc
{
  "node": "/talker",
  "name": "publish_rate",
  "value": 20
}
```

**Response `data`**:

```jsonc
{
  "node": "/talker",
  "name": "publish_rate",
  "value": 20,
  "success": true
}
```

---

#### `GET /ros/interfaces`

List all available ROS2 interface types.

**Query Parameters**:

| Param | Type | Default | Description |
|---|---|---|---|
| `kind` | string | `"all"` | `"msg"`, `"srv"`, `"action"`, or `"all"` |
| `filter` | string | `""` | Substring filter on package or type name |

**Response `data`**:

```jsonc
{
  "interfaces": [
    { "kind": "msg",  "package": "std_msgs",  "name": "String" },
    { "kind": "srv",  "package": "example_interfaces", "name": "AddTwoInts" },
    { "kind": "action", "package": "nav2_msgs", "name": "NavigateToPose" }
  ]
}
```

---

#### `GET /ros/interface-detail`

Get field definitions for a specific interface type.

**Query Parameters**:

| Param | Type | Required | Description |
|---|---|---|---|
| `type` | string | Yes | Fully qualified type, e.g. `std_msgs/msg/String` |

**Response `data`**:

```jsonc
{
  "type": "geometry_msgs/msg/Twist",
  "fields": [
    {
      "name": "linear",
      "type": "geometry_msgs/msg/Vector3",
      "isArray": false,
      "defaultValue": null,
      "children": [
        { "name": "x", "type": "float64", "isArray": false, "defaultValue": 0.0 },
        { "name": "y", "type": "float64", "isArray": false, "defaultValue": 0.0 },
        { "name": "z", "type": "float64", "isArray": false, "defaultValue": 0.0 }
      ]
    }
  ]
}
```

---

### 2.4 Build & Launch — `/build`

#### `POST /build`

Trigger a colcon build.

**Request Body**:

```jsonc
{
  "targets": ["my_pkg"],        // optional — empty or omitted = build all
  "args": ["--cmake-args", "-DCMAKE_BUILD_TYPE=Release"],  // extra CLI args
  "clean": false                 // run --clean-first
}
```

**Response `data`**:

```jsonc
{
  "buildId": "b_1716201000_abc",
  "status": "running"
}
```

Build output streams over WebSocket (see [section 3.5](#35-build-channel)).

---

#### `GET /build/status`

Get the status of a build.

**Query Parameters**:

| Param | Type | Required | Description |
|---|---|---|---|
| `buildId` | string | No | Specific build ID. Omit for latest. |

**Response `data`**:

```jsonc
{
  "buildId": "b_1716201000_abc",
  "status": "completed",     // "running" | "completed" | "failed" | "cancelled"
  "targets": {
    "my_pkg": {
      "status": "completed",
      "returnCode": 0
    }
  }
}
```

---

#### `POST /launch`

Launch a ROS2 launch file.

**Request Body**:

```jsonc
{
  "package": "my_pkg",
  "file": "demo.launch.py",
  "arguments": {                // optional — launch arguments as key-value pairs
    "use_sim": "true"
  }
}
```

**Response `data`**:

```jsonc
{
  "launchId": "l_1716201000_xyz",
  "status": "running",
  "pid": 12345
}
```

Launch output streams over WebSocket (see [section 3.5](#35-build-channel)).

---

#### `POST /launch/stop`

Stop a running launch.

**Request Body**:

```jsonc
{
  "launchId": "l_1716201000_xyz"
}
```

**Response `data`**:

```jsonc
{
  "launchId": "l_1716201000_xyz",
  "status": "stopped"
}
```

---

#### `GET /launch-files`

Discover available launch files in the workspace.

**Response `data`**:

```jsonc
{
  "files": [
    {
      "path": "/home/user/ros_ws/src/my_pkg/launch/demo.launch.py",
      "package": "my_pkg",
      "arguments": [
        { "name": "use_sim", "type": "string", "default": "false", "description": "Use simulation" }
      ]
    }
  ]
}
```

---

### 2.5 System — `/system`

#### `GET /system/info`

Get device/system information.

**Response `data`**:

```jsonc
{
  "hostname": "jetson-nano",
  "platform": "aarch64",
  "os": "Ubuntu 22.04",
  "cpu": {
    "model": "ARM Cortex-A57",
    "cores": 4,
    "usagePercent": 23.5
  },
  "memory": {
    "totalBytes": 4294967296,
    "usedBytes": 2147483648,
    "availableBytes": 2147483648
  },
  "disk": {
    "totalBytes": 322122547200,
    "usedBytes": 107374182400,
    "availableBytes": 214748364800,
    "mountPoint": "/"
  }
}
```

---

#### `GET /system/ros-env`

Get the ROS2 environment configuration.

**Response `data`**:

```jsonc
{
  "rosDistro": "humble",
  "rosVersion": "2",
  "domainId": 0,
  "variables": {
    "ROS_DOMAIN_ID": "0",
    "AMENT_PREFIX_PATH": "/opt/ros/humble:/home/user/ros_ws/install",
    "LD_LIBRARY_PATH": "/opt/ros/humble/lib:...",
    "PATH": "/opt/ros/humble/bin:..."
  }
}
```

---

## 3. WebSocket Protocol

Connection endpoint: `ws://{host}:{port}/ws`

### 3.1 Terminal Channel — `channel: "terminal"`

The terminal channel manages pseudo-terminal (PTY) sessions. Each terminal gets a unique `terminalId`.

#### Client → Server Messages

##### `create`

Create a new terminal session.

```jsonc
{
  "channel": "terminal",
  "type": "create",
  "seq": 1,
  "payload": {
    "terminalId": "term_1",        // client-assigned ID
    "shell": "/bin/bash",          // optional, default: user's default shell
    "cwd": "/home/user/ros_ws",    // optional, default: workspace root
    "env": {},                     // optional, extra environment variables
    "cols": 80,                    // initial terminal width
    "rows": 24                     // initial terminal height
  }
}
```

##### `input`

Send user input to the terminal.

```jsonc
{
  "channel": "terminal",
  "type": "input",
  "payload": {
    "terminalId": "term_1",
    "data": "ls -la\r"
  }
}
```

##### `resize`

Notify the backend of a terminal resize.

```jsonc
{
  "channel": "terminal",
  "type": "resize",
  "payload": {
    "terminalId": "term_1",
    "cols": 120,
    "rows": 40
  }
}
```

##### `close`

Close a terminal session.

```jsonc
{
  "channel": "terminal",
  "type": "close",
  "payload": {
    "terminalId": "term_1"
  }
}
```

#### Server → Client Messages

##### `created`

Confirms terminal creation (echoes `seq` from `create` request).

```jsonc
{
  "channel": "terminal",
  "type": "created",
  "seq": 1,
  "payload": {
    "terminalId": "term_1",
    "pid": 12345
  }
}
```

##### `output`

Terminal output data. Uses standard PTY escape sequences.

```jsonc
{
  "channel": "terminal",
  "type": "output",
  "payload": {
    "terminalId": "term_1",
    "data": "\u001b[?2004hroot@jetson:~$ "
  }
}
```

> **Note**: `data` contains raw terminal output including ANSI escape sequences. The frontend is responsible for rendering them (e.g., via xterm.js).

##### `exited`

Terminal process has exited.

```jsonc
{
  "channel": "terminal",
  "type": "exited",
  "payload": {
    "terminalId": "term_1",
    "exitCode": 0
  }
}
```

---

### 3.2 File Watch Channel — `channel: "file-watch"`

Monitors file system changes in real-time.

#### Client → Server Messages

##### `watch`

Subscribe to file system events under a path.

```jsonc
{
  "channel": "file-watch",
  "type": "watch",
  "seq": 10,
  "payload": {
    "watchId": "w_1",              // client-assigned subscription ID
    "path": "/home/user/ros_ws/src/my_pkg",
    "recursive": true              // watch subdirectories
  }
}
```

##### `unwatch`

Stop watching a path.

```jsonc
{
  "channel": "file-watch",
  "type": "unwatch",
  "payload": {
    "watchId": "w_1"
  }
}
```

#### Server → Client Messages

##### `watching`

Confirms the watch subscription (echoes `seq`).

```jsonc
{
  "channel": "file-watch",
  "type": "watching",
  "seq": 10,
  "payload": {
    "watchId": "w_1",
    "path": "/home/user/ros_ws/src/my_pkg"
  }
}
```

##### `changed`

A file's content was modified.

```jsonc
{
  "channel": "file-watch",
  "type": "changed",
  "payload": {
    "watchId": "w_1",
    "path": "/home/user/ros_ws/src/my_pkg/node.cpp",
    "kind": "modified"           // "modified" | "created" | "deleted" | "renamed"
  }
}
```

For `"renamed"`:

```jsonc
{
  "channel": "file-watch",
  "type": "changed",
  "payload": {
    "watchId": "w_1",
    "path": "/home/user/ros_ws/src/my_pkg/new_name.cpp",
    "oldPath": "/home/user/ros_ws/src/my_pkg/old_name.cpp",
    "kind": "renamed"
  }
}
```

---

### 3.3 ROS2 Channel — `channel: "ros"`

All ROS2 real-time interactions go through this channel.

#### Client → Server Messages

##### `subscribe-topic`

Subscribe to messages on a ROS2 topic.

```jsonc
{
  "channel": "ros",
  "type": "subscribe-topic",
  "seq": 20,
  "payload": {
    "subscriptionId": "sub_1",     // client-assigned
    "topic": "/chatter",
    "type": "std_msgs/msg/String", // optional — server can auto-detect if omitted
    "throttleRate": 0,             // optional — max messages per second (0 = unlimited)
    "queueLength": 1               // optional — how many messages to buffer
  }
}
```

##### `unsubscribe-topic`

```jsonc
{
  "channel": "ros",
  "type": "unsubscribe-topic",
  "payload": {
    "subscriptionId": "sub_1"
  }
}
```

##### `publish-topic`

Publish a message to a topic (one-shot or continuous).

```jsonc
{
  "channel": "ros",
  "type": "publish-topic",
  "payload": {
    "topic": "/cmd_vel",
    "type": "geometry_msgs/msg/Twist",
    "message": {
      "linear": { "x": 1.0, "y": 0.0, "z": 0.0 },
      "angular": { "x": 0.0, "y": 0.0, "z": 0.5 }
    }
  }
}
```

##### `call-service`

Call a ROS2 service. Response comes with the matching `seq`.

```jsonc
{
  "channel": "ros",
  "type": "call-service",
  "seq": 21,
  "payload": {
    "callId": "call_1",            // client-assigned
    "service": "/set_parameters",
    "type": "rcl_interfaces/srv/SetParameters",
    "request": {
      "parameters": [
        { "name": "publish_rate", "value": { "type": 2, "integer_value": 20 } }
      ]
    },
    "timeout": 5000                // ms, optional, default 5000
  }
}
```

##### `call-action`

Send a goal to a ROS2 action server. Multiple responses (feedback + result) share the same `callId`.

```jsonc
{
  "channel": "ros",
  "type": "call-action",
  "seq": 22,
  "payload": {
    "callId": "act_1",
    "action": "/navigate",
    "type": "nav2_msgs/action/NavigateToPose",
    "goal": {
      "pose": { /* ... */ }
    },
    "timeout": 30000               // ms, optional
  }
}
```

##### `cancel-action`

Cancel an in-progress action goal.

```jsonc
{
  "channel": "ros",
  "type": "cancel-action",
  "payload": {
    "callId": "act_1"
  }
}
```

##### `start-bag`

Start recording topics to a bag file.

```jsonc
{
  "channel": "ros",
  "type": "start-bag",
  "seq": 23,
  "payload": {
    "bagId": "bag_1",
    "topics": ["/chatter", "/tf"], // empty = record all
    "path": "/home/user/ros_ws/bags/recording",
    "format": "sqlite3"            // "sqlite3" | "mcap"
  }
}
```

##### `stop-bag`

Stop recording.

```jsonc
{
  "channel": "ros",
  "type": "stop-bag",
  "payload": {
    "bagId": "bag_1"
  }
}
```

#### Server → Client Messages

##### `subscribed`

Confirms topic subscription (echoes `seq`).

```jsonc
{
  "channel": "ros",
  "type": "subscribed",
  "seq": 20,
  "payload": {
    "subscriptionId": "sub_1",
    "topic": "/chatter"
  }
}
```

##### `topic-message`

A message received on a subscribed topic.

```jsonc
{
  "channel": "ros",
  "type": "topic-message",
  "payload": {
    "subscriptionId": "sub_1",
    "topic": "/chatter",
    "timestamp": 1716201000000000000,  // nanoseconds since epoch
    "message": {
      "data": "Hello, world!"
    }
  }
}
```

##### `service-result`

Response to `call-service` (echoes `seq`).

```jsonc
{
  "channel": "ros",
  "type": "service-result",
  "seq": 21,
  "payload": {
    "callId": "call_1",
    "success": true,
    "result": {
      "results": [{ "successful": true, "reason": "" }]
    }
  }
}
```

On error:

```jsonc
{
  "channel": "ros",
  "type": "service-result",
  "seq": 21,
  "payload": {
    "callId": "call_1",
    "success": false,
    "error": {
      "code": "ROS_SERVICE_TIMEOUT",
      "message": "Service call timed out after 5000ms"
    }
  }
}
```

##### `action-feedback`

Feedback from an active action goal.

```jsonc
{
  "channel": "ros",
  "type": "action-feedback",
  "payload": {
    "callId": "act_1",
    "feedback": {
      "distance_remaining": 12.5
    }
  }
}
```

##### `action-result`

Final result of an action goal.

```jsonc
{
  "channel": "ros",
  "type": "action-result",
  "seq": 22,
  "payload": {
    "callId": "act_1",
    "status": "succeeded",       // "succeeded" | "aborted" | "cancelled"
    "result": { /* ... */ }
  }
}
```

##### `node-event`

Real-time notification of ROS2 node lifecycle changes.

```jsonc
{
  "channel": "ros",
  "type": "node-event",
  "payload": {
    "event": "started",          // "started" | "stopped"
    "node": {
      "name": "/new_node",
      "namespace": "/",
      "pid": 12345
    }
  }
}
```

##### `bag-status`

Status update for a recording session.

```jsonc
{
  "channel": "ros",
  "type": "bag-status",
  "payload": {
    "bagId": "bag_1",
    "status": "recording",       // "recording" | "stopped" | "error"
    "duration": 120,             // seconds
    "messageCount": 5000,
    "sizeBytes": 10485760
  }
}
```

---

### 3.4 TF Channel — `channel: "tf"`

Provides ROS2 transform tree data.

#### Client → Server Messages

##### `subscribe-tf`

Subscribe to transform updates.

```jsonc
{
  "channel": "tf",
  "type": "subscribe-tf",
  "seq": 30,
  "payload": {
    "subscriptionId": "tf_1",
    "frames": [],                  // empty = all frames, or specific frame names
    "throttleRate": 10             // max updates per second
  }
}
```

##### `get-tf-tree`

Request the complete TF tree (one-shot).

```jsonc
{
  "channel": "tf",
  "type": "get-tf-tree",
  "seq": 31,
  "payload": {}
}
```

#### Server → Client Messages

##### `tf-tree`

Response with the full TF tree.

```jsonc
{
  "channel": "tf",
  "type": "tf-tree",
  "seq": 31,
  "payload": {
    "frames": [
      {
        "name": "base_link",
        "parent": null,
        "children": ["lidar", "camera"]
      },
      {
        "name": "lidar",
        "parent": "base_link",
        "children": []
      }
    ]
  }
}
```

##### `tf-update`

Incremental transform update.

```jsonc
{
  "channel": "tf",
  "type": "tf-update",
  "payload": {
    "subscriptionId": "tf_1",
    "transforms": [
      {
        "parent": "base_link",
        "child": "lidar",
        "translation": { "x": 0.5, "y": 0.0, "z": 0.3 },
        "rotation": { "x": 0.0, "y": 0.0, "z": 0.0, "w": 1.0 },
        "timestamp": 1716201000000000000
      }
    ]
  }
}
```

---

### 3.5 Build Channel — `channel: "build"`

Streams build and launch output.

#### Server → Client Messages

##### `build-output`

A line or chunk of build output.

```jsonc
{
  "channel": "build",
  "type": "build-output",
  "payload": {
    "buildId": "b_1716201000_abc",
    "target": "my_pkg",            // optional
    "stream": "stdout",            // "stdout" | "stderr"
    "data": "[ 50%] Building CXX object CMakeFiles/talker.dir/src/talker.cpp.o\n"
  }
}
```

##### `build-status`

Build status change.

```jsonc
{
  "channel": "build",
  "type": "build-status",
  "payload": {
    "buildId": "b_1716201000_abc",
    "status": "completed",         // "running" | "completed" | "failed" | "cancelled"
    "targets": {
      "my_pkg": { "status": "completed", "returnCode": 0 },
      "other_pkg": { "status": "failed", "returnCode": 1 }
    }
  }
}
```

##### `launch-output`

Output from a running launch.

```jsonc
{
  "channel": "build",
  "type": "launch-output",
  "payload": {
    "launchId": "l_1716201000_xyz",
    "node": "/talker",             // which node produced this output (optional)
    "stream": "stdout",
    "data": "[INFO] [talker]: Publishing: Hello, world!\n"
  }
}
```

##### `launch-status`

Launch status change.

```jsonc
{
  "channel": "build",
  "type": "launch-status",
  "payload": {
    "launchId": "l_1716201000_xyz",
    "status": "stopped",           // "running" | "stopped"
    "exitCode": 0
  }
}
```

---

## 4. Shared Type Definitions

These types are shared between backend and frontend. The TypeScript interface names are given; the C++ team should create equivalent structs. Field names and nesting MUST match exactly to ensure JSON compatibility.

### `FileEntry`

```typescript
interface FileEntry {
  path: string;
  name: string;
  type: "file" | "directory" | "symlink";
  size?: number;               // bytes, files only
  modified?: string;           // ISO 8601 datetime
  children?: FileEntry[];      // directories only
}
```

### `RosNode`

```typescript
interface RosNode {
  name: string;                // fully qualified, e.g. "/talker"
  namespace: string;           // e.g. "/"
  pid: number;
}
```

### `RosTopic`

```typescript
interface RosTopic {
  name: string;
  type: string;                // e.g. "std_msgs/msg/String"
  publisherCount: number;
  subscriberCount: number;
}
```

### `RosService`

```typescript
interface RosService {
  name: string;
  type: string;
  node: string;                // node offering the service
}
```

### `RosAction`

```typescript
interface RosAction {
  name: string;
  type: string;
  node: string;
}
```

### `RosParameter`

```typescript
interface RosParameter {
  name: string;
  type: "integer" | "double" | "string" | "boolean" | "byte_array" | "array" | "object";
  value: unknown;
  description?: string;
}
```

### `RosInterface`

```typescript
interface RosInterface {
  kind: "msg" | "srv" | "action";
  package: string;
  name: string;
}
```

### `RosInterfaceField`

```typescript
interface RosInterfaceField {
  name: string;
  type: string;                // primitive type name or fully qualified interface type
  isArray: boolean;
  defaultValue?: unknown;
  children?: RosInterfaceField[];  // nested fields for complex types
}
```

### `RosPackage`

```typescript
interface RosPackage {
  name: string;
  path: string;
  type: "ament_cmake" | "ament_python";
  executables: string[];
}
```

### `LaunchFile`

```typescript
interface LaunchFile {
  path: string;
  package: string;
  arguments: LaunchArgument[];
}

interface LaunchArgument {
  name: string;
  type: string;
  default?: string;
  description?: string;
}
```

### `SystemInfo`

```typescript
interface SystemInfo {
  hostname: string;
  platform: string;
  os: string;
  cpu: CpuInfo;
  memory: MemoryInfo;
  disk: DiskInfo;
}

interface CpuInfo {
  model: string;
  cores: number;
  usagePercent: number;
}

interface MemoryInfo {
  totalBytes: number;
  usedBytes: number;
  availableBytes: number;
}

interface DiskInfo {
  totalBytes: number;
  usedBytes: number;
  availableBytes: number;
  mountPoint: string;
}
```

### `WsEnvelope`

```typescript
interface WsEnvelope<T = unknown> {
  channel: string;
  type: string;
  payload: T;
  seq?: number;
}
```

### `RestResponse`

```typescript
interface RestResponse<T = unknown> {
  ok: boolean;
  data?: T;
  error?: RestError;
}

interface RestError {
  code: string;
  message: string;
  details?: Record<string, unknown>;
}
```

---

## 5. Error Handling

### 5.1 REST Error Codes

The `error.code` field uses namespaced string constants. The backend MUST use these exact codes; the frontend switches on them.

| Code | HTTP Status | Description |
|---|---|---|
| `FS_PATH_NOT_FOUND` | 404 | File or directory does not exist |
| `FS_PATH_EXISTS` | 409 | Path already exists (on create) |
| `FS_PERMISSION_DENIED` | 403 | Insufficient filesystem permissions |
| `FS_IS_DIRECTORY` | 400 | Expected a file, got a directory |
| `FS_IS_FILE` | 400 | Expected a directory, got a file |
| `FS_NOT_EMPTY` | 400 | Directory not empty, recursive not set |
| `FS_WRITE_FAILED` | 500 | Could not write file to disk |
| `WS_NOT_OPEN` | 400 | Workspace path not set or invalid |
| `ROS_NODE_NOT_FOUND` | 404 | Requested ROS node does not exist |
| `ROS_SERVICE_UNAVAILABLE` | 503 | ROS service exists but no server is available |
| `ROS_SERVICE_TIMEOUT` | 504 | Service/action call timed out |
| `ROS_TOPIC_TYPE_MISMATCH` | 400 | Provided type does not match actual topic type |
| `ROS_INVALID_MESSAGE` | 400 | Malformed message payload |
| `ROS_PARAM_SET_FAILED` | 500 | Failed to set parameter on node |
| `BUILD_IN_PROGRESS` | 409 | A build is already running |
| `BUILD_NOT_FOUND` | 404 | Build ID does not exist |
| `LAUNCH_NOT_FOUND` | 404 | Launch ID does not exist |
| `INTERNAL_ERROR` | 500 | Unexpected server error |

### 5.2 WebSocket Errors

WebSocket errors are sent as messages on the relevant channel:

```jsonc
{
  "channel": "terminal",
  "type": "error",
  "seq": 1,                      // echoes the seq of the failed request
  "payload": {
    "code": "TERMINAL_LIMIT_REACHED",
    "message": "Maximum number of terminals (10) reached"
  }
}
```

Additional WebSocket error codes:

| Code | Channel | Description |
|---|---|---|
| `TERMINAL_LIMIT_REACHED` | terminal | Too many concurrent terminals |
| `TERMINAL_NOT_FOUND` | terminal | Unknown `terminalId` |
| `SUBSCRIPTION_NOT_FOUND` | ros | Unknown `subscriptionId` |
| `BAG_WRITE_ERROR` | ros | Cannot write to bag path |
| `BAG_NOT_RECORDING` | ros | `stop-bag` for a `bagId` that is not recording |
| `ACTION_NOT_FOUND` | ros | Unknown action name |
| `INVALID_PAYLOAD` | any | Payload does not match expected schema |

---

## 6. Extensibility & Versioning

### 6.1 Adding New Features

The contract is designed for additive changes:

1. **New REST endpoints**: Add new paths under `/api/v1/`. Existing endpoints are unchanged.
2. **New WebSocket channels**: Add a new `channel` value. Clients that don't know the channel ignore it.
3. **New message types**: Add a new `type` to an existing channel. Unknown types are ignored.
4. **New payload fields**: Add fields to existing payloads. Implementations MUST ignore unknown fields (both C++ and TS).

### 6.2 Breaking Changes

Changes are **breaking** if they:
- Remove or rename an existing endpoint, channel, type, or payload field.
- Change the type of an existing field.
- Change the semantics of an existing value.

Breaking changes require:
1. A new API version prefix: `/api/v2/`.
2. A new WebSocket endpoint: `ws://{host}:{port}/ws/v2`.
3. Both versions run concurrently during the migration period.

### 6.3 Non-Breaking Changes (allowed freely)

- Adding new endpoints.
- Adding new WebSocket channels or message types.
- Adding new optional fields to payloads.
- Adding new values to existing enums (e.g., new error codes).
- Adding new query parameters with defaults.

### 6.4 Versioning Strategy

| When | Action |
|---|---|
| Additive change | Update this document, bump minor version (1.1, 1.2, ...) |
| Breaking change | Create `/api/v2/`, update this document, bump major version (2.0) |
| Both teams must review | Any change to this document requires sign-off from both teams |

---

## 7. Appendix: Changelog

| Version | Date | Changes |
|---|---|---|
| 1.0.0 | 2026-05-20 | Initial contract |
