#include "console_lease.hpp"
#include <algorithm>

namespace services {

using std::chrono::duration_cast;
using std::chrono::seconds;

ConsoleLease& ConsoleLease::Instance() {
  static ConsoleLease lease;
  return lease;
}

bool ConsoleLease::HeldByOther(const std::string& agent, Clock::time_point now) const {
  return !agent_.empty() && now < until_ && agent_ != agent;
}

std::string ConsoleLease::BusyMessage(Clock::time_point now) const {
  long left = (long)duration_cast<seconds>(until_ - now).count();
  return "Console is busy: agent '" + agent_ + "' is working on '" + task_ +
         "' (" + std::to_string(left) + " s left). Do not press buttons, launch or "
         "write until console_status shows it free; screenshots and reads are allowed.";
}

std::string ConsoleLease::Use(const std::string& agent, Clock::time_point now) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (agent.empty())
    return "Pass \"agent\": your session name, so other agents see who drives the console.";
  if (HeldByOther(agent, now)) return BusyMessage(now);
  if (agent_ != agent || now >= until_) {
    agent_ = agent;
    task_  = "(no task given; call console_lock)";
    until_ = now;
  }
  until_ = std::max(until_, now + seconds(kUseSeconds));
  return "";
}

std::string ConsoleLease::Claim(const std::string& agent, const std::string& task,
                                int secs, Clock::time_point now) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (agent.empty()) return "agent is required";
  if (HeldByOther(agent, now)) return BusyMessage(now);
  agent_ = agent;
  task_  = task.empty() ? "(no task given)" : task;
  until_ = now + seconds(std::clamp(secs, kMinSeconds, kMaxSeconds));
  return "";
}

bool ConsoleLease::Release(const std::string& agent, Clock::time_point now) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (agent_.empty() || agent_ != agent || now >= until_) return false;
  agent_.clear();
  task_.clear();
  until_ = {};
  return true;
}

nlohmann::json ConsoleLease::Status(Clock::time_point now) const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (agent_.empty() || now >= until_) return {{"busy", false}};
  return {
    {"busy", true},
    {"agent", agent_},
    {"task", task_},
    {"seconds_left", (long)duration_cast<seconds>(until_ - now).count()}
  };
}

}  // namespace services
