#include "settings.h"

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <wrl/client.h>

#include <fstream>
#include <memory>
#include <regex>

#include "i18n.h"
#include "squads_import.h"

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
  static const std::vector<ControlRow> layout = {
      {"Short pass / Contain", "A", Glyph::kFace, "keybind_a"},
      {"Shoot / Tackle", "B", Glyph::kFace, "keybind_b"},
      {"Lob pass / Slide tackle", "X", Glyph::kFace, "keybind_x"},
      {"Through ball / Rush goalkeeper", "Y", Glyph::kFace, "keybind_y"},
      {"Player run / Change player", "LB", Glyph::kBumper, "keybind_left_shoulder"},
      {"Finesse shot / Teammate contain", "RB", Glyph::kBumper, "keybind_right_shoulder"},
      {"Pace control / Jockey", "LT", Glyph::kTrigger, "keybind_left_trigger"},
      {"Sprint", "RT", Glyph::kTrigger, "keybind_right_trigger"},
      {"Move up", "L", Glyph::kStick, "keybind_lstick_up"},
      {"Move down", "L", Glyph::kStick, "keybind_lstick_down"},
      {"Move left", "L", Glyph::kStick, "keybind_lstick_left"},
      {"Move right", "L", Glyph::kStick, "keybind_lstick_right"},
      {"Right stick up", "R", Glyph::kStick, "keybind_rstick_up"},
      {"Right stick down", "R", Glyph::kStick, "keybind_rstick_down"},
      {"Right stick left", "R", Glyph::kStick, "keybind_rstick_left"},
      {"Right stick right", "R", Glyph::kStick, "keybind_rstick_right"},
      {"Left stick press", "L3", Glyph::kStick, "keybind_lstick_press"},
      {"Right stick press", "R3", Glyph::kStick, "keybind_rstick_press"},
      {"Tactics", "D-pad", Glyph::kPad, "keybind_dpad_up"},
      {"Custom tactics", "D-pad", Glyph::kPad, "keybind_dpad_down"},
      {"More defensive", "D-pad", Glyph::kPad, "keybind_dpad_left"},
      {"More attacking", "D-pad", Glyph::kPad, "keybind_dpad_right"},
      {"Pause", "Start", Glyph::kSmall, "keybind_start"},
      {"Switch to goalkeeper", "Back", Glyph::kSmall, "keybind_back"},
  };
  return layout;
}

// FIFA 14 on PC, keyboard only, Classic (its manual and the in-game table, as
// GamePressure shows it): the left hand moves, the right passes and shoots.
// Enter and Backspace are A and B's second keys, so the menus work as usual.
// L3 / R3, which it has no keys for, are Q and E.
const std::map<std::string, std::string>& DefaultKeys() {
  static const std::map<std::string, std::string> keys = {
      {"keybind_a", "L,Return"}, {"keybind_b", "Space,Backspace"}, {"keybind_x", "J"},
      {"keybind_y", "K"}, {"keybind_left_shoulder", "Control"}, {"keybind_right_shoulder", "I"},
      {"keybind_left_trigger", "CapsLock"}, {"keybind_right_trigger", "Shift"},
      {"keybind_lstick_up", "W"}, {"keybind_lstick_down", "S"}, {"keybind_lstick_left", "A"},
      {"keybind_lstick_right", "D"}, {"keybind_rstick_up", "Numpad8"},
      {"keybind_rstick_down", "Numpad2"}, {"keybind_rstick_left", "Numpad4"},
      {"keybind_rstick_right", "Numpad6"}, {"keybind_lstick_press", "Q"},
      {"keybind_rstick_press", "E"}, {"keybind_dpad_up", "Up"}, {"keybind_dpad_down", "Down"},
      {"keybind_dpad_left", "Left"}, {"keybind_dpad_right", "Right"},
      {"keybind_start", "Escape"}, {"keybind_back", "F"}};
  return keys;
}

std::map<std::string, std::string> KeyboardSettings() {
  std::map<std::string, std::string> settings = {{"mnk_mode", "true"},
                                                 {"mnk_exact_modifiers", "false"}};
  for (const auto& [key, keys] : DefaultKeys()) settings[key] = "\"" + keys + "\"";
  return settings;
}

std::string KeyName(unsigned vk, bool extended) {
  if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) return std::string(1, char(vk));
  if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) return "Numpad" + std::to_string(vk - VK_NUMPAD0);
  switch (vk) {
    case VK_SPACE: return "Space";
    case VK_RETURN: return extended ? "NumpadEnter" : "Return";
    case VK_TAB: return "Tab";
    case VK_BACK: return "Backspace";
    case VK_SHIFT: case VK_LSHIFT: case VK_RSHIFT: return "Shift";
    case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL: return "Control";
    case VK_MENU: case VK_LMENU: case VK_RMENU: return "Alt";
    case VK_CAPITAL: return "CapsLock";
    case VK_UP: return "Up";
    case VK_DOWN: return "Down";
    case VK_LEFT: return "Left";
    case VK_RIGHT: return "Right";
    case VK_INSERT: return "Insert";
    case VK_DELETE: return "Delete";
    case VK_HOME: return "Home";
    case VK_END: return "End";
    case VK_PRIOR: return "PageUp";
    case VK_NEXT: return "PageDown";
    case VK_ADD: return "NumpadPlus";
    case VK_SUBTRACT: return "NumpadMinus";
    case VK_MULTIPLY: return "NumpadStar";
    case VK_DIVIDE: return "NumpadSlash";
    case VK_OEM_COMMA: return "Comma";
    case VK_OEM_PERIOD: return "Period";
    case VK_OEM_MINUS: return "Minus";
    case VK_OEM_PLUS: return "Plus";
    case VK_OEM_1: return "Semicolon";
    case VK_OEM_2: return "Slash";
    case VK_OEM_3: return "Backtick";
    case VK_OEM_4: return "LBracket";
    case VK_OEM_5: return "Backslash";
    case VK_OEM_6: return "RBracket";
    case VK_OEM_7: return "Quote";
    default: return "";  // Esc (it cancels), F keys (the SDK's own), Windows keys...
  }
}

std::string KeyCap(const std::string& name) {
  static const std::map<std::string, std::string> caps = {
      {"Return", "Enter"}, {"Control", "Ctrl"}, {"CapsLock", "Caps"}, {"Escape", "Esc"},
      {"Backspace", "Bksp"}, {"PageUp", "PgUp"}, {"PageDown", "PgDn"}, {"Delete", "Del"},
      {"Insert", "Ins"}, {"NumpadEnter", "Num Enter"}, {"NumpadPlus", "Num +"},
      {"NumpadMinus", "Num -"}, {"NumpadStar", "Num *"}, {"NumpadSlash", "Num /"},
      {"Comma", ","}, {"Period", "."}, {"Minus", "-"}, {"Plus", "="}, {"Semicolon", ";"},
      {"Slash", "/"}, {"Backtick", "`"}, {"LBracket", "["}, {"Backslash", "\\"},
      {"RBracket", "]"}, {"Quote", "'"}};
  if (auto it = caps.find(name); it != caps.end()) return it->second;
  if (name.rfind("Numpad", 0) == 0) return "Num " + name.substr(6);
  return name;
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

// The desktop shortcut's file on a desktop (the player's own, or the shared
// one the installer puts it on), named as the installer names it.
fs::path DesktopShortcut(REFKNOWNFOLDERID desktop_id) {
  PWSTR desktop = nullptr;
  fs::path path;
  if (SUCCEEDED(SHGetKnownFolderPath(desktop_id, 0, nullptr, &desktop))) {
    path = fs::path(desktop) / L"2014 FIFA World Cup Brazil - Recompiled.lnk";
  }
  CoTaskMemFree(desktop);
  return path;
}

bool HasDesktopShortcut() {
  std::error_code ec;
  return fs::exists(DesktopShortcut(FOLDERID_Desktop), ec) ||
         fs::exists(DesktopShortcut(FOLDERID_PublicDesktop), ec);
}

// A desktop shortcut to this launcher with the game's icon (icon.ico in the
// data folder, made from the player's copy, see icon.h): the portable version
// has none, and a deleted one comes back this way. Not when one is there already.
void CreateDesktopShortcut(const fs::path& data_folder) {
  if (HasDesktopShortcut()) return;
  wchar_t exe[MAX_PATH];
  GetModuleFileNameW(nullptr, exe, MAX_PATH);
  fs::path shortcut = DesktopShortcut(FOLDERID_Desktop);
  Microsoft::WRL::ComPtr<IShellLinkW> link;
  Microsoft::WRL::ComPtr<IPersistFile> file;
  if (shortcut.empty() ||
      FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                              IID_PPV_ARGS(&link))) ||
      FAILED(link.As(&file))) {
    return;
  }
  link->SetPath(exe);
  link->SetWorkingDirectory(fs::path(exe).parent_path().c_str());
  link->SetDescription(L"2014 FIFA World Cup Brazil: Recompiled");
  std::error_code ec;
  fs::path icon = data_folder / "icon.ico";
  if (fs::exists(icon, ec)) link->SetIconLocation(icon.c_str(), 0);
  if (SUCCEEDED(file->Save(shortcut.c_str(), TRUE))) {
    SHChangeNotify(SHCNE_CREATE, SHCNF_PATHW, shortcut.c_str(), nullptr);
  }
}

// The resolution of the screen the game opens on (the `monitor` setting: 0 the
// main one, otherwise its place with the main one first, as the game counts), in
// real pixels. From its display mode, since the launcher isn't DPI aware and
// Windows scales the sizes it reports to it.
std::pair<int, int> ScreenSize(int monitor) {
  std::vector<std::wstring> devices;
  EnumDisplayMonitors(
      nullptr, nullptr,
      [](HMONITOR handle, HDC, LPRECT, LPARAM list) -> BOOL {
        auto& names = *reinterpret_cast<std::vector<std::wstring>*>(list);
        MONITORINFOEXW info = {};
        info.cbSize = sizeof(info);
        if (GetMonitorInfoW(handle, &info)) {
          names.insert(info.dwFlags & MONITORINFOF_PRIMARY ? names.begin() : names.end(),
                       info.szDevice);
        }
        return TRUE;
      },
      reinterpret_cast<LPARAM>(&devices));
  size_t index = monitor >= 1 && size_t(monitor) <= devices.size() ? size_t(monitor - 1) : 0;
  DEVMODEW mode = {};
  mode.dmSize = sizeof(mode);
  if (devices.empty() || !EnumDisplaySettingsW(devices[index].c_str(), ENUM_CURRENT_SETTINGS, &mode)) {
    return {1920, 1080};
  }
  return {int(mode.dmPelsWidth), int(mode.dmPelsHeight)};
}

Option OnOff(const char* label, std::string key, std::string fallback, std::string help) {
  Option option;
  option.label = T(label);
  option.help = std::move(help);
  option.choices = {{T("Off"), {{key, "false"}}, {}}, {T("On"), {{key, "true"}}, {}}};
  option.defaults = {{key, std::move(fallback)}};
  return option;
}

}  // namespace

std::vector<Section> MakeSections(const fs::path& data_folder, const fs::path& user_folder) {
  std::vector<Section> sections;

  Section display{T("Display"), 0, {}};
  {
    // In launcher.toml; the game gets it at Play (user_language, main.cpp).
    Option language;
    language.label = T("Language");
    language.help = T("The language of this menu and of the game, which asks for it at every "
                      "start with this one selected.");
    for (const LanguageInfo& info : Languages()) {
      language.choices.push_back({info.name, {{"language", std::string("\"") + info.code + "\""}}, {}});
    }
    language.defaults = {{"language", std::string("\"") + Info(Language::kEnglish).code + "\""}};
    language.local = true;
    display.options.push_back(language);

    Option window;
    window.label = T("Window");
    window.help = T("Fullscreen fills your screen and lets you switch apps quickly. Exclusive "
                    "gives the game the screen to itself. Windowed uses Window size.");
    window.choices = {
        {T("Fullscreen"), {{"fullscreen", "true"}, {"fullscreen_exclusive", "false"}}, {}},
        {T("Exclusive"), {{"fullscreen", "true"}, {"fullscreen_exclusive", "true"}}, {}},
        {T("Windowed"), {{"fullscreen", "false"}}, {}}};
    window.defaults = {{"fullscreen", "true"}, {"fullscreen_exclusive", "false"}};
    display.options.push_back(window);

    Option size;
    size.label = T("Window size");
    size.help = T("The size of the game's window in Windowed mode. Automatic makes it as large "
                  "as fits your screen. A size larger than your screen is shrunk to fit.");
    size.applies = [](const GameConfig& config) { return config.Get("fullscreen", "true") == "false"; };
    size.choices = {{T("Automatic"), {{"windowed_width", "0"}, {"windowed_height", "0"}}, {}}};
    // The 16:9 sizes that fit the chosen monitor (1280 x 720 always). Changing
    // Monitor builds the list again (ui.cpp).
    int monitor_index = std::atoi(GameConfig(data_folder / "fifawc14.toml").Get("monitor", "0").c_str());
    auto [screen_w, screen_h] = ScreenSize(monitor_index);
    for (auto [w, h] : {std::pair{1280, 720}, {1600, 900}, {1920, 1080}, {2560, 1440}, {3200, 1800},
                        {3840, 2160}, {5120, 2880}}) {
      if (w > 1280 && (w > screen_w || h > screen_h)) continue;
      size.choices.push_back({std::to_string(w) + " x " + std::to_string(h),
                              {{"windowed_width", std::to_string(w)},
                               {"windowed_height", std::to_string(h)}},
                              {}});
    }
    size.defaults = {{"windowed_width", "0"}, {"windowed_height", "0"}};
    display.options.push_back(size);

    Option picture;
    picture.label = T("Picture");
    picture.help = T("Fit keeps the right shape, with black bars on screens wider than 16:9. "
                     "Stretch fills the screen, but players and the HUD look wider.");
    picture.choices = {{T("Fit"), {{"present_letterbox", "true"}}, {}},
                       {T("Stretch"), {{"present_letterbox", "false"}}, {}}};
    picture.defaults = {{"present_letterbox", "true"}};
    display.options.push_back(picture);

    // The game's button icons (pc/src/game_files.h) and this menu's hints.
    Option buttons;
    buttons.label = T("Button icons");
    buttons.help = T("The controller buttons shown in the game and here. Automatic matches your "
                     "controller each time the game starts.");
    buttons.choices = {{T("Automatic"), {{"button_icons", "\"auto\""}}, {}},
                       {"Xbox", {{"button_icons", "\"xbox\""}}, {}},
                       {"PlayStation", {{"button_icons", "\"playstation\""}}, {}}};
    buttons.defaults = {{"button_icons", "\"auto\""}};
    display.options.push_back(buttons);

    int monitors = GetSystemMetrics(SM_CMONITORS);
    if (monitors > 1) {
      Option monitor;
      monitor.label = T("Monitor");
      monitor.help = T("The screen the game opens on.");
      monitor.choices = {{Tf("Monitor {0}", {"1"}), {{"monitor", "0"}}, {}}};
      for (int i = 2; i <= monitors; ++i) {
        monitor.choices.push_back({Tf("Monitor {0}", {std::to_string(i)}), {{"monitor", std::to_string(i)}}, {}});
      }
      monitor.defaults = {{"monitor", "0"}};
      display.options.push_back(monitor);
    }

    Option sync;
    sync.label = "VSync";
    sync.help = T("On stops tearing and keeps the game at your screen's refresh rate or below. "
                  "Off lets Frame rate go higher, but can tear.");
    // At Play, On also lowers the game's rate to the screen's (main.cpp).
    sync.choices = {{T("On"),
                     {{"d3d12_allow_variable_refresh_rate_and_tearing", "false"},
                      {"vsync_to_display", "true"}},
                     {}},
                    {T("Off"),
                     {{"d3d12_allow_variable_refresh_rate_and_tearing", "true"},
                      {"vsync_to_display", "false"}},
                     {}}};
    sync.defaults = {{"d3d12_allow_variable_refresh_rate_and_tearing", "false"},
                     {"vsync_to_display", "true"}};
    display.options.push_back(sync);
    Option rate;
    rate.label = T("Frame rate");
    rate.help = T("The game holds cutscenes at 40 FPS and many menus at 30, as on the console. "
                  "These rates lift both limits. Above 60 needs a faster PC, and heavy scenes may "
                  "run below the rate you pick.");
    for (int fps : {60, 120, 144, 165, 240}) {
      rate.choices.push_back({std::to_string(fps) + " FPS",
                              {{"present_interval_cap", "1"},
                               {"frame_rate_floor", std::to_string(fps)},
                               {"present_late_tolerance", "100"}},
                              {}});
    }
    rate.choices.push_back({T("As on console"),
                            {{"present_interval_cap", "0"},
                             {"frame_rate_floor", "0"},
                             {"present_late_tolerance", "0"}},
                            {}});
    rate.defaults = {{"present_interval_cap", "0"},
                     {"frame_rate_floor", "0"},
                     {"present_late_tolerance", "100"}};
    display.options.push_back(rate);

    display.options.push_back(OnOff("Show FPS", "show_fps", "false",
                                    T("The frame rate in the top left corner of the game.")));
  }
  sections.push_back(display);

  Section graphics{T("Graphics"), 1, {}};
  {
    Option resolution;
    resolution.label = T("Render resolution");
    resolution.help = T("What the game draws at, then fits to your screen, so any of these works "
                        "on any screen. 720p is the console's own. 1440p is sharp on a 1080p "
                        "screen. 4K needs a fast graphics card.");
    resolution.choices = {{"720p", {{"resolution_scale", "1"}}, {}},
                          {"1440p", {{"resolution_scale", "2"}}, {}},
                          {"4K", {{"resolution_scale", "3"}}, {}}};
    resolution.defaults = {{"resolution_scale", "1"}};
    graphics.options.push_back(resolution);

    Option upscaling;
    upscaling.label = T("Upscaling");
    upscaling.help = T("FSR sharpens the picture as it's fitted to your screen, most of all at "
                       "720p on a 1080p screen. It can look a bit artificial.");
    upscaling.choices = {{T("Off"), {{"present_effect", "\"bilinear\""}}, {}},
                         {"FSR", {{"present_effect", "\"fsr\""}}, {}}};
    upscaling.defaults = {{"present_effect", "\"bilinear\""}};
    graphics.options.push_back(upscaling);

    Option filtering;
    filtering.label = T("Texture filtering");
    filtering.help = T("Keeps the pitch markings and the stands sharp at shallow angles. Costs "
                       "next to nothing on today's graphics cards.");
    filtering.choices = {{T("Game's own"), {{"anisotropic_override", "-1"}}, {}},
                         {"2x", {{"anisotropic_override", "2"}}, {}},
                         {"4x", {{"anisotropic_override", "3"}}, {}},
                         {"8x", {{"anisotropic_override", "4"}}, {}},
                         {"16x", {{"anisotropic_override", "5"}}, {}}};
    filtering.defaults = {{"anisotropic_override", "3"}};
    graphics.options.push_back(filtering);
  }
  sections.push_back(graphics);

  // The launcher itself: what it does after the game, and this menu's own sound
  // (the game's volume is in the game's own settings).
  Section app{T("Launcher"), 3, {}};
  {
    // Unset, it closes under RecompLauncher, which backs up the saves once the
    // launcher has closed (main.cpp).
    Option after;
    after.label = T("After the game closes");
    after.help = T("Close the launcher takes you back to the desktop when you quit the game. "
                   "If the game stops with an error, the launcher still shows why.");
    after.choices = {{T("Show this menu"), {{"after_game", "\"menu\""}}, {}},
                     {T("Close the launcher"), {{"after_game", "\"close\""}}, {}}};
    bool recomplauncher = GetEnvironmentVariableW(L"RECOMPLAUNCHER", nullptr, 0) != 0;
    after.defaults = {{"after_game", recomplauncher ? "\"close\"" : "\"menu\""}};
    after.local = true;
    app.options.push_back(after);

    Option updates = OnOff("Check for updates", "check_updates", "true",
                           T("At start, asks this project's GitHub page whether a newer version "
                             "is out, and offers it. Nothing is sent about you or your game."));
    updates.local = true;
    app.options.push_back(updates);

    Option shortcut;
    shortcut.label = T("Desktop shortcut");
    shortcut.action = T("Create");
    shortcut.info = [] { return HasDesktopShortcut() ? std::string(T("On desktop")) : std::string(); };
    shortcut.run = [data_folder] { CreateDesktopShortcut(data_folder); };
    shortcut.outcome = [] {
      return HasDesktopShortcut()
                 ? std::make_pair(std::string(T("The shortcut is on your desktop.")), true)
                 : std::make_pair(std::string(T("The shortcut couldn't be created.")), false);
    };
    shortcut.help = T("A shortcut to this launcher on your desktop, with the game's icon. Useful "
                      "for the portable version, or if you deleted the one the installer made.");
    app.options.push_back(shortcut);

    auto percent = [](const char* label, std::string key, std::string fallback, std::string help) {
      Option option;
      option.label = T(label);
      option.help = std::move(help);
      for (int v = 10; v <= 100; v += 10) {
        option.choices.push_back({std::to_string(v) + "%", {{key, std::to_string(v)}}, {}});
      }
      option.defaults = {{key, std::move(fallback)}};
      option.local = true;
      return option;
    };
    auto only_here = [](const char* text) {
      return std::string(T(text)) + " " +
             T("Only in this menu. The game's own sound is set in the game.");
    };
    Option music = OnOff("Music", "menu_music", "true",
                         only_here("The game's EA Trax soundtrack, shuffled, while you're in "
                                   "this menu."));
    music.local = true;
    app.options.push_back(music);
    app.options.push_back(percent("Music volume", "music_volume", "100",
                                  only_here("How loud this menu's music is.")));
    Option sounds = OnOff("Sounds", "menu_sounds", "true",
                          only_here("The game's menu sounds as you move, select and go back."));
    sounds.local = true;
    app.options.push_back(sounds);
    app.options.push_back(percent("Sounds volume", "sounds_volume", "100",
                                  only_here("How loud this menu's sounds are.")));
  }
  sections.push_back(app);

  Section data{T("Data"), 2, {}};
  {
    Option saves;
    saves.label = T("Saves");
    saves.action = T("Open folder");
    saves.help = T("Your profile, settings and tournament saves.");
    saves.run = [user_folder] { OpenFolder(user_folder); };
    data.options.push_back(saves);

    auto imported = std::make_shared<SquadsImport>(SquadsImport::kCancelled);
    Option squads;
    squads.label = T("Squads");
    squads.action = T("Import");
    squads.help = T("Imports a squads file, such as EA's last squad update. The game loads it "
                    "by itself when it starts.");
    squads.run = [user_folder, imported] { *imported = PickAndImportSquads(user_folder); };
    squads.outcome = [imported]() -> std::pair<std::string, bool> {
      switch (*imported) {
        case SquadsImport::kOk:
          return {T("Squads imported. The game uses them from its next start."), true};
        case SquadsImport::kNotSquads:
          return {T("That file isn't a squads file."), false};
        case SquadsImport::kOtherGame:
          return {T("Those squads are for another FIFA game."), false};
        case SquadsImport::kWriteFailed:
          return {T("The squads couldn't be saved."), false};
        default:
          return {};
      }
    };
    data.options.push_back(squads);

    fs::path shaders = user_folder / "cache";
    Option cache;
    cache.label = T("Shader cache");
    cache.action = T("Clear");
    cache.confirm = true;
    cache.info = [shaders] { return Size(FolderBytes(shaders)); };
    cache.run = [shaders] {
      std::error_code ec;
      fs::remove_all(shaders, ec);
    };
    cache.help = T("Shaders the game has built, kept so later matches don't stutter. Clearing "
                   "only helps if graphics look wrong after an update.");
    data.options.push_back(cache);

    Option logs;
    logs.label = T("Logs");
    logs.action = T("Open folder");
    logs.help = T("The game's log (fifawc14.log). Attach it when you report a problem.");
    logs.run = [data_folder] { OpenFolder(data_folder); };
    data.options.push_back(logs);

  }
  sections.push_back(data);
  return sections;
}

}  // namespace launcher
