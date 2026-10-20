// The launcher's screens, drawn with Dear ImGui's draw lists in the game's own
// front-end style: torn painted boards on the game's deep blue, the menu
// watercolour on the right, Futura Std, and the yellow brush stroke that sweeps
// in under the focused item. Layout follows a 1920x1080 design scaled to the
// window.
//
// Screens: Home (Play, Settings, Controls, About), Settings (sections on one
// board, the focused section's options on a second), Controls and About.

#pragma once

#include <array>
#include <future>
#include <map>
#include <optional>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <d3d11.h>

#include "audio.h"
#include "settings.h"
#include "textures.h"
#include "updater.h"

struct ImFont;
struct ImDrawList;

namespace launcher {

class GameFiles;

// Menu commands from the keyboard, mouse or a controller.
enum class Command { kNone, kUp, kDown, kLeft, kRight, kAccept, kBack };
// The device used last decides the button hints shown.
enum class InputDevice { kKeyboard, kController };
// The controller's button symbols for the hints.
enum class ControllerStyle { kXbox, kPlayStation };

class Ui {
 public:
  // Loads the art and fonts from the game. False if something essential is missing.
  bool Load(ID3D11Device* device, const GameFiles& files, const std::filesystem::path& data_folder,
            const std::filesystem::path& user_folder);
  // Menu sounds and music (may be silent); the Sound settings apply to it.
  void SetAudio(Audio* audio);
  // Applies the Sound settings (also after the game, which has its own sound).
  void ApplySoundSettings();
  // The next song (N, Y / Triangle, or the board's button), with the button's press.
  void SkipSong();
  void SetControllerStyle(ControllerStyle style) { controller_style_ = style; }

  // Draws one frame for a window of `width` x `height` pixels. `dt` in seconds.
  void Frame(float width, float height, float dt);

  void OnCommand(Command command, InputDevice device);
  // A line at the bottom of the window for a few seconds (e.g. why Play failed).
  void ShowMessage(std::string text);

  // Set by the host: the player chose Play / Quit, or Settings, Data, Game.
  std::function<void()> on_play;
  std::function<void()> on_quit;
  std::function<void()> on_change_game;

  // The update popup (updater.h): shown over every screen once the first
  // start's welcome is closed, and until then it waits. While it's up, it
  // takes all input, so nothing behind it (Play) can be pressed.
  void ShowUpdate(const Release& release);
  // The running update, to show its progress (null: none).
  void SetUpdateJob(const UpdateJob* job) { update_job_ = job; }
  // Set by the host: the player chose Update (and again after a failure).
  std::function<void()> on_update;

 private:
  enum class Screen { kHome, kSettings, kControls, kAbout, kNotice };
  struct Hint {
    const char* key;         // keyboard key
    const char* button;      // controller button ("A", "B", ...)
    const char* label;
    Command command = Command::kNone;  // what clicking the hint does
    bool skip_song = false;            // ... or the next song
  };

  // Drawing helpers (positions in pixels; sizes already scaled).
  void DrawBackground(float width, float height);
  void DrawBoard(float x, float y, float w, float h, bool mirrored);
  // A focusable text item with the brush stroke; returns its bottom edge.
  // `focus` 0..1 animates the stroke; `selected` keeps a faint stroke.
  // `rest` is the label's colour when not focused (cream on the dark boards).
  float DrawItem(float x, float y, const std::string& label, ImFont* font, float size, float focus,
                 float faint = 0.0f, unsigned int rest = 0);
  void DrawSplash(const Texture& texture, float center_x, float center_y, float width, float appear);
  void DrawHints(float width, float height, const std::vector<Hint>& hints);
  // Text wrapped at `wrap` pixels with 1.4x line spacing; returns its height.
  float DrawWrapped(ImFont* font, float size, float x, float y, float wrap, unsigned int color,
                    const std::string& text);
  bool MouseIn(float x0, float y0, float x1, float y1) const;

  // Animated values that ease towards a target, by name.
  float Anim(const std::string& key, float target, float seconds);

  void HomeScreen(float width, float height);
  void SettingsScreen(float width, float height);
  void ControlsScreen(float width, float height);
  // A PlayStation face button for the Xbox one's letter (A Cross, B Circle,
  // X Square, Y Triangle): its symbol in its colour on a dark button.
  void DrawPlayStationFace(float x, float y, float radius, char xbox_letter);
  void NoticeScreen(float width, float height);
  void AboutScreen(float width, float height);
  // A keycap (or, `dark`, a controller's d-pad/Start style key) ending at
  // `right`, centred on `middle`; returns its width.
  float DrawKey(float right, float middle, float size, const char* text, bool dark);
  // A controller button in its console shape and colour, centred at (x, y).
  void DrawGlyph(float x, float y, float size, const ControlRow& row);
  // Shared by Controls and About: a heading board on the left and a wide board
  // on the right. Outputs the wide board's rectangle and where the left list starts.
  void TwoBoards(float width, float height, const char* heading, float list_height, float* x,
                 float* y, float* w, float* h, float* list_y);

  void Go(Screen screen);
  void Sound(Audio::Effect effect);
  GameConfig& ConfigFor(const Option& option) { return option.local ? *launcher_config_ : *config_; }
  void DrawNowPlaying(float x, float bottom, float width);
  void DrawMessage(float width, float height);
  bool UpdateVisible() const;
  void UpdatePopup(float width, float height);
  void UpdateCommand(Command command);
  void HomeCommand(Command command);
  void SettingsCommand(Command command);

  ImDrawList* draw_ = nullptr;
  float scale_ = 1.0f;  // window height / 1080
  float dt_ = 0.0f;
  bool mouse_moved_ = false;
  InputDevice device_ = InputDevice::kKeyboard;
  std::unordered_map<std::string, float> anims_;

  Texture backdrop_;  // behind the fluxwaves (BuildBackdrop in ui.cpp)
  std::array<Texture, 35> covers_;  // EA Trax art by Song::cover (1-34)
  Texture background_, panel_, stroke_, logo_, update_panel_, update_icon_, quit_panel_;
  std::array<Texture, 4> home_icons_;
  std::array<Texture, 5> section_icons_;
  ImFont* bold_ = nullptr;
  ImFont* book_ = nullptr;
  ImFont* condensed_ = nullptr;
  std::vector<std::vector<uint8_t>> font_data_;  // ImGui reads the fonts in place

  Screen screen_ = Screen::kHome;
  int home_focus_ = 0;
  float splash_appear_ = 1.0f;  // 0..1 since the focused item changed

  std::unique_ptr<GameConfig> config_;           // the game's fifawc14.toml
  std::unique_ptr<GameConfig> launcher_config_;  // launcher.toml
  Audio* audio_ = nullptr;
  Song shown_song_;  // the song on the main menu (fades when it changes)
  double shown_song_at_ = 0;  // when it appeared (ImGui time), for scrolling long names
  float skip_pressed_ = 0.0f;  // seconds the skip button still shows pressed
  ControllerStyle controller_style_ = ControllerStyle::kXbox;
  // The Controls page shows what the buttons do when defending (left / right).
  bool controls_defending_ = false;
  // Option infos (folder sizes) are computed in the background, every few seconds.
  struct Info {
    std::string text;
    std::future<std::string> pending;
    float age = 1e9f;
  };
  std::map<const Option*, Info> infos_;
  std::string OptionInfo(const Option& option);
  std::vector<Section> sections_;
  int section_focus_ = 0;
  int option_focus_ = 0;
  bool in_options_ = false;
  int armed_option_ = -1;  // an action waiting for its confirming second press
  int topic_focus_ = 0;
  float screen_appear_ = 1.0f;  // 0..1 since the screen changed (fade and slide in)
  // The update popup.
  std::optional<Release> update_;
  const UpdateJob* update_job_ = nullptr;
  int update_focus_ = 0;      // Update now / Later
  float update_appear_ = 0;   // 0..1, its fade-in
  // B / Esc on Home asks before quitting (QuitPopup).
  bool quit_asked_ = false;
  int quit_focus_ = 0;  // 0 Yes, 1 No
  float quit_appear_ = 0.0f;
  void QuitCommand(Command command);
  void QuitPopup(float width, float height);
  bool blocked_ = false;      // drawing the screen behind the popup: no mouse, no hints
  std::string message_;
  float message_left_ = 0.0f;  // seconds the message still shows
};

}  // namespace launcher
