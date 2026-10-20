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

#include "data_folder.h"
#include "fps_counter.h"
#include "launcher_redirect.h"
#include "quit_shortcut.h"
#include "thread_stats.h"
#include "game_window.h"

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
    SetUpGameWindow(window());
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
  // The players' overlay: the frame rate if it's on, the quit shortcut, and no
  // developer keys.
  void OnConfigureFonts(ImFontAtlas* atlas) override {
    fps_font_ = AddFpsFont(atlas, DataFolder());
  }
  void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {
    RemoveDebugKeys();
    fps_counter_ = CreateFpsCounter(drawer, fps_font_);
    quit_shortcut_ = CreateQuitShortcut(drawer, fps_font_, [this] { return runtime(); }, window(),
                                        immediate_drawer(), DataFolder());
  }
  // std::unique_ptr<rex::ui::ImGuiDialog> CreateAchievementsOverlay() override;
  // std::unique_ptr<rex::ui::AchievementNotificationDialog>
  // CreateAchievementNotificationDialog() override;
  // The overlays (and the quit shortcut's texture) go before the SDK tears
  // down the drawers.
  void OnShutdown() override {
    quit_shortcut_.reset();
    fps_counter_.reset();
  }
  void OnConfigurePaths(rex::PathConfig& paths) override {
    // fifawc14.toml in the data folder (data_folder.h), not necessarily next to
    // the exe. It's also the working folder, so the config's relative paths (the
    // log file, perf_log_csv, F9 captures) land there and not in Program Files.
    std::filesystem::path data = DataFolder();
    std::error_code ec;
    paths.config_path = data / "fifawc14.toml";
    std::filesystem::current_path(data, ec);
    // The SDK resolves game_data_root before it loads fifawc14.toml, so a path set in
    // the config file would be ignored. Load the config early when no path was given.
    if (paths.game_data_root.empty() && std::filesystem::exists(paths.config_path, ec)) {
      rex::cvar::LoadConfig(paths.config_path);
      // UTF-8 in the config: a plain path from it would read it in the system's
      // code page, and a path with letters outside it (é, ö, Cyrillic...) would
      // seem not to exist (the game then went back to the launcher).
      paths.game_data_root = rex::to_path(REXCVAR_GET(game_data_root));
    }
    // Saves and the shader cache in the user folder, or the portable copy's
    // own (data_folder.h; the SDK's default would be Documents\<app name>). An
    // explicit user_data_root / cache_root is kept.
    if (REXCVAR_GET(user_data_root).empty()) {
      std::filesystem::path folder = SavesFolder();
      paths.user_data_root = folder;
      if (REXCVAR_GET(cache_root).empty()) paths.cache_root = folder / "cache";
    }
    OpenLauncherIfNoGame(paths.game_data_root);
  }

 private:
  ImFont* fps_font_ = nullptr;
  std::unique_ptr<rex::ui::ImGuiDialog> fps_counter_;
  std::unique_ptr<rex::ui::ImGuiDialog> quit_shortcut_;
};
