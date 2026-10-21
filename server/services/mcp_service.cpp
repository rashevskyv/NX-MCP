#include "mcp_service.hpp"
#include "mcp_versions.hpp"
#include "tool_registry.hpp"
#include "console_lease.hpp"
#include <set>

namespace services {

// Tools that change what the console does. They need an "agent" argument and
// go through ConsoleLease; screenshots and reads stay open to everyone.
static bool ChangesConsole(const std::string& tool) {
  static const std::set<std::string> kTools = {
    "hid_press_buttons", "hid_sequence", "hid_touch", "hid_swipe",
    "pm_launch", "pm_kill", "power",
    "fs_write_file", "fs_delete", "fs_mkdir",
    "mount_save", "unmount_save", "ipc_call", "ipc_close",
  };
  return kTools.count(tool) != 0;
}

static nlohmann::json WithAgentArgument(nlohmann::json schema) {
  schema["properties"]["agent"] = {
    {"type", "string"},
    {"description", "Your session name. Shown to other agents while you drive the console."}
  };
  if (!schema.contains("required") || !schema["required"].is_array())
    schema["required"] = nlohmann::json::array();
  schema["required"].push_back("agent");
  return schema;
}

nlohmann::json McpService::Dispatch(const std::string& method,
                                    const nlohmann::json& params,
                                    const nlohmann::json& id) {
  if (method == "initialize") return HandleInitialize(params, id);
  if (method == "ping") return {{"jsonrpc", "2.0"}, {"id", id}, {"result", nlohmann::json::object()}};
  if (method == "server/discover") return HandleDiscover(id);
  if (method == "tools/list") return HandleToolsList(id);
  if (method == "tools/call") return HandleToolsCall(params, id);

  return {
    {"jsonrpc", "2.0"},
    {"id", id},
    {"error", {{"code", -32601}, {"message", "Method not found"}}}
  };
}

nlohmann::json McpService::HandleInitialize(const nlohmann::json& params,
                                             const nlohmann::json& id) {
  const std::string requested =
      params.value("protocolVersion", std::string(""));
  if (requested != mcp::kLegacyVersion) {
    return {
      {"jsonrpc", "2.0"},
      {"id", id},
      {"error", {
        {"code", -32022},
        {"message", "Unsupported protocol version"},
        {"data", {
          {"supported", nlohmann::json::array({mcp::kLegacyVersion})},
          {"requested", requested}
        }}
      }}
    };
  }
  return {
    {"jsonrpc", "2.0"},
    {"id", id},
    {"result", {
      {"protocolVersion", mcp::kLegacyVersion},
      {"capabilities", {{"tools", nlohmann::json::object()}}},
      {"serverInfo", {{"name", mcp::kServerName}, {"version", mcp::kServerVersion}}},
      {"instructions", mcp::kInstructions}
    }}
  };
}

nlohmann::json McpService::HandleDiscover(const nlohmann::json& id) {
  return {
    {"jsonrpc", "2.0"},
    {"id", id},
    {"result", {
      {"resultType", "complete"},
      {"supportedVersions", {mcp::kVersion}},
      {"capabilities", {{"tools", nlohmann::json::object()}}},
      {"_meta", {
        {"io.modelcontextprotocol/serverInfo", {
          {"name", mcp::kServerName},
          {"version", mcp::kServerVersion}
        }}
      }}
    }}
  };
}

nlohmann::json McpService::HandleToolsList(const nlohmann::json& id) {
  nlohmann::json tool_list = nlohmann::json::array();
  for (const auto* tool : tools::ToolRegistry::Instance().All()) {
    const std::string name = tool->Name();
    tool_list.push_back({
      {"name", name},
      {"description", tool->Description()},
      {"inputSchema", ChangesConsole(name) ? WithAgentArgument(tool->InputSchema())
                                           : tool->InputSchema()}
    });
  }
  return {
    {"jsonrpc", "2.0"},
    {"id", id},
    {"result", {{"resultType", "complete"}, {"tools", tool_list}}}
  };
}

nlohmann::json McpService::HandleToolsCall(const nlohmann::json& params,
                                            const nlohmann::json& id) {
  if (!params.contains("name") || !params["name"].is_string()) {
    return {
      {"jsonrpc", "2.0"},
      {"id", id},
      {"error", {{"code", -32602}, {"message", "Invalid params: missing tool name"}}}
    };
  }

  const std::string name = params["name"].get<std::string>();
  auto* tool = tools::ToolRegistry::Instance().Get(name);
  if (!tool) {
    return {
      {"jsonrpc", "2.0"},
      {"id", id},
      {"error", {{"code", -32602}, {"message", "Unknown tool: " + name}}}
    };
  }

  const nlohmann::json arguments = params.value("arguments", nlohmann::json::object());

  tools::ToolResult result;
  std::string refused;
  if (ChangesConsole(name))
    refused = ConsoleLease::Instance().Use(
        arguments.is_object() ? arguments.value("agent", std::string()) : std::string());
  try {
    result = refused.empty() ? tool->Execute(arguments) : tools::ToolResult{refused, true};
  } catch (const std::exception& e) {
    result = {std::string("Tool threw exception: ") + e.what(), true};
  } catch (...) {
    result = {"Tool threw unknown exception", true};
  }

  nlohmann::json content_item;
  if (!result.image_data.empty()) {
    content_item = {{"type", "image"}, {"data", result.image_data}, {"mimeType", result.image_mime}};
  } else {
    content_item = {{"type", "text"}, {"text", result.text}};
  }

  return {
    {"jsonrpc", "2.0"},
    {"id", id},
    {"result", {
      {"resultType", "complete"},
      {"isError", result.is_error},
      {"content", nlohmann::json::array({content_item})}
    }}
  };
}

}  // namespace services
