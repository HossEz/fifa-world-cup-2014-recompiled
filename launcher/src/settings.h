// What the player can change in the launcher, and the game's fifawc14.toml
// they're written to.
//
// A value option steps through choices; each choice is a set of game settings
// (cvars) it writes, so one choice can set several ("60 FPS everywhere" sets
// both caps). The current choice is the one whose settings all match the file,
// with the game's own default for settings the file doesn't name. An action
// option runs something (open a folder, clear the shader cache).

#pragma once

#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace launcher {

// fifawc14.toml, edited line by line so its comments and order stay as they are.
class GameConfig {
 public:
  explicit GameConfig(std::filesystem::path path);
  // A setting's value as written (strings with their quotes), or `fallback`.
  std::string Get(const std::string& key, const std::string& fallback) const;
  // Replaces the setting's value (keeping a trailing comment) or appends it.
  void Set(const std::string& key, const std::string& value);
  bool Save() const;

 private:
  std::filesystem::path path_;
  std::vector<std::string> lines_;
};

struct Choice {
  std::string label;
  std::map<std::string, std::string> writes;  // setting -> TOML value
  // Written too when chosen, but not compared to find the current choice.
  std::map<std::string, std::string> also;
};

struct Option {
  std::string label;
  std::string help;
  // A value option:
  std::vector<Choice> choices;
  std::map<std::string, std::string> defaults;  // the game's value when unset
  // An action option:
  std::string action;                  // its button, e.g. "Clear"
  bool confirm = false;                // needs a second press
  std::function<std::string()> info;   // shown before the button (e.g. a size)
  std::function<void()> run;

  // Kept in the launcher's own launcher.toml (not a game setting).
  bool local = false;
  // When it applies (e.g. Window size only when Windowed); otherwise it's shown
  // greyed out and can't be changed. Unset: always.
  std::function<bool(const GameConfig&)> applies;

  bool is_action() const { return choices.empty(); }
  size_t Current(const GameConfig& config) const;
  void Step(GameConfig& config, int delta) const;
};

struct Section {
  std::string label;
  int icon;  // index into the UI's section icons
  std::vector<Option> options;
};

// The keyboard layout: one row per controller button, the keys the launcher
// binds to it (the game's keybind_* settings, SDK key names) and what it does
// in a match. The Controls page shows the same table.
enum class Glyph { kFace, kBumper, kTrigger, kStick, kPad, kSmall };
struct ControlRow {
  const char* button;   // "A", "LB", "L", "D-pad", ...
  Glyph glyph;
  const char* setting;  // keybind_* setting(s), comma-separated for a stick's four
  const char* keys;     // SDK key names, as written (comma = alternatives)
  const char* shown;    // the keys as shown to the player
  const char* match;    // what it does in a match ("" if nothing by default)
};
const std::vector<ControlRow>& KeyboardLayout();
// The game settings for that layout: keyboard play on (it's merged with any
// controller, so it never gets in a controller's way), every keybind_*, and
// mnk_exact_modifiers off so Shift can sprint while steering.
std::map<std::string, std::string> KeyboardSettings();

// The launcher's sections. `data_folder`: the settings and logs (see
// DataFolder in main.cpp); `user_folder`: the game's saves and caches
// (Documents\2014 FIFA World Cup Brazil, or the portable copy's saves folder).
std::vector<Section> MakeSections(const std::filesystem::path& data_folder,
                                  const std::filesystem::path& user_folder);

}  // namespace launcher
