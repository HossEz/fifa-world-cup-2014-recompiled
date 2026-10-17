// Updates from the project's GitHub releases.
//
// At start the launcher asks for the latest release (GitHub's API; one small
// HTTPS request, nothing sent about the player). If it's newer than this
// build, the menu offers it (ui.cpp, the update popup). Updating:
//
// - Installed copy: the release's setup.exe, run silently (Windows asks for
//   admin rights, as for the first install). The installer replaces the files
//   in place, keeps the Apps entry up to date and starts the launcher again.
// - Portable copy (portable.txt): the release's portable zip, unpacked next to
//   the running copy; each program file is renamed to *.old (Windows allows
//   renaming a running exe) and replaced, then the new launcher starts. On any
//   failure the old files are put back. The *.old files go at the next start.
// - Developer builds (a fifawc14.toml next to the exe, no portable.txt) don't
//   check, unless an --update-feed is given for testing.
//
// Every download is checked against the release's SHA256SUMS.txt before
// anything is changed. Settings and saves aren't touched by either path.

#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace launcher {

// The project's latest release on GitHub.
inline constexpr const char* kUpdateFeed =
    "https://api.github.com/repos/HossEz/fifa-world-cup-2014-recompiled/releases/latest";

enum class InstallKind { kInstalled, kPortable, kDeveloper };
InstallKind DetectInstallKind(const std::filesystem::path& game_folder);

struct Release {
  std::string version;               // "0.2.0" (the tag without its "v")
  std::vector<std::string> changes;  // the release text's bullet points
  std::string setup_url, portable_url, sums_url;
  std::string page_url;              // the release's page
};

// The latest release if it's newer than `current_version` and has the files
// an update needs; nothing if not, or if the feed can't be reached.
std::optional<Release> CheckForUpdate(const std::string& feed_url,
                                      const std::string& current_version);

// "0.10.0" > "0.9.3": numeric, part by part.
bool IsNewerVersion(const std::string& candidate, const std::string& current);

// Removes the *.old files a portable update left behind.
void CleanUpAfterUpdate(const std::filesystem::path& game_folder);

// One update, run on its own thread; the UI polls it.
class UpdateJob {
 public:
  enum class State { kDownloading, kVerifying, kInstalling, kReady, kFailed };

  UpdateJob(Release release, InstallKind kind, std::filesystem::path game_folder);
  ~UpdateJob();

  State state() const { return state_.load(); }
  uint64_t downloaded() const { return downloaded_.load(); }
  uint64_t total() const { return total_.load(); }
  std::string error() const;

  // kReady: starts what comes next (the installer, or the new portable
  // launcher). The caller then quits the launcher. False if that couldn't
  // start (error() says why; the update was cancelled at the admin prompt).
  bool Finish();

 private:
  void Run();
  void Fail(std::string message);

  Release release_;
  InstallKind kind_;
  std::filesystem::path game_folder_;
  std::filesystem::path work_;      // %TEMP%\fifa-world-cup-2014-recompiled-update
  std::filesystem::path download_;  // the setup.exe (installed copy)
  std::atomic<State> state_{State::kDownloading};
  std::atomic<uint64_t> downloaded_{0}, total_{0};
  mutable std::mutex error_mutex_;
  std::string error_;
  std::thread thread_;
};

}  // namespace launcher
