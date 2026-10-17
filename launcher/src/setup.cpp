#include "setup.h"

#include <windows.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>

#include <imgui.h>

#include "game_source.h"
#include "log.h"

namespace launcher {

namespace fs = std::filesystem;

namespace {

// Laid out for a 1100x700 window and scaled with it.
constexpr float kCardWidth = 620, kPad = 40;

constexpr ImU32 kTop = IM_COL32(12, 36, 92, 255), kBottom = IM_COL32(4, 14, 40, 255);
constexpr ImU32 kCard = IM_COL32(17, 38, 86, 255), kCardEdge = IM_COL32(255, 255, 255, 22);
constexpr ImU32 kYellow = IM_COL32(247, 209, 23, 255), kYellowLight = IM_COL32(255, 224, 80, 255);
constexpr ImU32 kNavy = IM_COL32(10, 26, 66, 255);
constexpr ImU32 kWhite = IM_COL32(240, 244, 252, 255), kBody = IM_COL32(190, 203, 230, 255);
constexpr ImU32 kDim = IM_COL32(130, 150, 190, 255);
constexpr ImU32 kGreen = IM_COL32(88, 204, 130, 255), kOrange = IM_COL32(247, 160, 60, 255);

constexpr float kPi = 3.14159265f;

float EaseOut(float t) { return 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t); }

std::string Utf8(const fs::path& path) {
  std::u8string text = path.u8string();
  return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

std::string Hex(uint32_t value) {
  char text[16];
  std::snprintf(text, sizeof(text), "%08X", value);
  return text;
}

}  // namespace

bool Setup::Load() {
  ImGuiIO& io = ImGui::GetIO();
  wchar_t windows[MAX_PATH] = L"C:\\Windows";
  GetWindowsDirectoryW(windows, MAX_PATH);
  fs::path fonts = fs::path(windows) / "Fonts";
  auto add = [&](const char* name) -> ImFont* {
    std::error_code ec;
    if (!fs::exists(fonts / name, ec)) return nullptr;
    return io.Fonts->AddFontFromFileTTF(Utf8(fonts / name).c_str(), 32.0f);
  };
  regular_ = add("segoeui.ttf");
  semibold_ = add("seguisb.ttf");
  if (!semibold_) semibold_ = add("segoeuib.ttf");
  if (!semibold_) semibold_ = regular_;
  return regular_ != nullptr;
}

void Setup::Offer(const fs::path& picked) {
  fs::path location = picked;
  std::error_code ec;
  // A dropped default.xex stands for its folder.
  if (fs::is_regular_file(location, ec) && location.filename() == "default.xex") {
    location = location.parent_path();
  }
  location_ = location;
  source_ = OpenGameSource(location);
  check_ = source_ ? CheckGame(*source_) : GameCheck{};
  offered_ = true;
  focus_ = 0;
  appear_ = 0.0f;
  Log("setup: %s -> result %d, title %08X, version %s, media %08X%s%s", Utf8(location).c_str(),
      int(check_.result), check_.title_id, check_.version.c_str(), check_.media_id,
      check_.file.empty() ? "" : ", ", check_.file.c_str());
}

std::vector<Setup::Button> Setup::Buttons() {
  std::vector<Button> buttons;
  if (offered_ && check_.result == GameCheck::Result::kFound) {
    buttons = {{"Continue", [this] { if (on_continue) on_continue(std::move(source_)); }, true},
               {"Choose another", [this] {
                  offered_ = false;
                  source_.reset();
                  focus_ = 0;
                  appear_ = 0.0f;
                }, false}};
  } else {
    buttons = {{"Choose folder", [this] { if (on_choose_folder) on_choose_folder(); }, true},
               {"Choose .iso", [this] { if (on_choose_iso) on_choose_iso(); }, false}};
  }
  if (!current_.empty()) buttons.push_back({"Back", [this] { if (on_back) on_back(); }, false});
  return buttons;
}

// Wrapped text; returns its height. Measures only while draw_ is null.
float Setup::Text(ImFont* font, float size, float x, float y, float wrap, unsigned int color,
                  const std::string& text) {
  ImVec2 extent = font->CalcTextSizeA(size, FLT_MAX, wrap, text.c_str());
  if (draw_) draw_->AddText(font, size, ImVec2(x, y), color, text.c_str(), nullptr, wrap);
  return extent.y;
}

void Setup::OnCommand(Command command) {
  int count = int(Buttons().size());
  keyboard_focus_ = true;
  switch (command) {
    case Command::kLeft: case Command::kUp: focus_ = std::max(0, focus_ - 1); break;
    case Command::kRight: case Command::kDown: focus_ = std::min(count - 1, focus_ + 1); break;
    case Command::kAccept: {
      auto buttons = Buttons();
      if (focus_ < count) buttons[focus_].press();
      break;
    }
    case Command::kBack:
      if (!current_.empty() && on_back) on_back();
      break;
    default: break;
  }
}

void Setup::Frame(float width, float height, float dt) {
  float s = scale_ = std::min(width / 1100.0f, height / 700.0f);
  appear_ = std::min(1.0f, appear_ + dt / 0.35f);
  float a = EaseOut(appear_);
  auto fade = [&](ImU32 color) {
    ImU32 alpha = ImU32(float(color >> IM_COL32_A_SHIFT) * a);
    return (color & ~IM_COL32_A_MASK) | (alpha << IM_COL32_A_SHIFT);
  };
  ImDrawList* list = ImGui::GetBackgroundDrawList();
  list->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(width, height), kTop, kTop, kBottom, kBottom);
  // A soft light behind the card.
  for (int i = 0; i < 8; ++i) {
    list->AddCircleFilled(ImVec2(width * 0.5f, height * 0.42f), (520 - i * 50) * s,
                          IM_COL32(60, 110, 200, 6), 64);
  }

  // What the card says.
  using Result = GameCheck::Result;
  std::string heading, body, details;
  ImU32 icon = 0;
  if (!offered_ && !current_.empty()) {
    heading = "Change where the game is";
    body = "It's read from " + Utf8(current_) + " now. Choose another folder (with default.xex) "
           "or disc image (.iso); Back keeps this one.";
  } else if (!offered_) {
    heading = "Find your copy of the game";
    body = "This plays your own copy of 2014 FIFA World Cup Brazil for Xbox 360. Choose the "
           "game's folder (the one with default.xex) or its disc image (.iso).";
  } else {
    details = Utf8(location_);
    if (check_.title_id) {
      details += "\nTitle " + Hex(check_.title_id) + "  \xC2\xB7  Version " + check_.version +
                 "  \xC2\xB7  Disc " + std::to_string(check_.disc) + " of " +
                 std::to_string(check_.discs);
    }
    icon = check_.result == Result::kFound ? kGreen : kOrange;
    switch (check_.result) {
      case Result::kFound:
        heading = "2014 FIFA World Cup Brazil detected";
        body = "This is the release the PC version is made from. Continue to the menu; the "
               "game is played from here.";
        break;
      case Result::kNoGame:
        heading = "No Xbox 360 game there";
        body = source_ ? "There's no default.xex in it. Choose the folder that holds the "
                         "game's default.xex, or its .iso."
                       : "That isn't a folder or an Xbox 360 disc image. Choose the game's "
                         "folder (with default.xex) or its .iso.";
        break;
      case Result::kOtherGame:
        heading = "That's a different game";
        body = "It's an Xbox 360 game, but not 2014 FIFA World Cup Brazil (its title ID is " +
               Hex(check_.title_id) + "). Choose your copy of 2014 FIFA World Cup Brazil.";
        break;
      case Result::kOtherVersion:
        heading = "A different version of the game";
        body = "This is 2014 FIFA World Cup Brazil, version " + check_.version +
               " (media ID " + Hex(check_.media_id) + "), but the PC version is made from " +
               kGameVersion + " (media ID 5D31A745) and only runs that one.";
        break;
      case Result::kChanged:
        heading = "The game's files have been changed";
        body = "It's the right version, but " + check_.file +
               " isn't as it is on the disc (patched or modded?), so the PC version can't run "
               "it. Choose an unchanged copy.";
        break;
      case Result::kIncomplete:
        heading = "Some of the game is missing";
        body = "default.xex is right, but " + fs::path(check_.file).filename().string() +
               " isn't there. Copy the whole disc, or choose its .iso.";
        break;
    }
  }
  std::vector<Button> buttons = Buttons();
  bool found = offered_ && check_.result == Result::kFound;
  focus_ = std::min(focus_, int(buttons.size()) - 1);

  // Laid out twice: once to measure the card, then drawn centred.
  float card_w = kCardWidth * s, pad = kPad * s, inner = card_w - 2 * pad;
  float x0 = (width - card_w) * 0.5f;
  float button_h = 44 * s, drop_h = 64 * s;
  const ImGuiIO& io = ImGui::GetIO();
  auto layout = [&](float top) {
    float x = x0 + pad, y = top + pad;
    if (draw_) draw_->AddRectFilled(ImVec2(x, y), ImVec2(x + 28 * s, y + 4 * s), fade(kYellow), 2 * s);
    y += 14 * s;
    y += Text(semibold_, 13 * s, x, y, 0, fade(kYellow), "GAME SETUP") + 10 * s;
    float heading_x = x;
    if (icon) {
      float r = 13 * s, cx = x + r, cy = y + 18 * s;
      if (draw_) {
        draw_->AddCircleFilled(ImVec2(cx, cy), r, fade(icon), 32);
        if (found) {
          ImVec2 tick[] = {{cx - 6 * s, cy + 0.5f * s}, {cx - 1.5f * s, cy + 5 * s},
                           {cx + 6.5f * s, cy - 4.5f * s}};
          draw_->AddPolyline(tick, 3, fade(kNavy), 0, 3 * s);
        } else {
          draw_->AddLine(ImVec2(cx, cy - 7 * s), ImVec2(cx, cy + 2 * s), fade(kNavy), 3 * s);
          draw_->AddCircleFilled(ImVec2(cx, cy + 6.5f * s), 1.8f * s, fade(kNavy), 12);
        }
      }
      heading_x += 2 * r + 12 * s;
    }
    y += Text(semibold_, 26 * s, heading_x, y, inner - (heading_x - x), fade(kWhite), heading) +
         12 * s;
    y += Text(regular_, 15.5f * s, x, y, inner, fade(kBody), body) + 20 * s;
    if (!details.empty()) {
      float box_pad = 14 * s;
      float text_h = regular_->CalcTextSizeA(13.5f * s, FLT_MAX, inner - 2 * box_pad,
                                             details.c_str()).y;
      if (draw_) {
        draw_->AddRectFilled(ImVec2(x, y), ImVec2(x + inner, y + text_h + 2 * box_pad),
                             fade(IM_COL32(255, 255, 255, 12)), 8 * s);
      }
      Text(regular_, 13.5f * s, x + box_pad, y + box_pad, inner - 2 * box_pad, fade(kDim),
           details);
      y += text_h + 2 * box_pad + 22 * s;
    }
    // The buttons.
    float bx = x;
    for (size_t i = 0; i < buttons.size(); ++i) {
      const Button& button = buttons[i];
      float label_w = semibold_->CalcTextSizeA(16 * s, FLT_MAX, 0, button.label.c_str()).x;
      float bw = label_w + 2 * 26 * s;
      ImVec2 p0(bx, y), p1(bx + bw, y + button_h);
      if (draw_) {
        bool hover = ImGui::IsMousePosValid(&io.MousePos) && io.MousePos.x >= p0.x &&
                     io.MousePos.x < p1.x && io.MousePos.y >= p0.y && io.MousePos.y < p1.y;
        if (hover) {
          ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
          if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            focus_ = int(i);
            keyboard_focus_ = false;
            // Pressed after drawing: a press can change what the card shows.
            pressed_ = int(i);
          }
        }
        float r = 8 * s;
        if (button.primary) {
          draw_->AddRectFilled(p0, p1, fade(hover ? kYellowLight : kYellow), r);
        } else {
          draw_->AddRectFilled(p0, p1, fade(IM_COL32(255, 255, 255, hover ? 30 : 14)), r);
          draw_->AddRect(p0, p1, fade(IM_COL32(255, 255, 255, 70)), r, 0, 1.2f * s);
        }
        if (keyboard_focus_ && focus_ == int(i)) {
          float o = 4 * s;
          draw_->AddRect(ImVec2(p0.x - o, p0.y - o), ImVec2(p1.x + o, p1.y + o), fade(kYellow),
                         r + o, 0, 2 * s);
        }
        float ty = y + (button_h - 16 * s * 1.33f) * 0.5f;
        draw_->AddText(semibold_, 16 * s, ImVec2(bx + 26 * s, ty),
                       fade(button.primary ? kNavy : kWhite), button.label.c_str());
      }
      bx += bw + 12 * s;
    }
    y += button_h;
    // Dropping works on the whole window; this says so.
    if (!found) {
      y += 18 * s;
      if (draw_) {
        ImU32 dash = fade(IM_COL32(255, 255, 255, 60));
        float step = 9 * s, len = 5 * s, r = 8 * s;
        for (float dx = r; dx + len <= inner - r; dx += step) {
          draw_->AddLine(ImVec2(x + dx, y), ImVec2(x + dx + len, y), dash, 1.2f * s);
          draw_->AddLine(ImVec2(x + dx, y + drop_h), ImVec2(x + dx + len, y + drop_h), dash,
                         1.2f * s);
        }
        for (float dy = r; dy + len <= drop_h - r; dy += step) {
          draw_->AddLine(ImVec2(x, y + dy), ImVec2(x, y + dy + len), dash, 1.2f * s);
          draw_->AddLine(ImVec2(x + inner, y + dy), ImVec2(x + inner, y + dy + len), dash,
                         1.2f * s);
        }
        const float corners[4][2] = {{r, r}, {inner - r, r}, {inner - r, drop_h - r},
                                     {r, drop_h - r}};
        for (int c = 0; c < 4; ++c) {
          float a0 = kPi + c * kPi * 0.5f;
          draw_->PathArcTo(ImVec2(x + corners[c][0], y + corners[c][1]), r, a0,
                           a0 + kPi * 0.5f, 8);
          draw_->PathStroke(dash, 0, 1.2f * s);
        }
        const char* hint = "Or drop the game's folder or .iso on this window";
        ImVec2 size = regular_->CalcTextSizeA(14 * s, FLT_MAX, 0, hint);
        draw_->AddText(regular_, 14 * s,
                       ImVec2(x + (inner - size.x) * 0.5f, y + (drop_h - size.y) * 0.5f),
                       fade(kDim), hint);
      }
      y += drop_h;
    }
    return y + pad - top;
  };

  draw_ = nullptr;
  float card_h = layout(0);
  const char* notice =
      "Ported by hosse (github.com/HossEz/fifa-world-cup-2014-recompiled). Unofficial, and not affiliated with or endorsed by EA, FIFA "
      "or Microsoft. None of the game is included: it plays your own copy.";
  float notice_h = regular_->CalcTextSizeA(12.5f * s, FLT_MAX, card_w, notice).y;
  float top = std::max(16 * s, (height - card_h - notice_h - 24 * s) * 0.5f) + (1 - a) * 12 * s;
  draw_ = list;
  draw_->AddRectFilled(ImVec2(x0, top + 6 * s), ImVec2(x0 + card_w, top + card_h + 10 * s),
                       fade(IM_COL32(0, 0, 0, 70)), 16 * s);  // shadow
  draw_->AddRectFilled(ImVec2(x0, top), ImVec2(x0 + card_w, top + card_h), fade(kCard), 14 * s);
  draw_->AddRect(ImVec2(x0, top), ImVec2(x0 + card_w, top + card_h), fade(kCardEdge), 14 * s, 0,
                 1.0f * s);

  pressed_ = -1;
  layout(top);
  // The notice under the card, centred line by line.
  float ny = top + card_h + 24 * s;
  ImVec2 one = regular_->CalcTextSizeA(12.5f * s, FLT_MAX, card_w, notice);
  Text(regular_, 12.5f * s, x0 + std::max(0.0f, (card_w - one.x) * 0.5f), ny, card_w,
       fade(kDim), notice);
  draw_ = nullptr;
  if (pressed_ >= 0 && pressed_ < int(buttons.size())) buttons[pressed_].press();
}

}  // namespace launcher
