#pragma once

#include <filesystem>
#include <memory>

struct ImFont;
struct ImFontAtlas;

namespace rex::ui {
class ImGuiDialog;
class ImGuiDrawer;
}  // namespace rex::ui

// The frame rate in the game's top left corner (show_fps, the launcher's
// Settings, Display, Show FPS): the frames the game makes per second, updated
// every half second, in the game's own condensed font when the launcher has
// put it next to the exe, with a soft shadow and no background.

// Adds the counter's font to the overlay's font atlas (call from
// OnConfigureFonts). Null if the font file isn't there (ImGui's own is used).
ImFont* AddFpsFont(ImFontAtlas* atlas, const std::filesystem::path& folder);

// The counter, or null if show_fps is off.
std::unique_ptr<rex::ui::ImGuiDialog> CreateFpsCounter(rex::ui::ImGuiDrawer* drawer, ImFont* font);
