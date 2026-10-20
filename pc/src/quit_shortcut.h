#pragma once

#include <filesystem>
#include <functional>
#include <memory>

struct ImFont;

namespace rex {
class Runtime;
}
namespace rex::ui {
class ImGuiDialog;
class ImGuiDrawer;
class ImmediateDrawer;
class Window;
}  // namespace rex::ui

// Quitting with the controller: holding Back + Start (Create + Options on a
// PlayStation pad) for 2 seconds closes the game, with a bar filling up meanwhile
// that letting go of either cancels. The game never needs both held, so it
// can't happen by accident. For the first seconds of a session a hint shows
// it ("Hold [Back] + [Start] to quit (Alt+F4)").
// The panel behind it is quit-panel.png in the data folder (the launcher
// writes it from the player's game), and the button icons and names are
// PlayStation or Xbox ones by quit_buttons (the launcher sets it from the
// controller in use). The runtime is created after the overlay, so it's
// looked up when drawing.
std::unique_ptr<rex::ui::ImGuiDialog> CreateQuitShortcut(
    rex::ui::ImGuiDrawer* drawer, ImFont* font, std::function<rex::Runtime*()> runtime,
    rex::ui::Window* window, rex::ui::ImmediateDrawer* immediate_drawer,
    const std::filesystem::path& folder);
