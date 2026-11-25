// What the player can change in the launcher, and the game's fifawc14.toml
// they're written to.
//
// A value option steps through choices; each choice is a set of game settings
// (cvars) it writes, so one choice can set several ("120 FPS" sets both
// caps). The current choice is the one whose settings all match the file,
// with the game's own default for settings the file doesn't name. An action
// option runs something (open a folder, clear the shader cache).

#pragma once

#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <utility>
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
  // After run: a message for the player (bottom left) and whether it went well
  // (green) or not (red). None if empty.
  std::function<std::pair<std::string, bool>()> outcome;

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

// The keyboard, as the Controls page lists it: one row per action (a stick or
// the d-pad has one per direction), the controller button it is, and the
// game's keybind_* setting that holds its keys.
enum class Glyph { kFace, kBumper, kTrigger, kStick, kPad, kSmall };
struct ControlRow {
  const char* label;    // what it does: attacking / defending (one when both are the same)
  const char* button;   // "A", "LB", "L", "Up" (the D-pad), ...
  Glyph glyph;
  const char* setting;  // keybind_*
};
const std::vector<ControlRow>& KeyboardLayout();
// The default keys: setting -> keys (SDK key names, comma = a second key).
const std::map<std::string, std::string>& DefaultKeys();
// The game settings for keyboard play: on (it's merged with any controller, so
// it never gets in a controller's way), the default keybind_*, and
// mnk_exact_modifiers off so Shift can sprint while steering.
std::map<std::string, std::string> KeyboardSettings();
// The SDK's name for a Windows virtual key ("" for keys that can't be bound:
// Esc, the F keys, Windows keys...). `extended`: the key message's extended bit.
std::string KeyName(unsigned virtual_key, bool extended);
// A key name as shown on a key cap ("Return" -> "Enter").
std::string KeyCap(const std::string& name);

// The launcher's sections. `data_folder`: the settings and logs (see
// DataFolder in main.cpp); `user_folder`: the game's saves and caches
// (Documents\2014 FIFA World Cup Brazil, or the portable copy's saves folder).
std::vector<Section> MakeSections(const std::filesystem::path& data_folder,
                                  const std::filesystem::path& user_folder);

}  // namespace launcher
