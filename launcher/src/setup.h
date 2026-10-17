// The first screen for a newcomer: finding their copy of the game. Before
// it's found there's none of the game's art to draw with, so this has its own
// look: the deep blue and yellow of the menu, a plain card, Segoe UI.
//
// The player chooses the game's folder (the one with default.xex) or its disc
// image (.iso), with the buttons or by dropping it on the window; the card
// says what was found, and Continue goes on to the menu.

#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "game_check.h"
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

  void Frame(float width, float height, float dt);
  void OnCommand(Command command);

  // Set by the host: the buttons, and Continue with the game that was found.
  std::function<void()> on_choose_folder;
  std::function<void()> on_choose_iso;
  std::function<void(std::unique_ptr<GameSource>)> on_continue;
  std::function<void()> on_back;

 private:
  struct Button {
    std::string label;
    std::function<void()> press;
    bool primary;
  };
  std::vector<Button> Buttons();
  float Text(ImFont* font, float size, float x, float y, float wrap, unsigned int color,
             const std::string& text);

  ImDrawList* draw_ = nullptr;
  ImFont* regular_ = nullptr;
  ImFont* semibold_ = nullptr;
  float scale_ = 1.0f;
  int focus_ = 0;
  bool keyboard_focus_ = false;  // the focus ring shows after a key or button press
  float appear_ = 0.0f;          // 0..1, the card's fade-in (again for each result)
  int pressed_ = -1;             // a button clicked this frame

  std::unique_ptr<GameSource> source_;  // what was offered (null: nothing yet / not readable)
  std::filesystem::path location_;
  GameCheck check_;
  bool offered_ = false;
  std::filesystem::path current_;  // set when changing the game (Back is offered)
};

}  // namespace launcher
