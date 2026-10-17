#pragma once

namespace rex::ui {
class Window;
}

// The game window's title (the project's name) and icon: icon.ico next to the
// exe, which the launcher makes from the player's own copy of the game
// (launcher/src/icon.h). Without it the window keeps Windows' plain icon.
void ApplyWindowBranding(rex::ui::Window* window);

// The SDK's overlay keys (F3 debug, F4 settings, F7 achievements, ` console)
// are for developers: removed unless debug_keys is set in fifawc14.toml.
void RemoveDebugKeys();
