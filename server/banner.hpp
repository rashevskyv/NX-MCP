#pragma once

namespace banner {

// Starts a thread that shows "agent at work" at the top of the console screen
// while services::ConsoleLease is held, and removes it when the lease ends.
// The banner is not added to the screenshot layer stack, so agents' own
// screenshots stay clean.
void Start();

}  // namespace banner
