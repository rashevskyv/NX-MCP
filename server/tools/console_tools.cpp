// console_status / console_lock / console_unlock: the shared-console lease
// (services::ConsoleLease) as MCP tools, so agents can see and reserve the
// Switch before they drive it.
#include "tool_registry.hpp"
#include "console_lease.hpp"

namespace tools {

using services::ConsoleLease;

class ConsoleStatusTool : public Tool {
 public:
  std::string Name() const override { return "console_status"; }
  std::string Description() const override {
    return "Shows which agent drives the console now (busy, agent, task, seconds_left). "
           "Call it before you start; if another agent is busy, wait.";
  }
  nlohmann::json InputSchema() const override {
    return {{"type", "object"}, {"properties", nlohmann::json::object()},
            {"required", nlohmann::json::array()}};
  }
  ToolResult Execute(const nlohmann::json&) override {
    return {ConsoleLease::Instance().Status().dump(), false};
  }
};

class ConsoleLockTool : public Tool {
 public:
  std::string Name() const override { return "console_lock"; }
  std::string Description() const override {
    return "Reserves the console for your agent with a task note, so other agents are "
           "refused input, launches and writes. Any input call also extends the lease "
           "by 5 minutes. Call console_unlock when you finish.";
  }
  nlohmann::json InputSchema() const override {
    return {
      {"type", "object"},
      {"properties", {
        {"agent",   {{"type", "string"}, {"description", "Your session name"}}},
        {"task",    {{"type", "string"}, {"description", "What you are doing, in a few words"}}},
        {"minutes", {{"type", "number"}, {"description", "Lease length, 1-60 (default 15)"}}}
      }},
      {"required", {"agent", "task"}}
    };
  }
  ToolResult Execute(const nlohmann::json& args) override {
    int minutes = 15;
    if (args.contains("minutes") && args["minutes"].is_number())
      minutes = (int)args["minutes"].get<double>();
    std::string refused = ConsoleLease::Instance().Claim(
        args.value("agent", std::string()), args.value("task", std::string()), minutes * 60);
    if (!refused.empty()) return {refused, true};
    return {ConsoleLease::Instance().Status().dump(), false};
  }
};

class ConsoleUnlockTool : public Tool {
 public:
  std::string Name() const override { return "console_unlock"; }
  std::string Description() const override {
    return "Frees the console lease held by your agent.";
  }
  nlohmann::json InputSchema() const override {
    return {
      {"type", "object"},
      {"properties", {{"agent", {{"type", "string"}, {"description", "Your session name"}}}}},
      {"required", {"agent"}}
    };
  }
  ToolResult Execute(const nlohmann::json& args) override {
    if (ConsoleLease::Instance().Release(args.value("agent", std::string())))
      return {"Console is free.", false};
    return {"You do not hold the lease. " + ConsoleLease::Instance().Status().dump(), true};
  }
};

static bool kStatusRegistered =
    ToolRegistry::Instance().Register(std::make_unique<ConsoleStatusTool>());
static bool kLockRegistered =
    ToolRegistry::Instance().Register(std::make_unique<ConsoleLockTool>());
static bool kUnlockRegistered =
    ToolRegistry::Instance().Register(std::make_unique<ConsoleUnlockTool>());

}  // namespace tools
