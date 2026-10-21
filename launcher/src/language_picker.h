// The language button in the window's top right corner: a globe and the
// language's own name; clicked, the languages drop down under it. Drawn on
// the setup screen and the menu, each in its own font.

#pragma once

#include "i18n.h"

struct ImFont;
struct ImDrawList;

namespace launcher {

class LanguagePicker {
 public:
  // Draws it with its right edge at `right`, top at `top`; `size` is the text
  // size. Returns true when the player chose another language (already
  // SetLanguage'd).
  bool Frame(ImDrawList* draw, ImFont* font, float right, float top, float size,
             bool mouse_allowed);
  // The mouse is over it, or its list is open: the screen behind leaves the
  // mouse alone (from the last frame's layout).
  bool Captures() const;
  void Close() { open_ = false; }

 private:
  bool open_ = false;
  float x0_ = 0, y0_ = 0, x1_ = 0, y1_ = 0;  // the button
  float list_y1_ = 0;                          // the open list's bottom
};

}  // namespace launcher
