// A watchdog for the launcher's main thread: if the main loop stops ticking
// for a few seconds (the window "not responding"), the main thread's call
// stack is written to the log, with function names from the launcher's PDB.

#pragma once

namespace launcher {

// Starts watching the calling thread (call from the main loop's thread).
// `window`: also report when Windows considers it hung (not responding).
void StartWatchdog(void* window);
// Call once per main loop iteration.
void WatchdogTick();
// Stops watching until the next tick (around a modal dialog, which waits on the player).
void WatchdogPause();

}  // namespace launcher
