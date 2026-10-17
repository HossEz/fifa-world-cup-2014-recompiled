#pragma once

namespace rex::ui {
class Window;
}

// The game window as players see it, set up once it exists:
// - the title (the project's name) and the icon: icon.ico in the data folder
//   (data_folder.h), made by the launcher from the player's own copy of the
//   game (launcher/src/icon.h); without it, Windows' plain icon;
// - windowed: the size from windowed_width / windowed_height (0: as large as
//   fits the screen), fitted to the screen it opens on, and kept 16:9 while
//   it's resized, like the picture (otherwise black bars appear).
//
// The SDK's own window_width / window_height also set the video mode the game
// is told about; they stay unset, so that remains the console's 720p.
void SetUpGameWindow(rex::ui::Window* window);

// The SDK's overlay keys (F3 debug, F4 settings, F7 achievements, ` console)
// are for developers: removed unless debug_keys is set in fifawc14.toml.
void RemoveDebugKeys();
