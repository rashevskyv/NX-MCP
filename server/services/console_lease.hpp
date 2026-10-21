#pragma once
#include <nlohmann/json.hpp>
#include <chrono>
#include <mutex>
#include <string>

namespace services {

// Who is driving the console right now. Several agents share one Switch;
// a call that presses buttons, launches or writes must name its agent, and
// while one agent holds the lease every other agent is refused with the
// holder's name and task. The lease expires on its own if the holder
// stops calling, so a crashed agent never locks the console for good.
class ConsoleLease {
 public:
  using Clock = std::chrono::steady_clock;

  static constexpr int kUseSeconds = 300;   // granted or extended by any use
  static constexpr int kMinSeconds = 60;
  static constexpr int kMaxSeconds = 3600;

  static ConsoleLease& Instance();

  // "" when `agent` may change the console now (the lease is taken or
  // extended for it); otherwise the reason it may not.
  std::string Use(const std::string& agent, Clock::time_point now = Clock::now());

  // Takes the lease for `seconds` with a task note; "" on success.
  std::string Claim(const std::string& agent, const std::string& task, int seconds,
                    Clock::time_point now = Clock::now());

  // Frees the lease if `agent` holds it.
  bool Release(const std::string& agent, Clock::time_point now = Clock::now());

  nlohmann::json Status(Clock::time_point now = Clock::now()) const;

 private:
  bool HeldByOther(const std::string& agent, Clock::time_point now) const;
  std::string BusyMessage(Clock::time_point now) const;

  mutable std::mutex mutex_;  // HTTP thread writes, banner thread reads
  std::string       agent_;
  std::string       task_;
  Clock::time_point until_{};
};

}  // namespace services
