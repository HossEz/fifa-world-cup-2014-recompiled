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
#include "language_picker.h"
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
  // The controller in use; the Button icons setting decides the hints unless it's Automatic.
  void SetControllerStyle(ControllerStyle detected);
  // Held scrolling, -1 (up) to 1 (down): the right stick or Page Up / Page Down.
  void SetScrollInput(float amount) { scroll_input_ = amount; }

  // Draws one frame for a window of `width` x `height` pixels. `dt` in seconds.
  void Frame(float width, float height, float dt);

  void OnCommand(Command command, InputDevice device);
  // A key press (Windows virtual key). True if it was taken: the Controls page
  // was waiting for a key to bind. `repeat`: held down (taken, not bound).
  bool OnKey(unsigned virtual_key, bool extended, bool repeat);
  // A line at the bottom of the window for a few seconds (e.g. why Play failed).
  // Bottom left for a few seconds; `ok` gives it a green stripe instead of red.
  void ShowMessage(std::string text, bool ok = false);

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
  // Opens a page in the browser (the About page's GitHub link).
  std::function<void(const std::string&)> on_open_page;

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
  // An Xbox face button: its letter centred on a button in its colour.
  void DrawXboxFace(float x, float y, float radius, char letter);
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
  // The settings in the current language (again when it changes).
  void BuildSections();
  // After a launcher setting changed (Settings): the sound and the language.
  void ApplyLocalSettings();
  // After the language button changed it: into launcher.toml, and the settings again.
  void SaveLanguage();

  ImDrawList* draw_ = nullptr;
  float scale_ = 1.0f;  // window height / 1080
  float dt_ = 0.0f;
  bool mouse_moved_ = false;
  bool hint_clicked_ = false;  // a hint was clicked this frame
  InputDevice device_ = InputDevice::kKeyboard;
  std::unordered_map<std::string, float> anims_;

  Texture backdrop_;  // behind the fluxwaves (BuildBackdrop in ui.cpp)
  std::array<Texture, 35> covers_;  // EA Trax art by Song::cover (1-34)
  Texture background_, panel_, stroke_, logo_, update_panel_, update_icon_, quit_panel_, note_;
  std::array<Texture, 4> home_icons_;
  std::array<Texture, 5> section_icons_;
  ImFont* bold_ = nullptr;
  ImFont* book_ = nullptr;
  ImFont* condensed_ = nullptr;
  std::vector<std::vector<uint8_t>> font_data_;  // ImGui reads the fonts in place

  Screen screen_ = Screen::kHome;
  float language_alpha_ = 1.0f;  // the language picker's, dimmed off the home screen
  int home_focus_ = 0;
  float splash_appear_ = 1.0f;  // 0..1 since the focused item changed

  std::unique_ptr<GameConfig> config_;           // the game's fifawc14.toml
  std::unique_ptr<GameConfig> launcher_config_;  // launcher.toml
  std::filesystem::path data_folder_, user_folder_;  // for MakeSections
  LanguagePicker language_;  // top right
  Audio* audio_ = nullptr;
  Song shown_song_;  // the song on the main menu (fades when it changes)
  double shown_song_at_ = 0;  // when it appeared (ImGui time), for scrolling long names
  float skip_pressed_ = 0.0f;  // seconds the skip button still shows pressed
  ControllerStyle controller_style_ = ControllerStyle::kXbox;
  // The Controls page: the focused row (one per action, then Reset to
  // default), the focused key column (0 the key, 1 the second key), whether
  // the next key press goes there, and the list's scroll (in rows).
  int controls_row_ = 0;
  int controls_column_ = 0;
  bool binding_ = false;

  // A list that scrolls when its rows don't fit: the mouse wheel, gliding
  // when the mouse is near its top or bottom edge, a focused row moved by the
  // keyboard or a controller kept in view, and a scroll bar to drag or click.
  struct ScrollList {
    float scroll = 0.0f;        // in rows
    float drag = -1.0f;         // dragging the bar: where on its thumb (-1: not)
    bool mouse_focus = false;   // the focus came from the mouse: no jump into view
    int last_focus = -1, owner = -1;
    float top = 0, bottom = 0, row_h = 1, max_scroll = 0, shown = 0;
    float bar_x = -1.0f;        // where End drew the bar (-1 before the first frame)
  };
  // Between Begin and End, row i is at the returned y + i * row_h (drawing is
  // clipped to the list). `owner`: what the list shows (a new one starts at
  // the top). `active`: false while something else takes the input.
  float BeginScrollList(ScrollList& list, const char* key, int owner, float x0, float x1, float top,
                        float bottom, float row_h, int rows, int focus, bool active);
  void EndScrollList(ScrollList& list, float bar_x, bool active, uint32_t color = 0);  // 0: cream
  ScrollList controls_list_, settings_list_, notes_list_;
  bool on_scroll_bar_ = false;
  float scroll_input_ = 0.0f;  // the mouse is on a list's bar, so its rows don't take it
  // An action's keys, from fifawc14.toml (the default if it has none).
  std::vector<std::string> KeysOf(const std::string& setting) const;
  // Binds `name` to the focused key; "" clears a second key.
  void BindKey(const std::string& name);
  void ControlsCommand(Command command);
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
  float message_shown_ = 0.0f;  // seconds since it came up
  bool message_ok_ = false;
  bool message_stays_ = false;  // a problem: shown until the next input
  void DismissMessage();
};

}  // namespace launcher
