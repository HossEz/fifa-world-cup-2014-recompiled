#include "language_picker.h"

#include <algorithm>
#include <cfloat>

#include <imgui.h>

namespace launcher {

namespace {
constexpr ImU32 kPanel = IM_COL32(8, 20, 50, 215), kEdge = IM_COL32(255, 255, 255, 40);
constexpr ImU32 kText = IM_COL32(244, 239, 200, 255), kYellow = IM_COL32(247, 209, 23, 255);
constexpr ImU32 kRow = IM_COL32(255, 255, 255, 22);

bool Inside(ImVec2 p, float x0, float y0, float x1, float y1) {
  return p.x >= x0 && p.x < x1 && p.y >= y0 && p.y < y1;
}

// A globe: its outline, the equator and one meridian.
void Globe(ImDrawList* draw, ImVec2 c, float r, ImU32 color, float thickness) {
  draw->AddCircle(c, r, color, 24, thickness);
  draw->AddEllipse(c, ImVec2(r * 0.45f, r), color, 0.0f, 20, thickness);
  draw->AddLine(ImVec2(c.x - r, c.y), ImVec2(c.x + r, c.y), color, thickness);
}
}  // namespace

bool LanguagePicker::Captures() const {
  if (open_) return true;
  ImVec2 mouse = ImGui::GetIO().MousePos;
  return Inside(mouse, x0_, y0_, x1_, y1_);
}

bool LanguagePicker::Frame(ImDrawList* draw, ImFont* font, float right, float top, float size,
                           bool mouse_allowed) {
  const ImGuiIO& io = ImGui::GetIO();
  ImVec2 mouse = io.MousePos;
  bool click = mouse_allowed && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
  float pad = size * 0.7f, r = size * 0.45f, h = size * 1.9f;
  const char* name = Info(CurrentLanguage()).name;

  // The list is as wide as its longest name.
  float names_w = 0;
  for (const LanguageInfo& info : Languages()) {
    names_w = std::max(names_w, font->CalcTextSizeA(size, FLT_MAX, 0, info.name).x);
  }
  float chevron = size * 0.35f;
  float w = pad + 2 * r + size * 0.5f + names_w + size * 0.6f + chevron * 2 + pad;
  x0_ = right - w, y0_ = top, x1_ = right, y1_ = top + h;
  bool hover = mouse_allowed && Inside(mouse, x0_, y0_, x1_, y1_);
  ImU32 ink = hover || open_ ? kYellow : kText;

  draw->AddRectFilled(ImVec2(x0_, y0_), ImVec2(x1_, y1_), kPanel, h * 0.5f);
  draw->AddRect(ImVec2(x0_, y0_), ImVec2(x1_, y1_), kEdge, h * 0.5f, 0, 1.0f);
  float mid = top + h * 0.5f;
  Globe(draw, ImVec2(x0_ + pad + r, mid), r, ink, std::max(1.0f, size * 0.08f));
  float text_x = x0_ + pad + 2 * r + size * 0.5f;
  draw->AddText(font, size, ImVec2(text_x, mid - size * 0.62f), ink, name);
  // A small chevron pointing down (up while open).
  float cx = x1_ - pad - chevron, d = open_ ? -1.0f : 1.0f;
  draw->AddTriangleFilled(ImVec2(cx - chevron, mid - d * chevron * 0.5f),
                          ImVec2(cx + chevron, mid - d * chevron * 0.5f),
                          ImVec2(cx, mid + d * chevron * 0.6f), ink);
  if (hover) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

  bool changed = false;
  if (open_) {
    float row_h = size * 1.8f, ly = y1_ + size * 0.3f;
    float ly1 = ly + row_h * float(Languages().size()) + size * 0.5f;
    list_y1_ = ly1;
    draw->AddRectFilled(ImVec2(x0_, ly), ImVec2(x1_, ly1), kPanel, size * 0.6f);
    draw->AddRect(ImVec2(x0_, ly), ImVec2(x1_, ly1), kEdge, size * 0.6f, 0, 1.0f);
    float y = ly + size * 0.25f;
    for (const LanguageInfo& info : Languages()) {
      bool current = info.id == CurrentLanguage();
      bool row_hover = mouse_allowed && Inside(mouse, x0_, y, x1_, y + row_h);
      if (row_hover) {
        draw->AddRectFilled(ImVec2(x0_ + size * 0.25f, y), ImVec2(x1_ - size * 0.25f, y + row_h),
                            kRow, size * 0.4f);
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
      }
      draw->AddText(font, size, ImVec2(text_x, y + (row_h - size * 1.2f) * 0.5f),
                    current || row_hover ? kYellow : kText, info.name);
      if (current) {
        // A tick before the current one.
        float tx = x0_ + pad + r, ty = y + row_h * 0.5f, u = size * 0.22f;
        ImVec2 tick[] = {{tx - u * 1.4f, ty}, {tx - u * 0.3f, ty + u}, {tx + u * 1.5f, ty - u * 1.1f}};
        draw->AddPolyline(tick, 3, kYellow, 0, std::max(1.5f, size * 0.12f));
      }
      if (row_hover && click && !current) {
        SetLanguage(info.id);
        changed = true;
      }
      y += row_h;
    }
    // A click anywhere closes it (on the button too).
    if (click) open_ = false;
  } else if (hover && click) {
    open_ = true;
  }
  return changed;
}

}  // namespace launcher
