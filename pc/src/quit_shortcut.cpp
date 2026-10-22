#include "quit_shortcut.h"

#include <algorithm>
#include <cfloat>
#include <chrono>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

#include <imgui.h>

#include <rex/cvar.h>
#include <rex/input/input.h>
#include <rex/input/input_system.h>
#include <rex/runtime.h>
#include <rex/ui/image_decode.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/immediate_drawer.h>
#include <rex/ui/window.h>
#include <rex/ui/windowed_app_context.h>

#include "../fixes/present_interval.h"

REXCVAR_DEFINE_STRING(quit_buttons, "xbox", "Game",
                      "Button icons of the quit shortcut: xbox or playstation (the launcher sets "
                      "it from the controller in use)");
// The overlay's words, in the launcher's language: the launcher writes them
// translated (launcher/src/main.cpp), so the game has no translations of its
// own. A table of them here got the setup flagged by Microsoft (0.1.6).
REXCVAR_DEFINE_STRING(quit_hold, "Hold", "Game",
                      "Quit shortcut: \"Hold\" [buttons] (the launcher sets it)");
REXCVAR_DEFINE_STRING(quit_to_quit, "to quit", "Game",
                      "Quit shortcut: [buttons] \"to quit\" (the launcher sets it)");
REXCVAR_DEFINE_STRING(quit_quitting, "Quitting", "Game",
                      "Quit shortcut: shown while quitting (the launcher sets it)");
REXCVAR_DEFINE_STRING(quit_create, "Create", "Game",
                      "Quit shortcut: the PlayStation Create button's name (the launcher sets it)");

namespace {

using Clock = std::chrono::steady_clock;

constexpr double kHoldSeconds = 2.0;
constexpr double kHintSeconds = 8.0;
constexpr ImU32 kCream = IM_COL32(244, 239, 200, 255);
constexpr uint16_t kQuitButtons = rex::input::X_INPUT_GAMEPAD_BACK |
                                  rex::input::X_INPUT_GAMEPAD_START;

// The overlay's words (the quit_* settings; the game's own language may
// differ, when the disc doesn't have the launcher's).
struct Words {
  std::string hold;      // "Hold " [buttons]
  std::string to_quit;   // " to quit"
  std::string quitting;
  std::string create;    // the PlayStation Create button's name (Back, Start and Options stay)
};

ImU32 WithAlpha(ImU32 color, float alpha) {
  uint32_t a = uint32_t(float((color >> IM_COL32_A_SHIFT) & 0xFF) * alpha);
  return (color & ~IM_COL32_A_MASK) | (a << IM_COL32_A_SHIFT);
}

class QuitShortcut : public rex::ui::ImGuiDialog {
 public:
  QuitShortcut(rex::ui::ImGuiDrawer* drawer, ImFont* font, std::function<rex::Runtime*()> runtime,
               rex::ui::Window* window, std::unique_ptr<rex::ui::ImmediateTexture> panel)
      : ImGuiDialog(drawer),
        font_(font),
        runtime_(std::move(runtime)),
        window_(window),
        panel_(std::move(panel)),
        playstation_(REXCVAR_GET(quit_buttons) == "playstation"),
        words_{REXCVAR_GET(quit_hold) + " ", " " + REXCVAR_GET(quit_to_quit),
               REXCVAR_GET(quit_quitting), REXCVAR_GET(quit_create)} {}
  // Drawn with each game frame, as the FPS counter (fps_counter.cpp).
  bool WantsContinuousPaint() const override { return false; }

 protected:
  void OnDraw(ImGuiIO& io) override {
    auto now = Clock::now();
    float size = std::max(14.0f, io.DisplaySize.y * 15.0f / 720.0f);
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    ImFont* font = font_ ? font_ : ImGui::GetFont();

    // The hint, from the game's first frame on (not over the black start-up),
    // bottom left.
    if (!hint_start_ && GuestFramesQueued()) hint_start_ = now;
    if (hint_start_) {
      double shown = std::chrono::duration<double>(now - *hint_start_).count();
      if (shown < kHintSeconds) {
        float alpha = float(std::clamp(kHintSeconds - shown, 0.0, 1.0));
        // "Hold [icon] Create + [icon] Options to quit (Alt+F4)".
        float width = Measure(font, size, words_.hold.c_str()) + MeasureButtons(font, size) +
                      Measure(font, size, words_.to_quit.c_str()) + Measure(font, size, " (Alt+F4)");
        ImVec2 pos(size * 0.5f + PanelPadding(width, size).x,
                   io.DisplaySize.y - size * 1.5f - PanelPadding(width, size).y);
        DrawPanel(draw, pos, width, size, alpha);
        float x = DrawText(draw, font, size, ImVec2(pos.x, pos.y), words_.hold.c_str(), alpha);
        x = DrawButtons(draw, font, ImVec2(x, pos.y), size, alpha);
        x = DrawText(draw, font, size, ImVec2(x, pos.y), words_.to_quit.c_str(), alpha);
        DrawText(draw, font, size, ImVec2(x, pos.y), " (Alt+F4)", alpha * 0.75f);
      }
    }

    if (!input_) {
      rex::Runtime* runtime = runtime_();
      input_ = runtime ? dynamic_cast<rex::input::InputSystem*>(runtime->input_system()) : nullptr;
      if (!input_) return;
    }
    uint16_t buttons = 0;
    for (uint32_t user = 0; user < 4; ++user) {
      buttons |= input_->GetLastGuestButtons(user);
    }
    if ((buttons & kQuitButtons) != kQuitButtons) {
      hold_start_.reset();
      return;
    }
    if (!hold_start_) hold_start_ = now;
    double held = std::chrono::duration<double>(now - *hold_start_).count();
    float progress = float(std::min(held / kHoldSeconds, 1.0));

    // In the middle of the screen: "Quitting" and a bar.
    float big = size * 1.4f;
    float width = size * 12.0f;
    ImVec2 pos(io.DisplaySize.x * 0.5f - width * 0.5f, io.DisplaySize.y * 0.5f - size * 2.0f);
    DrawPanel(draw, pos, width, size * 2.6f, 1.0f);
    DrawText(draw, font, big,
             ImVec2(io.DisplaySize.x * 0.5f - Measure(font, big, words_.quitting.c_str()) * 0.5f,
                    pos.y - size * 0.2f),
             words_.quitting.c_str(), 1.0f);
    ImVec2 bar_min(pos.x, pos.y + size * 2.0f);
    ImVec2 bar_max(pos.x + width, bar_min.y + size * 0.4f);
    draw->AddRectFilled(ImVec2(bar_min.x - 2, bar_min.y - 2), ImVec2(bar_max.x + 2, bar_max.y + 2),
                        IM_COL32(0, 0, 0, 160));
    draw->AddRectFilled(bar_min, ImVec2(bar_min.x + width * progress, bar_max.y), kCream);

    if (progress >= 1.0f && !quit_requested_) {
      quit_requested_ = true;
      // Not while the overlay is being drawn.
      rex::ui::Window* window = window_;
      window->app_context().CallInUIThreadDeferred([window] { window->RequestClose(); });
    }
  }

 private:
  static float Measure(ImFont* font, float size, const char* text) {
    return font->CalcTextSizeA(size, FLT_MAX, 0.0f, text).x;
  }

  // Text with a soft shadow, as the frame-rate display. Returns the end x.
  static float DrawText(ImDrawList* draw, ImFont* font, float size, ImVec2 pos, const char* text,
                        float alpha) {
    draw->AddText(font, size, ImVec2(pos.x + 1, pos.y + 1), WithAlpha(IM_COL32(0, 0, 0, 230), alpha),
                  text);
    draw->AddText(font, size, ImVec2(pos.x, pos.y + 2), WithAlpha(IM_COL32(0, 0, 0, 110), alpha),
                  text);
    draw->AddText(font, size, pos, WithAlpha(kCream, alpha), text);
    return pos.x + Measure(font, size, text);
  }

  // The game's brush-stroke panel around content `width` wide and `height`
  // high at `pos`, or a dark box without it.
  // The stroke covers about the middle three quarters of the image's width
  // and half its height, so the content goes there.
  static ImVec2 PanelPadding(float width, float height) {
    return ImVec2(width * 0.2f + height * 0.8f, height * 1.4f);
  }

  void DrawPanel(ImDrawList* draw, ImVec2 pos, float width, float height, float alpha) const {
    ImVec2 pad = PanelPadding(width, height);
    float pad_x = pad.x, pad_y = pad.y;
    ImVec2 min(pos.x - pad_x, pos.y - pad_y);
    ImVec2 max(pos.x + width + pad_x, pos.y + height + pad_y);
    if (panel_) {
      draw->AddImage(reinterpret_cast<ImTextureID>(panel_.get()), min, max, ImVec2(0, 0),
                     ImVec2(1, 1), WithAlpha(IM_COL32_WHITE, alpha));
    } else {
      draw->AddRectFilled(min, max, WithAlpha(IM_COL32(0, 0, 0, 150), alpha), height * 0.3f);
    }
  }

  const char* BackLabel() const { return playstation_ ? words_.create.c_str() : "Back"; }
  const char* StartLabel() const { return playstation_ ? "Options" : "Start"; }

  float MeasureButtons(ImFont* font, float size) const {
    return 2.0f * size * 1.6f + Measure(font, size, BackLabel()) + Measure(font, size, " + ") +
           Measure(font, size, StartLabel()) + size * 0.8f;
  }

  // The two buttons, each as its icon and name: "[icon] Create + [icon] Options".
  // Returns the end x.
  float DrawButtons(ImDrawList* draw, ImFont* font, ImVec2 pos, float size, float alpha) const {
    float x = DrawIcon(draw, ImVec2(pos.x, pos.y), size, true, alpha);
    x = DrawText(draw, font, size, ImVec2(x + size * 0.4f, pos.y), BackLabel(), alpha);
    x = DrawText(draw, font, size, ImVec2(x, pos.y), " + ", alpha);
    x = DrawIcon(draw, ImVec2(x, pos.y), size, false, alpha);
    return DrawText(draw, font, size, ImVec2(x + size * 0.4f, pos.y), StartLabel(), alpha);
  }

  // A small dark pill like the controller's button, with its symbol: Create's
  // three rays and Options' three lines on a PlayStation pad, View's two
  // windows and Menu's three lines on an Xbox one. Returns the end x.
  float DrawIcon(ImDrawList* draw, ImVec2 pos, float size, bool back, float alpha) const {
    float w = size * 1.6f, h = size * 1.05f;
    // Centred on the letters (x-height to cap height), not the line with its
    // descender space.
    ImVec2 min(pos.x, pos.y + size * 0.52f - h * 0.5f);
    ImVec2 max(min.x + w, min.y + h);
    draw->AddRectFilled(ImVec2(min.x, min.y + 2), ImVec2(max.x, max.y + 2),
                        WithAlpha(IM_COL32(0, 0, 0, 90), alpha), h * 0.5f);
    draw->AddRectFilled(min, max, WithAlpha(IM_COL32(43, 47, 51, 255), alpha), h * 0.5f);
    ImVec2 c((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
    ImU32 ink = WithAlpha(IM_COL32(230, 230, 230, 255), alpha);
    float u = h * 0.22f, t = std::max(1.5f, size * 0.09f);
    if (!back) {
      // Options / Menu: three lines.
      for (int i = -1; i <= 1; ++i) {
        draw->AddLine(ImVec2(c.x - u * 1.2f, c.y + i * u * 0.8f),
                      ImVec2(c.x + u * 1.2f, c.y + i * u * 0.8f), ink, t);
      }
    } else if (playstation_) {
      // Create: three rays fanning up.
      ImVec2 base(c.x, c.y + u * 1.1f);
      draw->AddLine(base, ImVec2(c.x, c.y - u * 1.1f), ink, t);
      draw->AddLine(ImVec2(base.x - u * 0.4f, base.y), ImVec2(c.x - u * 1.3f, c.y - u * 0.6f), ink, t);
      draw->AddLine(ImVec2(base.x + u * 0.4f, base.y), ImVec2(c.x + u * 1.3f, c.y - u * 0.6f), ink, t);
    } else {
      // View: two overlapping windows.
      draw->AddRect(ImVec2(c.x - u * 1.2f, c.y - u * 0.9f), ImVec2(c.x + u * 0.4f, c.y + u * 0.5f),
                    ink, 0, 0, t);
      draw->AddRect(ImVec2(c.x - u * 0.4f, c.y - u * 0.3f), ImVec2(c.x + u * 1.2f, c.y + u * 1.1f),
                    ink, 0, 0, t);
    }
    return max.x;
  }

  ImFont* font_;
  std::function<rex::Runtime*()> runtime_;
  rex::input::InputSystem* input_ = nullptr;
  rex::ui::Window* window_;
  std::unique_ptr<rex::ui::ImmediateTexture> panel_;
  bool playstation_;
  Words words_;
  std::optional<Clock::time_point> hint_start_;
  std::optional<Clock::time_point> hold_start_;
  bool quit_requested_ = false;
};

// quit-panel.png, which the launcher writes from the player's game
// (custompanels_42, a brush stroke).
std::unique_ptr<rex::ui::ImmediateTexture> LoadPanel(rex::ui::ImmediateDrawer* drawer,
                                                     const std::filesystem::path& folder) {
  if (!drawer) return nullptr;
  std::ifstream file(folder / "quit-panel.png", std::ios::binary);
  std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)),
                            std::istreambuf_iterator<char>());
  int width = 0, height = 0;
  std::vector<uint8_t> rgba = rex::ui::DecodeImageRGBA(data.data(), data.size(), width, height);
  if (rgba.empty()) return nullptr;
  return drawer->CreateTexture(uint32_t(width), uint32_t(height),
                               rex::ui::ImmediateTextureFilter::kLinear, false, rgba.data());
}

}  // namespace

std::unique_ptr<rex::ui::ImGuiDialog> CreateQuitShortcut(
    rex::ui::ImGuiDrawer* drawer, ImFont* font, std::function<rex::Runtime*()> runtime,
    rex::ui::Window* window, rex::ui::ImmediateDrawer* immediate_drawer,
    const std::filesystem::path& folder) {
  if (!window) return nullptr;
  return std::make_unique<QuitShortcut>(drawer, font, std::move(runtime), window,
                                        LoadPanel(immediate_drawer, folder));
}
