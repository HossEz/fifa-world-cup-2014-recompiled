// fifawc14 - ReXGlue Recompiled Project
//
// Customize your app by overriding virtual hooks from rex::ReXApp.

#pragma once

#include <filesystem>

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/perf/counter.h>
#include <rex/rex_app.h>
#include <rex/ui/overlay/debug_overlay.h>
#include <rex/runtime.h>

#include "fps_counter.h"
#include "launcher_redirect.h"
#include "thread_stats.h"
#include "window_branding.h"

REXCVAR_DECLARE(std::string, perf_log_csv);  // defined in the SDK's perf/counter.cpp

class Fifawc14App : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<Fifawc14App>(new Fifawc14App(ctx, "fifawc14",
        PPCImageConfig));
  }

  // Override virtual hooks for customization:
  // void OnPostInitLogging() override {}
  // void OnPreSetup(rex::RuntimeConfig& config) override {}
  // void OnLoadXexImage(std::string& xex_image) override {}
  // void OnPostLoadXexImage() override {}
  // Feed the F3 debug overlay's "Guest: N FPS" line. The SDK times every guest
  // frame swap (VdSwap) into perf counters but leaves the overlay hook unset.
  void OnPostSetup() override {
    // The SDK defines perf_log_csv but never opens the file; do it here.
    if (auto csv = REXCVAR_GET(perf_log_csv); !csv.empty()) {
      rex::perf::SetCsvLogPath(csv);
    }
    StartThreadCpuLog();
    ApplyWindowBranding(window());
    SetGuestFrameStats([smoothed_ms = 0.0, frames = uint64_t{0}]() mutable {
      rex::ui::FrameStats stats;
      double ms = rex::perf::GetSnapshotCounter(rex::perf::CounterId::kFrameTimeUs) / 1000.0;
      if (ms <= 0.0) return stats;  // no frame swapped yet: overlay hides the line
      smoothed_ms = frames++ == 0 ? ms : smoothed_ms * 0.95 + ms * 0.05;
      stats.frame_time_ms = smoothed_ms;
      stats.fps = 1000.0 / smoothed_ms;
      stats.frame_count = frames;
      return stats;
    });
  }
  // The players' overlay: the frame rate if it's on, and no developer keys.
  void OnConfigureFonts(ImFontAtlas* atlas) override {
    fps_font_ = AddFpsFont(atlas, rex::filesystem::GetExecutableFolder());
  }
  void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {
    RemoveDebugKeys();
    fps_counter_ = CreateFpsCounter(drawer, fps_font_);
  }
  // std::unique_ptr<rex::ui::ImGuiDialog> CreateAchievementsOverlay() override;
  // std::unique_ptr<rex::ui::AchievementNotificationDialog>
  // CreateAchievementNotificationDialog() override;
  // void OnShutdown() override {}
  // The SDK resolves game_data_root before it loads fifawc14.toml, so a path set in
  // the config file would be ignored. Load the config early when no path was given.
  void OnConfigurePaths(rex::PathConfig& paths) override {
    if (paths.game_data_root.empty() && std::filesystem::exists(paths.config_path)) {
      rex::cvar::LoadConfig(paths.config_path);
      paths.game_data_root = REXCVAR_GET(game_data_root);
    }
    // Saves, settings and the shader cache in Documents\2014 FIFA World Cup
    // Brazil (the SDK's default is Documents\<app name>, Documents\fifawc14,
    // which earlier builds used: moved over once). An explicit user_data_root /
    // cache_root is kept.
    if (REXCVAR_GET(user_data_root).empty()) {
      std::filesystem::path documents = rex::filesystem::GetUserFolder();
      std::filesystem::path folder = documents / "2014 FIFA World Cup Brazil";
      std::filesystem::path old = documents / "fifawc14";
      std::error_code ec;
      if (!std::filesystem::exists(folder, ec) && std::filesystem::is_directory(old, ec)) {
        std::filesystem::rename(old, folder, ec);
      }
      paths.user_data_root = folder;
      if (REXCVAR_GET(cache_root).empty()) paths.cache_root = folder / "cache";
    }
    OpenLauncherIfNoGame(paths.game_data_root);
  }

 private:
  ImFont* fps_font_ = nullptr;
  std::unique_ptr<rex::ui::ImGuiDialog> fps_counter_;
};
