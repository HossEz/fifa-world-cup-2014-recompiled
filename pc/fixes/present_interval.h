#pragma once

#include <cstdint>

// Frames the game has queued for display so far (each call of its present
// function, sub_82453028): a frame-rate counter's source that works in every
// build, unlike the SDK's perf counters (compiled out of Release builds).
uint64_t GuestFramesQueued();
