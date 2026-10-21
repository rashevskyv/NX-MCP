#define CATCH_CONFIG_MAIN
#include <catch2/catch.hpp>
#include "../../services/console_lease.hpp"

using services::ConsoleLease;
using std::chrono::seconds;

static const ConsoleLease::Clock::time_point kT0{};

TEST_CASE("Use without an agent name is refused") {
  ConsoleLease lease;
  REQUIRE_FALSE(lease.Use("", kT0).empty());
  REQUIRE(lease.Status(kT0)["busy"] == false);
}

TEST_CASE("First user takes the lease and others are refused with its name") {
  ConsoleLease lease;
  REQUIRE(lease.Use("kefir-hub", kT0).empty());
  std::string refused = lease.Use("cbfd", kT0 + seconds(10));
  REQUIRE(refused.find("kefir-hub") != std::string::npos);
  REQUIRE(lease.Use("kefir-hub", kT0 + seconds(10)).empty());
}

TEST_CASE("Lease expires so a silent holder does not block the console") {
  ConsoleLease lease;
  REQUIRE(lease.Use("a", kT0).empty());
  REQUIRE_FALSE(lease.Use("b", kT0 + seconds(ConsoleLease::kUseSeconds - 1)).empty());
  REQUIRE(lease.Use("b", kT0 + seconds(ConsoleLease::kUseSeconds)).empty());
  REQUIRE(lease.Status(kT0 + seconds(ConsoleLease::kUseSeconds))["agent"] == "b");
}

TEST_CASE("Claim keeps the task and clamps the length") {
  ConsoleLease lease;
  REQUIRE(lease.Claim("a", "doc screenshots", 999999, kT0).empty());
  auto status = lease.Status(kT0);
  REQUIRE(status["task"] == "doc screenshots");
  REQUIRE(status["seconds_left"] == ConsoleLease::kMaxSeconds);
  REQUIRE_FALSE(lease.Claim("b", "other", 60, kT0).empty());
}

TEST_CASE("Use by the holder does not shorten a longer claim") {
  ConsoleLease lease;
  REQUIRE(lease.Claim("a", "long job", 1800, kT0).empty());
  REQUIRE(lease.Use("a", kT0 + seconds(10)).empty());
  REQUIRE(lease.Status(kT0)["seconds_left"] == 1800);
}

TEST_CASE("Only the holder can release") {
  ConsoleLease lease;
  REQUIRE(lease.Claim("a", "t", 600, kT0).empty());
  REQUIRE_FALSE(lease.Release("b", kT0));
  REQUIRE(lease.Release("a", kT0));
  REQUIRE(lease.Status(kT0)["busy"] == false);
}
