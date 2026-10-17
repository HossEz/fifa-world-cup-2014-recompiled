#include "settings.h"

#include <windows.h>
#include <shellapi.h>

#include <fstream>
#include <regex>

namespace launcher {

namespace fs = std::filesystem;

// ------------------------------------------------------------------ GameConfig

namespace {

// `key = value  # comment`: groups 1 (up to the value), 2 (value), 3 (rest).
std::regex SettingLine(const std::string& key) {
  return std::regex("^(\\s*" + key + "\\s*=\\s*)((?:\"[^\"]*\"|[^#\\s])*)(.*)$");
}

}  // namespace

GameConfig::GameConfig(fs::path path) : path_(std::move(path)) {
  std::ifstream file(path_);
  std::string line;
  while (std::getline(file, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    lines_.push_back(line);
  }
}

std::string GameConfig::Get(const std::string& key, const std::string& fallback) const {
  std::regex pattern = SettingLine(key);
  std::smatch match;
  for (const std::string& line : lines_) {
    if (std::regex_match(line, match, pattern)) return match[2].str();
  }
  return fallback;
}

void GameConfig::Set(const std::string& key, const std::string& value) {
  std::regex pattern = SettingLine(key);
  std::smatch match;
  for (std::string& line : lines_) {
    if (std::regex_match(line, match, pattern)) {
      line = match[1].str() + value + match[3].str();
      return;
    }
  }
  lines_.push_back(key + " = " + value);
}

bool GameConfig::Save() const {
  std::ofstream file(path_, std::ios::trunc);
  for (const std::string& line : lines_) file << line << '\n';
  return bool(file);
}

// ------------------------------------------------------------------ Option

size_t Option::Current(const GameConfig& config) const {
  for (size_t i = 0; i < choices.size(); ++i) {
    bool matches = true;
    for (const auto& [key, value] : choices[i].writes) {
      auto fallback = defaults.find(key);
      if (config.Get(key, fallback != defaults.end() ? fallback->second : "") != value) {
        matches = false;
        break;
      }
    }
    if (matches) return i;
  }
  return 0;
}

void Option::Step(GameConfig& config, int delta) const {
  if (choices.empty()) return;
  size_t count = choices.size();
  size_t next = (Current(config) + count + delta) % count;
  for (const auto& [key, value] : choices[next].writes) config.Set(key, value);
  for (const auto& [key, value] : choices[next].also) config.Set(key, value);
  config.Save();
}

// ------------------------------------------------------------------ keyboard

const std::vector<ControlRow>& KeyboardLayout() {
  // Sticks: one setting per direction (up, down, left, right), keys in the same order.
  static const std::vector<ControlRow> layout = {
      {"A", Glyph::kFace, "keybind_a", "Space,Z", "Space  Z", "Short pass"},
      {"B", Glyph::kFace, "keybind_b", "X,Backspace", "X  Backspace", "Shoot"},
      {"X", Glyph::kFace, "keybind_x", "C", "C", "Lob pass, cross"},
      {"Y", Glyph::kFace, "keybind_y", "V", "V", "Through ball"},
      {"LB", Glyph::kBumper, "keybind_left_shoulder", "Q", "Q", ""},
      {"RB", Glyph::kBumper, "keybind_right_shoulder", "E", "E", ""},
      {"LT", Glyph::kTrigger, "keybind_left_trigger", "1,Control", "1  Ctrl", ""},
      {"RT", Glyph::kTrigger, "keybind_right_trigger", "Shift,3", "Shift  3", "Sprint"},
      {"L", Glyph::kStick,
       "keybind_lstick_up,keybind_lstick_down,keybind_lstick_left,keybind_lstick_right",
       "W,S,A,D", "W  A  S  D", "Move"},
      {"R", Glyph::kStick,
       "keybind_rstick_up,keybind_rstick_down,keybind_rstick_left,keybind_rstick_right",
       "I,K,J,L", "I  J  K  L", "Skill moves"},
      {"L3", Glyph::kStick, "keybind_lstick_press", "F", "F", ""},
      {"R3", Glyph::kStick, "keybind_rstick_press", "G", "G", ""},
      {"D-pad", Glyph::kPad, "keybind_dpad_up,keybind_dpad_down,keybind_dpad_left,keybind_dpad_right",
       "Up,Down,Left,Right", "Arrow keys", "Tactics"},
      {"Start", Glyph::kSmall, "keybind_start", "Return", "Enter", "Pause"},
      {"Back", Glyph::kSmall, "keybind_back", "Tab", "Tab", ""},
  };
  return layout;
}

// The keybind settings for the layout above: each row's setting gets its keys;
// a stick's four settings get one key each.
std::map<std::string, std::string> KeyboardSettings() {
  std::map<std::string, std::string> settings = {{"mnk_mode", "true"},
                                                 {"mnk_exact_modifiers", "false"}};
  auto split = [](const std::string& text) {
    std::vector<std::string> parts;
    size_t start = 0;
    for (size_t comma; (comma = text.find(',', start)) != std::string::npos; start = comma + 1) {
      parts.push_back(text.substr(start, comma - start));
    }
    parts.push_back(text.substr(start));
    return parts;
  };
  for (const ControlRow& row : KeyboardLayout()) {
    std::vector<std::string> names = split(row.setting);
    if (names.size() == 1) {
      settings[names[0]] = "\"" + std::string(row.keys) + "\"";
    } else {
      std::vector<std::string> keys = split(row.keys);
      for (size_t i = 0; i < names.size() && i < keys.size(); ++i) {
        settings[names[i]] = "\"" + keys[i] + "\"";
      }
    }
  }
  return settings;
}


// ------------------------------------------------------------------ sections

namespace {

void OpenFolder(const fs::path& folder) {
  std::error_code ec;
  fs::create_directories(folder, ec);
  ShellExecuteW(nullptr, L"open", folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

uint64_t FolderBytes(const fs::path& folder) {
  uint64_t total = 0;
  std::error_code ec;
  for (auto it = fs::recursive_directory_iterator(folder, ec);
       it != fs::recursive_directory_iterator(); it.increment(ec)) {
    if (ec) break;
    if (it->is_regular_file(ec)) total += it->file_size(ec);
  }
  return total;
}

std::string Size(uint64_t bytes) {
  char text[32];
  if (bytes < 1024 * 1024) {
    std::snprintf(text, sizeof(text), "%llu KB", (unsigned long long)(bytes / 1024));
  } else {
    std::snprintf(text, sizeof(text), "%llu MB", (unsigned long long)(bytes / (1024 * 1024)));
  }
  return text;
}

Option OnOff(std::string label, std::string key, std::string fallback, std::string help) {
  Option option;
  option.label = std::move(label);
  option.help = std::move(help);
  option.choices = {{"Off", {{key, "false"}}, {}}, {"On", {{key, "true"}}, {}}};
  option.defaults = {{key, std::move(fallback)}};
  return option;
}

}  // namespace

std::vector<Section> MakeSections(const fs::path& game_folder, const fs::path& user_folder) {
  std::vector<Section> sections;

  Section display{"Display", 0, {}};
  {
    Option window;
    window.label = "Window";
    window.help = "Fullscreen covers your screen as a borderless window (switching apps is "
                  "quick). Exclusive takes over the screen. Windowed uses Window size.";
    window.choices = {
        {"Fullscreen", {{"fullscreen", "true"}, {"fullscreen_exclusive", "false"}}, {}},
        {"Exclusive", {{"fullscreen", "true"}, {"fullscreen_exclusive", "true"}}, {}},
        {"Windowed", {{"fullscreen", "false"}}, {}}};
    window.defaults = {{"fullscreen", "true"}, {"fullscreen_exclusive", "false"}};
    display.options.push_back(window);

    Option size;
    size.label = "Window size";
    size.help = "The game's window when Window is Windowed (in fullscreen the game fills your "
                "screen).";
    size.applies = [](const GameConfig& config) { return config.Get("fullscreen", "true") == "false"; };
    size.choices = {{"Automatic", {{"window_width", "0"}, {"window_height", "0"}}, {}}};
    for (auto [w, h] : {std::pair{1280, 720}, {1600, 900}, {1920, 1080}, {2560, 1440}}) {
      size.choices.push_back({std::to_string(w) + " x " + std::to_string(h),
                              {{"window_width", std::to_string(w)},
                               {"window_height", std::to_string(h)}},
                              {}});
    }
    size.defaults = {{"window_width", "0"}, {"window_height", "0"}};
    display.options.push_back(size);

    int monitors = GetSystemMetrics(SM_CMONITORS);
    if (monitors > 1) {
      Option monitor;
      monitor.label = "Monitor";
      monitor.help = "The screen the game opens on.";
      monitor.choices = {{"Main", {{"monitor", "0"}}, {}}};
      for (int i = 2; i <= monitors; ++i) {
        monitor.choices.push_back({"Monitor " + std::to_string(i), {{"monitor", std::to_string(i)}}, {}});
      }
      monitor.defaults = {{"monitor", "0"}};
      display.options.push_back(monitor);
    }

    display.options.push_back(OnOff(
        "Variable refresh", "d3d12_allow_variable_refresh_rate_and_tearing", "true",
        "Shows each frame the moment it's ready: smooth on G-Sync / FreeSync screens, and it "
        "keeps the game at full speed. Leave it on; off, the frame rate can drop to 30-45."));
    Option rate;
    rate.label = "Frame rate";
    rate.help = "The game holds cutscenes at 40 FPS and many menus at 30, as on the console. "
                "60 everywhere lifts both limits.";
    rate.choices = {{"60 everywhere", {{"present_interval_cap", "1"}, {"frame_rate_floor", "60"}}, {}},
                    {"As on console", {{"present_interval_cap", "0"}, {"frame_rate_floor", "0"}}, {}}};
    rate.defaults = {{"present_interval_cap", "0"}, {"frame_rate_floor", "0"}};
    display.options.push_back(rate);

    display.options.push_back(OnOff("Show FPS", "show_fps", "false",
                                    "The frame rate in the top left corner of the game."));
  }
  sections.push_back(display);

  Section graphics{"Graphics", 1, {}};
  {
    Option resolution;
    resolution.label = "Resolution";
    resolution.help = "What the game draws at, before it's fitted to your screen: the "
                      "console's 720p, or 2 or 3 times as wide and high. On a 1080p screen "
                      "1440p is the sharp choice, but for now it costs a lot of speed in cutscenes.";
    resolution.choices = {{"720p", {{"resolution_scale", "1"}}, {}},
                          {"1440p", {{"resolution_scale", "2"}}, {}},
                          {"4K", {{"resolution_scale", "3"}}, {}}};
    resolution.defaults = {{"resolution_scale", "1"}};
    graphics.options.push_back(resolution);

    Option aa;
    aa.label = "Anti-aliasing";
    aa.help = "Smooths jagged edges on top of the game's own. FXAA is light; Extreme smooths "
              "more and softens the picture a little.";
    aa.choices = {{"Off", {{"swap_post_effect", "\"none\""}}, {}},
                  {"FXAA", {{"swap_post_effect", "\"fxaa\""}}, {}},
                  {"FXAA Extreme", {{"swap_post_effect", "\"fxaa_extreme\""}}, {}}};
    aa.defaults = {{"swap_post_effect", "\"none\""}};
    graphics.options.push_back(aa);

    Option filtering;
    filtering.label = "Texture filtering";
    filtering.help = "Keeps the pitch markings and the stands sharp at shallow angles. Costs "
                     "next to nothing on today's graphics cards.";
    filtering.choices = {{"Game's own", {{"anisotropic_override", "-1"}}, {}},
                         {"2x", {{"anisotropic_override", "2"}}, {}},
                         {"4x", {{"anisotropic_override", "3"}}, {}},
                         {"8x", {{"anisotropic_override", "4"}}, {}},
                         {"16x", {{"anisotropic_override", "5"}}, {}}};
    filtering.defaults = {{"anisotropic_override", "3"}};
    graphics.options.push_back(filtering);
  }
  sections.push_back(graphics);

  // This menu's own sound; the game's volume is in the game's own settings.
  Section sound{"Menu sound", 3, {}};
  {
    auto percent = [](std::string label, std::string key, std::string fallback, std::string help) {
      Option option;
      option.label = std::move(label);
      option.help = std::move(help);
      for (int v = 10; v <= 100; v += 10) {
        option.choices.push_back({std::to_string(v) + "%", {{key, std::to_string(v)}}, {}});
      }
      option.defaults = {{key, std::move(fallback)}};
      option.local = true;
      return option;
    };
    const char* only_here = " Only in this menu: the game's own sound is set in the game.";
    Option music = OnOff("Music", "menu_music", "true",
                         std::string("The game's EA Trax soundtrack, shuffled, while you're in "
                                     "this menu.") + only_here);
    music.local = true;
    sound.options.push_back(music);
    sound.options.push_back(percent("Music volume", "music_volume", "50",
                                    std::string("How loud this menu's music is.") + only_here));
    Option sounds = OnOff("Sounds", "menu_sounds", "true",
                          std::string("The game's menu sounds as you move, select and go back.") +
                              only_here);
    sounds.local = true;
    sound.options.push_back(sounds);
    sound.options.push_back(percent("Sounds volume", "sounds_volume", "80",
                                    std::string("How loud this menu's sounds are.") + only_here));
  }
  sections.push_back(sound);

  Section data{"Data", 2, {}};
  {
    Option saves;
    saves.label = "Saves";
    saves.action = "Open folder";
    saves.help = "Your profile, settings and tournament saves.";
    saves.run = [user_folder] { OpenFolder(user_folder); };
    data.options.push_back(saves);

    fs::path shaders = user_folder / "cache";
    Option cache;
    cache.label = "Shader cache";
    cache.action = "Clear";
    cache.confirm = true;
    cache.info = [shaders] { return Size(FolderBytes(shaders)); };
    cache.run = [shaders] {
      std::error_code ec;
      fs::remove_all(shaders, ec);
    };
    cache.help = "Shaders the game has built, kept so later matches don't stutter. Clearing "
                 "only helps if graphics look wrong after an update.";
    data.options.push_back(cache);

    Option logs;
    logs.label = "Logs";
    logs.action = "Open folder";
    logs.help = "The game's log (fifawc14.log). Attach it when you report a problem.";
    logs.run = [game_folder] { OpenFolder(game_folder); };
    data.options.push_back(logs);
  }
  sections.push_back(data);
  return sections;
}

}  // namespace launcher
