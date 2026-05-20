#include "ros/local_ros_manager.hpp"

#include <algorithm>
#include <sstream>

namespace rosweb::ros {

// --- Helpers ---

auto LocalRosManager::run_command(const std::vector<std::string>& args)
    -> std::expected<std::string, errors::ErrorCode> {
    auto result = executor_.execute(args, 10000);
    if (!result.has_value()) {
        return std::unexpected(result.error());
    }
    if (result->exit_code != 0) {
        // Non-zero exit from ros2 CLI typically means ROS2 not available or entity not found
        return std::unexpected(errors::ErrorCode::ROS_SERVICE_UNAVAILABLE);
    }
    return result->stdout_output;
}

auto LocalRosManager::split_lines(const std::string& output) -> std::vector<std::string> {
    std::vector<std::string> lines;
    std::istringstream stream(output);
    std::string line;
    while (std::getline(stream, line)) {
        // Trim whitespace
        auto start = line.find_first_not_of(" \t\r\n");
        auto end = line.find_last_not_of(" \t\r\n");
        if (start != std::string::npos) {
            lines.push_back(line.substr(start, end - start + 1));
        }
    }
    return lines;
}

auto LocalRosManager::parse_namespace(const std::string& node_name) -> std::string {
    auto pos = node_name.rfind('/');
    if (pos == std::string::npos) return "/";
    auto ns = node_name.substr(0, pos);
    if (ns.empty()) return "/";
    return ns;
}

auto LocalRosManager::json_value_to_param_type(const nlohmann::json& val) -> std::string {
    if (val.is_string()) return "string";
    if (val.is_number_integer()) return "integer";
    if (val.is_number_float()) return "double";
    if (val.is_boolean()) return "boolean";
    if (val.is_array()) return "array";
    if (val.is_object()) return "object";
    return "string";
}

// --- list_nodes ---

auto LocalRosManager::list_nodes()
    -> std::expected<models::RosNodesResponse, errors::ErrorCode> {
    auto output = run_command({"ros2", "node", "list"});
    if (!output.has_value()) {
        return std::unexpected(output.error());
    }

    models::RosNodesResponse response;
    auto names = split_lines(output.value());

    for (const auto& name : names) {
        models::RosNode node;
        node.name = name;
        node.node_namespace = parse_namespace(name);

        // Try to get PID from node info
        auto info = run_command({"ros2", "node", "info", name});
        if (info.has_value()) {
            // Parse "Pid: NNNN" or "pid: NNNN" from node info output
            auto lines = split_lines(info.value());
            for (const auto& line : lines) {
                auto pid_pos = line.find("Pid:");
                if (pid_pos == std::string::npos) {
                    pid_pos = line.find("pid:");
                }
                if (pid_pos != std::string::npos) {
                    auto num_start = line.find_first_of("0123456789", pid_pos);
                    if (num_start != std::string::npos) {
                        try {
                            node.pid = std::stoi(line.substr(num_start));
                        } catch (...) {}
                    }
                    break;
                }
            }
        }

        response.nodes.push_back(std::move(node));
    }

    return response;
}

// --- list_topics ---

auto LocalRosManager::list_topics(bool include_hidden)
    -> std::expected<models::RosTopicsResponse, errors::ErrorCode> {
    auto output = run_command({"ros2", "topic", "list"});
    if (!output.has_value()) {
        return std::unexpected(output.error());
    }

    models::RosTopicsResponse response;
    auto names = split_lines(output.value());

    for (const auto& name : names) {
        // Filter hidden topics (start with /_ or contain /_ after a /)
        if (!include_hidden) {
            // A topic is "hidden" if any segment starts with _
            bool hidden = false;
            if (name.size() >= 2 && name[0] == '/' && name[1] == '_') hidden = true;
            if (!hidden) {
                auto pos = name.find("/_");
                if (pos != std::string::npos) hidden = true;
            }
            if (hidden) continue;
        }

        models::RosTopic topic;
        topic.name = name;

        // Get topic info for type and counts
        auto info = run_command({"ros2", "topic", "info", name});
        if (info.has_value()) {
            auto lines = split_lines(info.value());
            for (const auto& line : lines) {
                if (line.find("Type:") != std::string::npos) {
                    auto colon = line.find(':');
                    if (colon != std::string::npos) {
                        auto val = line.substr(colon + 1);
                        auto s = val.find_first_not_of(" \t");
                        if (s != std::string::npos) {
                            auto e = val.find_last_not_of(" \t\r\n");
                            topic.type = val.substr(s, e - s + 1);
                        }
                    }
                }
                if (line.find("Publisher count:") != std::string::npos ||
                    line.find("Subscription count:") != std::string::npos) {
                    auto colon = line.find(':');
                    if (colon != std::string::npos) {
                        try {
                            int count = std::stoi(line.substr(colon + 1));
                            if (line.find("Publisher") != std::string::npos) {
                                topic.publisher_count = count;
                            } else {
                                topic.subscriber_count = count;
                            }
                        } catch (...) {}
                    }
                }
            }
        }

        response.topics.push_back(std::move(topic));
    }

    return response;
}

// --- list_services ---

auto LocalRosManager::list_services()
    -> std::expected<models::RosServicesResponse, errors::ErrorCode> {
    auto output = run_command({"ros2", "service", "list"});
    if (!output.has_value()) {
        return std::unexpected(output.error());
    }

    models::RosServicesResponse response;
    auto names = split_lines(output.value());

    for (const auto& name : names) {
        models::RosService svc;
        svc.name = name;

        // Get service type
        auto type_out = run_command({"ros2", "service", "type", name});
        if (type_out.has_value()) {
            auto type_lines = split_lines(type_out.value());
            if (!type_lines.empty()) {
                svc.type = type_lines[0];
            }
        }

        // Derive node name from service name (best effort: take the namespace)
        // This is a heuristic — the actual node offering the service isn't directly
        // available from `ros2 service list`. The node info command has this data.
        // For now, leave node empty or derive from the namespace.
        auto last_slash = name.rfind('/');
        if (last_slash != std::string::npos && last_slash > 0) {
            svc.node = name.substr(0, last_slash);
        }

        response.services.push_back(std::move(svc));
    }

    return response;
}

// --- list_actions ---

auto LocalRosManager::list_actions()
    -> std::expected<models::RosActionsResponse, errors::ErrorCode> {
    auto output = run_command({"ros2", "action", "list"});
    if (!output.has_value()) {
        return std::unexpected(output.error());
    }

    models::RosActionsResponse response;
    auto names = split_lines(output.value());

    for (const auto& name : names) {
        models::RosAction action;
        action.name = name;

        auto info = run_command({"ros2", "action", "info", name});
        if (info.has_value()) {
            auto lines = split_lines(info.value());
            for (const auto& line : lines) {
                if (line.find("Action type:") != std::string::npos ||
                    line.find("Type:") != std::string::npos) {
                    auto colon = line.find(':');
                    if (colon != std::string::npos) {
                        auto val = line.substr(colon + 1);
                        auto s = val.find_first_not_of(" \t");
                        if (s != std::string::npos) {
                            auto e = val.find_last_not_of(" \t\r\n");
                            action.type = val.substr(s, e - s + 1);
                        }
                    }
                }
                // "Action servers: N" or server node names follow
                if (line.find("Action servers:") != std::string::npos) {
                    // The next lines after this contain node names
                    // For simplicity, we don't parse them here
                }
            }
        }

        // Derive node from action name
        auto last_slash = name.rfind('/');
        if (last_slash != std::string::npos && last_slash > 0) {
            action.node = name.substr(0, last_slash);
        }

        response.actions.push_back(std::move(action));
    }

    return response;
}

// --- list_params ---

auto LocalRosManager::list_params(const std::string& node)
    -> std::expected<models::RosParamsResponse, errors::ErrorCode> {
    if (node.empty()) {
        return std::unexpected(errors::ErrorCode::ROS_NODE_NOT_FOUND);
    }

    auto output = run_command({"ros2", "param", "list", node});
    if (!output.has_value()) {
        return std::unexpected(output.error());
    }

    models::RosParamsResponse response;
    response.node = node;
    auto param_names = split_lines(output.value());

    for (const auto& pname : param_names) {
        // Skip section headers like "Parameters:" or empty lines
        if (pname.find(':') != std::string::npos && pname.find(' ') == std::string::npos) {
            continue;  // Likely a section header
        }

        models::RosParameter param;
        param.name = pname;

        auto val_out = run_command({"ros2", "param", "get", node, pname});
        if (val_out.has_value()) {
            // Output format: "String value: hello" or "Integer value: 42"
            auto val_lines = split_lines(val_out.value());
            for (const auto& vl : val_lines) {
                auto colon = vl.find("value:");
                if (colon != std::string::npos) {
                    auto val_str = vl.substr(colon + 6);
                    // Trim
                    auto s = val_str.find_first_not_of(" \t");
                    if (s != std::string::npos) {
                        auto e = val_str.find_last_not_of(" \t\r\n");
                        val_str = val_str.substr(s, e - s + 1);
                    }

                    // Determine type from the prefix before "value:"
                    if (vl.find("String") != std::string::npos) {
                        param.type = "string";
                        param.value = val_str;
                    } else if (vl.find("Integer") != std::string::npos) {
                        param.type = "integer";
                        try { param.value = std::stoi(val_str); } catch (...) { param.value = val_str; }
                    } else if (vl.find("Double") != std::string::npos || vl.find("Float") != std::string::npos) {
                        param.type = "double";
                        try { param.value = std::stod(val_str); } catch (...) { param.value = val_str; }
                    } else if (vl.find("Boolean") != std::string::npos) {
                        param.type = "boolean";
                        param.value = (val_str == "true");
                    } else if (vl.find("Array") != std::string::npos) {
                        param.type = "array";
                    } else {
                        param.type = json_value_to_param_type(nlohmann::json(val_str));
                        param.value = val_str;
                    }
                    break;
                }
            }
        }

        response.parameters.push_back(std::move(param));
    }

    return response;
}

// --- set_param ---

auto LocalRosManager::set_param(const models::RosParamSetRequest& req)
    -> std::expected<models::RosParamSetResponse, errors::ErrorCode> {
    // Format the value as a string for the CLI
    std::string value_str;
    if (req.value.is_string()) {
        value_str = req.value.get<std::string>();
    } else if (req.value.is_number_integer()) {
        value_str = std::to_string(req.value.get<int>());
    } else if (req.value.is_number_float()) {
        value_str = std::to_string(req.value.get<double>());
    } else if (req.value.is_boolean()) {
        value_str = req.value.get<bool>() ? "true" : "false";
    } else {
        value_str = req.value.dump();
    }

    auto result = executor_.execute(
        {"ros2", "param", "set", req.node, req.name, value_str}, 10000);
    if (!result.has_value()) {
        return std::unexpected(result.error());
    }

    models::RosParamSetResponse response;
    response.node = req.node;
    response.name = req.name;
    response.value = req.value;
    response.success = (result->exit_code == 0);

    if (!response.success) {
        return std::unexpected(errors::ErrorCode::ROS_PARAM_SET_FAILED);
    }

    return response;
}

// --- list_interfaces ---

auto LocalRosManager::list_interfaces(const std::string& kind, const std::string& filter)
    -> std::expected<models::RosInterfacesResponse, errors::ErrorCode> {
    // ros2 interface list outputs lines like "std_msgs/msg/String"
    std::vector<std::string> cmd = {"ros2", "interface", "list"};
    auto output = run_command(cmd);
    if (!output.has_value()) {
        return std::unexpected(output.error());
    }

    models::RosInterfacesResponse response;
    auto lines = split_lines(output.value());

    for (const auto& line : lines) {
        // Parse "package/kind/Name"
        auto first_slash = line.find('/');
        if (first_slash == std::string::npos) continue;
        auto second_slash = line.find('/', first_slash + 1);
        if (second_slash == std::string::npos) continue;

        auto pkg = line.substr(0, first_slash);
        auto k = line.substr(first_slash + 1, second_slash - first_slash - 1);
        auto name = line.substr(second_slash + 1);

        // Filter by kind
        if (kind != "all" && !kind.empty() && k != kind) continue;

        // Filter by substring
        if (!filter.empty() && line.find(filter) == std::string::npos) continue;

        response.interfaces.push_back(models::RosInterface{
            .kind = k,
            .package = pkg,
            .name = name,
        });
    }

    return response;
}

// --- get_interface_detail ---

auto LocalRosManager::get_interface_detail(const std::string& type_name)
    -> std::expected<models::RosInterfaceDetailResponse, errors::ErrorCode> {
    auto output = run_command({"ros2", "interface", "show", type_name});
    if (!output.has_value()) {
        return std::unexpected(output.error());
    }

    models::RosInterfaceDetailResponse response;
    response.type = type_name;

    auto lines = split_lines(output.value());

    // Parse field definitions. Simple approach: each non-empty line is a field.
    // Format: "type name" or "type name default_value" or "type[] name"
    // Indented lines are children of the previous unindented line.
    // For a first pass, treat all lines as top-level fields.

    for (const auto& line : lines) {
        // Skip comment lines
        if (line.starts_with('#')) continue;
        if (line.empty()) continue;

        // Check indentation (2 spaces = nested child — we'll flatten for now)
        auto content = line;
        auto indent = line.find_first_not_of(' ');
        bool is_nested = (indent > 0 && indent != std::string::npos);
        if (is_nested) {
            content = line.substr(indent);
        }

        models::RosInterfaceField field;

        // Check for array type: "type[]"
        auto bracket = content.find('[');
        if (bracket != std::string::npos && bracket < content.find(' ')) {
            field.type = content.substr(0, bracket);
            field.is_array = true;
            content = content.substr(content.find(' ') + 1);
        } else {
            // "type name" or "type name default"
            auto space1 = content.find(' ');
            if (space1 == std::string::npos) continue;
            field.type = content.substr(0, space1);
            content = content.substr(space1 + 1);
        }

        // Remaining: "name" or "name default_value"
        auto space2 = content.find(' ');
        if (space2 != std::string::npos) {
            field.name = content.substr(0, space2);
            auto defval = content.substr(space2 + 1);
            // Try to parse as JSON value
            try {
                field.default_value = nlohmann::json::parse(defval);
            } catch (...) {
                field.default_value = defval;
            }
        } else {
            field.name = content;
        }

        response.fields.push_back(std::move(field));
    }

    return response;
}

}  // namespace rosweb::ros
