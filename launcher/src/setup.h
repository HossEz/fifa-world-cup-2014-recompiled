// The first screen for a newcomer: finding their copy of the game. Before
// it's found there's none of the game's art to draw with, so this has its own
// look: the deep blue and yellow of the menu, a plain card, Segoe UI.
//
// The card's main part is a drop box: the player drops the game's .iso or
// folder on the window, or chooses it with one of the two buttons in the box.
// The card then says in plain words what was found; Continue goes on to the
// menu. The language button is at the top right, as in the menu.

#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "game_check.h"
#include "language_picker.h"
#include "ui.h"

struct ImFont;
struct ImDrawList;

namespace launcher {

class GameSource;

class Setup {
 public:
  // Segoe UI from Windows' fonts. False if it isn't there.
  bool Load();
  // Checks a folder, .iso or default.xex the player chose or dropped.
  void Offer(const std::filesystem::path& location);
  // Opened from Settings to change the game: says where it is now, and Back
  // (or Esc / B) keeps it.
  void AllowBack(const std::filesystem::path& current) {
    current_ = current;
    current_.make_preferred();
  }
  // Something is being dragged over the window: the drop box lights up.
  void SetDragging(bool dragging) { dragging_ = dragging; }
  // What the last offer turned out to be.
  const GameCheck& check() const { return check_; }

  void Frame(float width, float height, float dt);
  void OnCommand(Command command);

  // Set by the host: the buttons, Continue with the game that was found, and
  // the language button's choice (to keep it).
  std::function<void()> on_choose_folder;
  std::function<void()> on_choose_iso;
  std::function<void(std::unique_ptr<GameSource>)> on_continue;
  std::function<void()> on_back;
  std::function<void()> on_language;

 private:
  enum class Kind { kPrimary, kSecondary, kLink };
  enum class Icon { kNone, kDisc, kFolder };
  struct Button {
    std::string label;
    std::function<void()> press;
    Kind kind;
    Icon icon = Icon::kNone;
  };
  std::vector<Button> Buttons();
  float Text(ImFont* font, float size, float x, float y, float wrap, unsigned int color,
             const std::string& text);
  // A button's width for its label (links: the label's).
  float ButtonWidth(const Button& button) const;
  // Draws button `index` at (x, y), and notes a click on it.
  void DrawButton(const Button& button, int index, float x, float y);

  ImDrawList* draw_ = nullptr;
  ImFont* regular_ = nullptr;
  ImFont* semibold_ = nullptr;
  float scale_ = 1.0f;
  float alpha_ = 1.0f;  // the card's fade-in, 0..1
  int focus_ = 0;
  bool keyboard_focus_ = false;  // the focus ring shows after a key or button press
  float appear_ = 0.0f;          // 0..1, the card's fade-in (again for each result)
  int pressed_ = -1;             // a button clicked this frame
  bool dragging_ = false;
  float drag_glow_ = 0.0f;       // 0..1, eased towards dragging_
  LanguagePicker language_;

  std::unique_ptr<GameSource> source_;  // what was offered (null: nothing yet / not readable)
  std::filesystem::path location_;
  GameCheck check_;
  bool offered_ = false;
  std::filesystem::path current_;  // set when changing the game (Back is offered)
};

}  // namespace launcher
