#include "fps_counter.h"

#include <algorithm>
#include <chrono>
#include <cstdio>

#include <imgui.h>

#include <rex/cvar.h>
#include <rex/ui/imgui_dialog.h>

#include "../fixes/present_interval.h"

REXCVAR_DEFINE_BOOL(show_fps, false, "Game",
                    "Frames per second in the top left corner of the game (the launcher's Show "
                    "FPS)");

namespace {

// The launcher copies it into the data folder from the player's game.
constexpr const char* kFontFile = "futurastd-condensed.ttf";
constexpr ImU32 kCream = IM_COL32(244, 239, 200, 255);

class FpsCounter : public rex::ui::ImGuiDialog {
 public:
  FpsCounter(rex::ui::ImGuiDrawer* drawer, ImFont* font) : ImGuiDialog(drawer), font_(font) {}

 protected:
  void OnDraw(ImGuiIO& io) override {
    auto now = std::chrono::steady_clock::now();
    uint64_t frames = GuestFramesQueued();
    double seconds = std::chrono::duration<double>(now - window_start_).count();
    if (seconds >= 0.5) {
      fps_ = frames_seen_ ? double(frames - frames_at_start_) / seconds : 0.0;
      frames_at_start_ = frames;
      window_start_ = now;
      frames_seen_ = true;
    }
    if (fps_ <= 0) return;  // nothing measured yet
    char text[32];
    std::snprintf(text, sizeof(text), "%.0f FPS", fps_);
    // Sized with the window: 15 px at 720p.
    float size = std::max(14.0f, io.DisplaySize.y * 15.0f / 720.0f);
    float x = size * 0.9f, y = size * 0.65f;
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    ImFont* font = font_ ? font_ : ImGui::GetFont();
    draw->AddText(font, size, ImVec2(x + 1, y + 1), IM_COL32(0, 0, 0, 230), text);
    draw->AddText(font, size, ImVec2(x, y + 2), IM_COL32(0, 0, 0, 110), text);
    draw->AddText(font, size, ImVec2(x, y), kCream, text);
  }

 private:
  ImFont* font_;
  std::chrono::steady_clock::time_point window_start_ = std::chrono::steady_clock::now();
  uint64_t frames_at_start_ = 0;
  bool frames_seen_ = false;
  double fps_ = 0;
};

}  // namespace

ImFont* AddFpsFont(ImFontAtlas* atlas, const std::filesystem::path& folder) {
  std::filesystem::path path = folder / kFontFile;
  std::error_code ec;
  if (!REXCVAR_GET(show_fps) || !std::filesystem::exists(path, ec)) return nullptr;
  return atlas->AddFontFromFileTTF(path.string().c_str(), 32.0f);
}

std::unique_ptr<rex::ui::ImGuiDialog> CreateFpsCounter(rex::ui::ImGuiDrawer* drawer, ImFont* font) {
  if (!REXCVAR_GET(show_fps)) return nullptr;
  return std::make_unique<FpsCounter>(drawer, font);
}
