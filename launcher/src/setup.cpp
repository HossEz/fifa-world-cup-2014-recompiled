#include "setup.h"

#include <windows.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>

#include <imgui.h>

#include "game_source.h"
#include "i18n.h"
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

ImU32 Mix(ImU32 a, ImU32 b, float t) {
  ImVec4 ca = ImGui::ColorConvertU32ToFloat4(a), cb = ImGui::ColorConvertU32ToFloat4(b);
  return ImGui::ColorConvertFloat4ToU32(ImVec4(ca.x + (cb.x - ca.x) * t, ca.y + (cb.y - ca.y) * t,
                                               ca.z + (cb.z - ca.z) * t, ca.w + (cb.w - ca.w) * t));
}

// A disc: its edge and the hole in the middle.
void Disc(ImDrawList* draw, ImVec2 c, float r, ImU32 color, float thickness) {
  draw->AddCircle(c, r, color, 32, thickness);
  draw->AddCircle(c, r * 0.32f, color, 20, thickness);
}

// A folder: its body and the tab on its top left.
void Folder(ImDrawList* draw, ImVec2 c, float r, ImU32 color, float thickness) {
  float w = r * 2.1f, h = r * 1.55f, x0 = c.x - w * 0.5f, y0 = c.y - h * 0.5f + r * 0.15f;
  ImVec2 outline[] = {{x0, y0 - r * 0.3f},          {x0 + w * 0.38f, y0 - r * 0.3f},
                      {x0 + w * 0.48f, y0},           {x0 + w, y0},
                      {x0 + w, y0 + h},               {x0, y0 + h}};
  draw->AddPolyline(outline, 6, color, ImDrawFlags_Closed, thickness);
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
    buttons = {{T("Continue"), [this] { if (on_continue) on_continue(std::move(source_)); },
                Kind::kPrimary},
               {T("Use a different copy"), [this] {
                  offered_ = false;
                  source_.reset();
                  focus_ = 0;
                  appear_ = 0.0f;
                }, Kind::kLink}};
  } else {
    buttons = {{T("Choose .iso file"), [this] { if (on_choose_iso) on_choose_iso(); },
                Kind::kSecondary, Icon::kDisc},
               {T("Choose folder"), [this] { if (on_choose_folder) on_choose_folder(); },
                Kind::kSecondary, Icon::kFolder}};
  }
  if (!current_.empty()) {
    buttons.push_back({T("Back"), [this] { if (on_back) on_back(); }, Kind::kLink});
  }
  return buttons;
}

// Wrapped text; returns its height. Measures only while draw_ is null.
float Setup::Text(ImFont* font, float size, float x, float y, float wrap, unsigned int color,
                  const std::string& text) {
  ImVec2 extent = font->CalcTextSizeA(size, FLT_MAX, wrap, text.c_str());
  if (draw_) {
    ImU32 alpha = ImU32(float(color >> IM_COL32_A_SHIFT) * alpha_);
    color = (color & ~IM_COL32_A_MASK) | (alpha << IM_COL32_A_SHIFT);
    draw_->AddText(font, size, ImVec2(x, y), color, text.c_str(), nullptr, wrap);
  }
  return extent.y;
}

void Setup::OnCommand(Command command) {
  int count = int(Buttons().size());
  keyboard_focus_ = true;
  language_.Close();
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

float Setup::ButtonWidth(const Button& button) const {
  float s = scale_;
  if (button.kind == Kind::kLink) {
    return semibold_->CalcTextSizeA(15 * s, FLT_MAX, 0, button.label.c_str()).x;
  }
  float label = semibold_->CalcTextSizeA(16 * s, FLT_MAX, 0, button.label.c_str()).x;
  return label + 2 * 24 * s + (button.icon != Icon::kNone ? 28 * s : 0);
}

void Setup::DrawButton(const Button& button, int index, float x, float y) {
  if (!draw_) return;
  float s = scale_, a = alpha_;
  auto fade = [a](ImU32 color) {
    ImU32 alpha = ImU32(float(color >> IM_COL32_A_SHIFT) * a);
    return (color & ~IM_COL32_A_MASK) | (alpha << IM_COL32_A_SHIFT);
  };
  const ImGuiIO& io = ImGui::GetIO();
  bool link = button.kind == Kind::kLink;
  float w = ButtonWidth(button), h = link ? 15 * s * 1.4f : 46 * s;
  ImVec2 p0(x, y), p1(x + w, y + h);
  bool hover = !language_.Captures() && ImGui::IsMousePosValid(&io.MousePos) &&
               io.MousePos.x >= p0.x && io.MousePos.x < p1.x && io.MousePos.y >= p0.y &&
               io.MousePos.y < p1.y;
  if (hover) {
    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
      focus_ = index;
      keyboard_focus_ = false;
      // Pressed after drawing: a press can change what the card shows.
      pressed_ = index;
    }
  }
  bool focused = keyboard_focus_ && focus_ == index;
  float r = 8 * s;
  if (link) {
    ImU32 color = fade(hover || focused ? kWhite : kBody);
    draw_->AddText(semibold_, 15 * s, p0, color, button.label.c_str());
    if (hover || focused) {
      draw_->AddLine(ImVec2(p0.x, p1.y - 1 * s), ImVec2(p1.x, p1.y - 1 * s), color, 1.2f * s);
    }
    return;
  }
  if (button.kind == Kind::kPrimary) {
    draw_->AddRectFilled(p0, p1, fade(hover ? kYellowLight : kYellow), r);
  } else {
    draw_->AddRectFilled(p0, p1, fade(IM_COL32(255, 255, 255, hover ? 34 : 16)), r);
    draw_->AddRect(p0, p1, fade(IM_COL32(255, 255, 255, hover ? 120 : 70)), r, 0, 1.2f * s);
  }
  if (focused) {
    float o = 4 * s;
    draw_->AddRect(ImVec2(p0.x - o, p0.y - o), ImVec2(p1.x + o, p1.y + o), fade(kYellow), r + o, 0,
                   2 * s);
  }
  ImU32 ink = fade(button.kind == Kind::kPrimary ? kNavy : kWhite);
  float tx = x + 24 * s, mid = y + h * 0.5f;
  if (button.icon != Icon::kNone) {
    ImVec2 c(tx + 9 * s, mid);
    if (button.icon == Icon::kDisc) {
      Disc(draw_, c, 9 * s, ink, 1.6f * s);
    } else {
      Folder(draw_, c, 9 * s, ink, 1.6f * s);
    }
    tx += 28 * s;
  }
  draw_->AddText(semibold_, 16 * s, ImVec2(tx, mid - 16 * s * 0.68f), ink, button.label.c_str());
}

void Setup::Frame(float width, float height, float dt) {
  float s = scale_ = std::min(width / 1100.0f, height / 700.0f);
  appear_ = std::min(1.0f, appear_ + dt / 0.35f);
  drag_glow_ = std::clamp(drag_glow_ + (dragging_ ? dt : -dt) / 0.15f, 0.0f, 1.0f);
  float a = alpha_ = EaseOut(appear_);
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
  bool found = offered_ && check_.result == Result::kFound;
  std::string label = found ? T("GAME FOUND") : T("ONE LAST STEP");
  std::string heading, body;
  ImU32 icon = 0;
  if (!offered_ && !current_.empty()) {
    heading = T("Change where the game is");
    body = Tf("It's read from {0} now.", {Utf8(current_)});
  } else if (!offered_) {
    heading = T("Show us where your game is");
    body = T("The game isn't included. Use your own copy of 2014 FIFA World Cup Brazil for "
             "Xbox 360.");
  } else {
    icon = found ? kGreen : kOrange;
    switch (check_.result) {
      case Result::kFound:
        heading = T("Ready to play");
        break;
      case Result::kNoGame:
        heading = source_ ? T("No game in that folder") : T("That's not a game folder or .iso");
        body = source_ ? T("Choose the folder that has default.xex in it, or the game's .iso.")
                       : T("Choose the game's .iso, or its folder (the one with default.xex).");
        break;
      case Result::kOtherGame:
        heading = T("That's a different game");
        body = T("It's an Xbox 360 game, but not 2014 FIFA World Cup Brazil. Choose your copy of "
                 "2014 FIFA World Cup Brazil.");
        break;
      case Result::kOtherCode:
        heading = T("This copy can't be used");
        body = T("It's 2014 FIFA World Cup Brazil, but another version, or patched or modded. "
                 "Use the European or US disc.");
        break;
      case Result::kIncomplete:
        heading = T("Part of the game is missing");
        body = Tf("{0} isn't there. Copy the whole disc, or use its .iso.",
                  {fs::path(check_.file).filename().string()});
        break;
    }
  }
  // The game that was found, in plain words: which disc, and the file.
  std::string found_line;
  if (found) {
    if (check_.region == "Europe") found_line = T("European disc");
    if (check_.region == "USA") found_line = T("US disc");
    std::string name = Utf8(location_.filename().empty() ? location_ : location_.filename());
    found_line += (found_line.empty() ? "" : "  \xC2\xB7  ") + name;
  }
  std::vector<Button> buttons = Buttons();
  focus_ = std::min(focus_, int(buttons.size()) - 1);

  // Laid out twice: once to measure the card, then drawn centred.
  float card_w = kCardWidth * s, pad = kPad * s, inner = card_w - 2 * pad;
  float x0 = (width - card_w) * 0.5f;
  auto layout = [&](float top) {
    float x = x0 + pad, y = top + pad;
    if (draw_) draw_->AddRectFilled(ImVec2(x, y), ImVec2(x + 28 * s, y + 4 * s), fade(kYellow), 2 * s);
    y += 14 * s;
    y += Text(semibold_, 13 * s, x, y, 0, kYellow, label) + 10 * s;
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
    y += Text(semibold_, 26 * s, heading_x, y, inner - (heading_x - x), kWhite, heading) + 12 * s;
    if (!body.empty()) y += Text(regular_, 15.5f * s, x, y, inner, kBody, body) + 22 * s;

    if (found) {
      // The game, in a box.
      float box_pad = 16 * s;
      float name_h = semibold_->CalcTextSizeA(17 * s, FLT_MAX, 0, "W").y;
      float line_h = regular_->CalcTextSizeA(13.5f * s, FLT_MAX, inner - 2 * box_pad,
                                             found_line.c_str()).y;
      float box_h = box_pad * 2 + name_h + 2 * s + line_h;
      if (draw_) {
        draw_->AddRectFilled(ImVec2(x, y), ImVec2(x + inner, y + box_h),
                             fade(IM_COL32(255, 255, 255, 12)), 10 * s);
      }
      Text(semibold_, 17 * s, x + box_pad, y + box_pad, 0, kWhite, "2014 FIFA World Cup Brazil");
      Text(regular_, 13.5f * s, x + box_pad, y + box_pad + name_h + 2 * s, inner - 2 * box_pad,
           kDim, found_line);
      y += box_h + 24 * s;
      // Continue, and beside it the link to choose again.
      float bh = 46 * s;
      DrawButton(buttons[0], 0, x, y);
      float link_x = x + ButtonWidth(buttons[0]) + 24 * s;
      DrawButton(buttons[1], 1, link_x, y + (bh - 15 * s * 1.4f) * 0.5f);
      if (buttons.size() > 2) {
        DrawButton(buttons[2], 2, link_x + ButtonWidth(buttons[1]) + 24 * s,
                   y + (bh - 15 * s * 1.4f) * 0.5f);
      }
      y += bh;
    } else {
      // The drop box: the whole window takes a drop, the box says so. While
      // something is dragged over the window it lights up.
      float box_pad = 26 * s, icon_r = 20 * s, bh = 46 * s;
      float hint_size = 17 * s, or_size = 13.5f * s;
      float box_h = box_pad + 2 * icon_r + 14 * s + hint_size * 1.35f + 6 * s + or_size * 1.35f +
                    10 * s + bh + box_pad;
      if (draw_) {
        float g = drag_glow_;
        ImU32 edge = fade(Mix(IM_COL32(255, 255, 255, 70), kYellow, g));
        float r = 12 * s, thick = (1.3f + g * 0.9f) * s;
        draw_->AddRectFilled(ImVec2(x, y), ImVec2(x + inner, y + box_h),
                             fade(Mix(IM_COL32(255, 255, 255, 6), IM_COL32(247, 209, 23, 26), g)), r);
        // Dashed edge.
        float step = 10 * s, len = 6 * s;
        for (float dx = r; dx + len <= inner - r; dx += step) {
          draw_->AddLine(ImVec2(x + dx, y), ImVec2(x + dx + len, y), edge, thick);
          draw_->AddLine(ImVec2(x + dx, y + box_h), ImVec2(x + dx + len, y + box_h), edge, thick);
        }
        for (float dy = r; dy + len <= box_h - r; dy += step) {
          draw_->AddLine(ImVec2(x, y + dy), ImVec2(x, y + dy + len), edge, thick);
          draw_->AddLine(ImVec2(x + inner, y + dy), ImVec2(x + inner, y + dy + len), edge, thick);
        }
        const float corners[4][2] = {{r, r}, {inner - r, r}, {inner - r, box_h - r}, {r, box_h - r}};
        for (int c = 0; c < 4; ++c) {
          float a0 = kPi + c * kPi * 0.5f;
          draw_->PathArcTo(ImVec2(x + corners[c][0], y + corners[c][1]), r, a0, a0 + kPi * 0.5f, 8);
          draw_->PathStroke(edge, 0, thick);
        }
        // The disc over the hint, rising a little while dragging.
        ImVec2 c(x + inner * 0.5f, y + box_pad + icon_r - g * 4 * s);
        Disc(draw_, c, icon_r, fade(Mix(kBody, kYellow, g)), 2.2f * s);
      }
      float cy = y + box_pad + 2 * icon_r + 14 * s;
      std::string hint = dragging_ ? T("Drop it here") : T("Drop your .iso or game folder here");
      float hint_w = semibold_->CalcTextSizeA(hint_size, FLT_MAX, 0, hint.c_str()).x;
      Text(semibold_, hint_size, x + (inner - hint_w) * 0.5f, cy, 0, dragging_ ? kYellow : kWhite,
           hint);
      cy += hint_size * 1.35f + 6 * s;
      const char* word = T("or");
      float or_w = regular_->CalcTextSizeA(or_size, FLT_MAX, 0, word).x;
      float or_x = x + (inner - or_w) * 0.5f;
      Text(regular_, or_size, or_x, cy, 0, kDim, word);
      // Short rules either side of it ("o" alone could pass for a ring).
      if (draw_) {
        float line_y = cy + or_size * 0.72f, len = 36 * s, gap = 10 * s;
        ImU32 rule = fade(IM_COL32(255, 255, 255, 50));
        draw_->AddLine(ImVec2(or_x - gap - len, line_y), ImVec2(or_x - gap, line_y), rule, 1.0f * s);
        draw_->AddLine(ImVec2(or_x + or_w + gap, line_y), ImVec2(or_x + or_w + gap + len, line_y),
                       rule, 1.0f * s);
      }
      cy += or_size * 1.35f + 10 * s;
      // The two buttons, alike, side by side in the middle.
      float w0 = ButtonWidth(buttons[0]), w1 = ButtonWidth(buttons[1]), gap = 12 * s;
      float bx = x + (inner - w0 - gap - w1) * 0.5f;
      DrawButton(buttons[0], 0, bx, cy);
      DrawButton(buttons[1], 1, bx + w0 + gap, cy);
      y += box_h;
      int index = 2;  // Back, when changing the game
      if (int(buttons.size()) > index) {
        y += 18 * s;
        DrawButton(buttons[index], index, x, y);
        y += 15 * s * 1.4f;
      }
    }
    return y + pad - top;
  };

  draw_ = nullptr;
  float card_h = layout(0);
  std::string notice = Tf("Ported by hosse ({0}). Unofficial, and not affiliated with or endorsed "
                          "by EA, FIFA or Microsoft.",
                          {"github.com/HossEz/fifa-world-cup-2014-recompiled"});
  float notice_h = regular_->CalcTextSizeA(12.5f * s, FLT_MAX, card_w, notice.c_str()).y;
  float top = std::max(56 * s, (height - card_h - notice_h - 24 * s) * 0.5f) + (1 - a) * 12 * s;
  draw_ = list;
  draw_->AddRectFilled(ImVec2(x0, top + 6 * s), ImVec2(x0 + card_w, top + card_h + 10 * s),
                       fade(IM_COL32(0, 0, 0, 70)), 16 * s);  // shadow
  draw_->AddRectFilled(ImVec2(x0, top), ImVec2(x0 + card_w, top + card_h), fade(kCard), 14 * s);
  draw_->AddRect(ImVec2(x0, top), ImVec2(x0 + card_w, top + card_h), fade(kCardEdge), 14 * s, 0,
                 1.0f * s);

  pressed_ = -1;
  layout(top);
  // The notice under the card, centred.
  float ny = top + card_h + 24 * s;
  ImVec2 one = regular_->CalcTextSizeA(12.5f * s, FLT_MAX, card_w, notice.c_str());
  Text(regular_, 12.5f * s, x0 + std::max(0.0f, (card_w - one.x) * 0.5f), ny, card_w, kDim, notice);
  draw_ = nullptr;

  // The language, top right, over everything.
  if (language_.Frame(ImGui::GetForegroundDrawList(), semibold_, width - 24 * s, 18 * s, 15 * s,
                      true)) {
    if (on_language) on_language();
  }
  if (pressed_ >= 0 && pressed_ < int(buttons.size())) buttons[pressed_].press();
}

}  // namespace launcher
