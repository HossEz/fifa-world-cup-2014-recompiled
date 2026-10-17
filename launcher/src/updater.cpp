#include "updater.h"

#include <windows.h>
#include <bcrypt.h>
#include <shellapi.h>
#include <winhttp.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <functional>
#include <sstream>

#include <nlohmann/json.hpp>

#include "log.h"

namespace launcher {

namespace fs = std::filesystem;

namespace {

std::wstring Wide(const std::string& text) {
  int n = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), int(text.size()), nullptr, 0);
  std::wstring wide(size_t(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.c_str(), int(text.size()), wide.data(), n);
  return wide;
}

std::string Lower(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(),
                 [](unsigned char c) { return char(std::tolower(c)); });
  return text;
}

// GET `url` (https, or http for local tests), following redirects (GitHub's
// download links redirect to its file storage). Each received block goes to
// `sink`, which returns false to stop. `total` gets the length if it's known.
bool HttpGet(const std::string& url, const std::function<bool(const char*, size_t)>& sink,
             std::atomic<uint64_t>* total = nullptr) {
  std::wstring wide = Wide(url);
  URL_COMPONENTS parts = {sizeof(parts)};
  wchar_t host[256] = {}, path[2048] = {};
  parts.lpszHostName = host;
  parts.dwHostNameLength = DWORD(std::size(host));
  parts.lpszUrlPath = path;
  parts.dwUrlPathLength = DWORD(std::size(path));
  if (!WinHttpCrackUrl(wide.c_str(), 0, 0, &parts)) return false;
  bool secure = parts.nScheme == INTERNET_SCHEME_HTTPS;
  std::wstring target = std::wstring(path) + (parts.lpszExtraInfo ? parts.lpszExtraInfo : L"");

  HINTERNET session = WinHttpOpen(L"fifa-world-cup-2014-recompiled-launcher",
                                  WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                                  WINHTTP_NO_PROXY_BYPASS, 0);
  if (!session) return false;
  WinHttpSetTimeouts(session, 10000, 10000, 15000, 30000);
  HINTERNET connection = WinHttpConnect(session, host, parts.nPort, 0);
  HINTERNET request =
      connection ? WinHttpOpenRequest(connection, L"GET", target.c_str(), nullptr,
                                      WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                      secure ? WINHTTP_FLAG_SECURE : 0)
                 : nullptr;
  bool ok = false;
  if (request) {
    const wchar_t* headers = L"Accept: application/vnd.github+json\r\n";
    DWORD status = 0, size = sizeof(status);
    if (WinHttpSendRequest(request, headers, DWORD(-1), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(request, nullptr) &&
        WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX) &&
        status == 200) {
      if (total) {
        wchar_t length[32] = {};
        DWORD length_size = sizeof(length);
        if (WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX,
                                length, &length_size, WINHTTP_NO_HEADER_INDEX)) {
          total->store(_wcstoui64(length, nullptr, 10));
        }
      }
      ok = true;
      std::vector<char> buffer(64 * 1024);
      for (;;) {
        DWORD read = 0;
        if (!WinHttpReadData(request, buffer.data(), DWORD(buffer.size()), &read)) {
          ok = false;
          break;
        }
        if (read == 0) break;
        if (!sink(buffer.data(), read)) {
          ok = false;
          break;
        }
      }
    } else if (status) {
      Log("update: %s answered %lu", url.c_str(), status);
    }
  }
  if (request) WinHttpCloseHandle(request);
  if (connection) WinHttpCloseHandle(connection);
  WinHttpCloseHandle(session);
  return ok;
}

bool HttpGetText(const std::string& url, std::string* text) {
  text->clear();
  return HttpGet(url, [&](const char* data, size_t size) {
    text->append(data, size);
    return text->size() < 8 * 1024 * 1024;
  });
}

std::string Sha256File(const fs::path& path) {
  BCRYPT_ALG_HANDLE algorithm = nullptr;
  BCRYPT_HASH_HANDLE hash = nullptr;
  if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) return {};
  std::string hex;
  if (BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) == 0) {
    std::ifstream file(path, std::ios::binary);
    std::vector<char> buffer(1 << 20);
    bool ok = bool(file);
    while (ok && file) {
      file.read(buffer.data(), std::streamsize(buffer.size()));
      if (file.gcount() > 0) {
        ok = BCryptHashData(hash, reinterpret_cast<PUCHAR>(buffer.data()), ULONG(file.gcount()), 0) == 0;
      }
    }
    uint8_t digest[32];
    if (ok && BCryptFinishHash(hash, digest, sizeof(digest), 0) == 0) {
      char byte[3];
      for (uint8_t b : digest) {
        std::snprintf(byte, sizeof(byte), "%02x", b);
        hex += byte;
      }
    }
    BCryptDestroyHash(hash);
  }
  BCryptCloseAlgorithmProvider(algorithm, 0);
  return hex;
}

// SHA256SUMS.txt: "<hash>  <file name>" per line (as sha256sum writes it).
std::string ExpectedHash(const std::string& sums, const std::string& file_name) {
  std::istringstream lines(sums);
  std::string line;
  while (std::getline(lines, line)) {
    std::istringstream fields(line);
    std::string hash, name;
    fields >> hash >> name;
    if (!name.empty() && name[0] == '*') name.erase(0, 1);
    if (Lower(name) == Lower(file_name)) return Lower(hash);
  }
  return {};
}

std::string FileNameOf(const std::string& url) {
  size_t slash = url.find_last_of('/');
  return slash == std::string::npos ? url : url.substr(slash + 1);
}

// Runs a program hidden and waits for it (tar, for the portable zip).
bool RunHidden(const std::wstring& command_line, const fs::path& folder) {
  std::wstring command = command_line;
  STARTUPINFOW startup = {sizeof(startup)};
  startup.dwFlags = STARTF_USESHOWWINDOW;
  startup.wShowWindow = SW_HIDE;
  PROCESS_INFORMATION process = {};
  if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                      folder.c_str(), &startup, &process)) {
    return false;
  }
  WaitForSingleObject(process.hProcess, 5 * 60 * 1000);
  DWORD code = 1;
  GetExitCodeProcess(process.hProcess, &code);
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);
  return code == 0;
}

}  // namespace

InstallKind DetectInstallKind(const fs::path& game_folder) {
  std::error_code ec;
  if (fs::exists(game_folder / "portable.txt", ec)) return InstallKind::kPortable;
  if (fs::exists(game_folder / "fifawc14.toml", ec)) return InstallKind::kDeveloper;
  return InstallKind::kInstalled;
}

bool IsNewerVersion(const std::string& candidate, const std::string& current) {
  auto parts = [](const std::string& version) {
    std::vector<long> numbers;
    std::istringstream stream(version);
    std::string part;
    while (std::getline(stream, part, '.')) numbers.push_back(std::strtol(part.c_str(), nullptr, 10));
    numbers.resize(4, 0);
    return numbers;
  };
  return parts(candidate) > parts(current);
}

std::optional<Release> CheckForUpdate(const std::string& feed_url,
                                      const std::string& current_version) {
  std::string text;
  if (!HttpGetText(feed_url, &text)) {
    Log("update: no answer from %s", feed_url.c_str());
    return std::nullopt;
  }
  nlohmann::json feed = nlohmann::json::parse(text, nullptr, false);
  if (feed.is_discarded() || !feed.is_object()) return std::nullopt;
  if (feed.value("draft", false) || feed.value("prerelease", false)) return std::nullopt;
  Release release;
  release.version = feed.value("tag_name", "");
  if (!release.version.empty() && (release.version[0] == 'v' || release.version[0] == 'V')) {
    release.version.erase(0, 1);
  }
  release.page_url = feed.value("html_url", "");
  if (release.version.empty() || !IsNewerVersion(release.version, current_version)) {
    Log("update: latest is %s, this is %s", release.version.c_str(), current_version.c_str());
    return std::nullopt;
  }
  for (const auto& asset : feed.value("assets", nlohmann::json::array())) {
    std::string name = Lower(asset.value("name", "")), url = asset.value("browser_download_url", "");
    if (name.ends_with("-setup.exe")) release.setup_url = url;
    else if (name.ends_with("-portable.zip")) release.portable_url = url;
    else if (name == "sha256sums.txt") release.sums_url = url;
  }
  // The release text's bullet points ("- ..." / "* ..."), as the summary.
  std::istringstream lines(feed.value("body", ""));
  std::string line;
  while (std::getline(lines, line) && release.changes.size() < 4) {
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
    size_t start = line.find_first_not_of(' ');
    if (start == std::string::npos || line.size() < start + 3) continue;
    if ((line[start] == '-' || line[start] == '*') && line[start + 1] == ' ') {
      std::string change = line.substr(start + 2);
      change.erase(std::remove(change.begin(), change.end(), '`'), change.end());
      std::string plain;  // without **bold** markers
      for (size_t i = 0; i < change.size(); ++i) {
        if (change[i] == '*' && i + 1 < change.size() && change[i + 1] == '*') ++i;
        else plain += change[i];
      }
      release.changes.push_back(plain);
    }
  }
  if (release.sums_url.empty() || (release.setup_url.empty() && release.portable_url.empty())) {
    Log("update: %s lacks its files", release.version.c_str());
    return std::nullopt;
  }
  Log("update: %s is available (this is %s)", release.version.c_str(), current_version.c_str());
  return release;
}

fs::path UpdateWorkFolder() {
  wchar_t temp[MAX_PATH];
  GetTempPathW(MAX_PATH, temp);
  return fs::path(temp) / "fifa-world-cup-2014-recompiled-update";
}

void CleanUpAfterUpdate(const fs::path& game_folder) {
  std::error_code ec;
  for (const auto& entry : fs::directory_iterator(game_folder, ec)) {
    if (entry.path().extension() == ".old") fs::remove(entry.path(), ec);
  }
  // The download and what was unpacked from it (the installer has finished by
  // the time the launcher runs again).
  fs::remove_all(UpdateWorkFolder(), ec);
}

UpdateJob::UpdateJob(Release release, InstallKind kind, fs::path game_folder)
    : release_(std::move(release)), kind_(kind), game_folder_(std::move(game_folder)) {
  work_ = UpdateWorkFolder();
  thread_ = std::thread([this] { Run(); });
}

UpdateJob::~UpdateJob() {
  if (thread_.joinable()) thread_.join();
}

std::string UpdateJob::error() const {
  std::lock_guard lock(error_mutex_);
  return error_;
}

void UpdateJob::Fail(std::string message) {
  Log("update: failed: %s", message.c_str());
  {
    std::lock_guard lock(error_mutex_);
    error_ = std::move(message);
  }
  state_ = State::kFailed;
}

void UpdateJob::Run() {
  std::error_code ec;
  fs::remove_all(work_, ec);
  fs::create_directories(work_, ec);
  const std::string& url = kind_ == InstallKind::kPortable ? release_.portable_url : release_.setup_url;
  if (url.empty()) return Fail("This release has no download for this kind of copy.");

  std::string sums;
  if (!HttpGetText(release_.sums_url, &sums)) return Fail("Couldn't download the checksums.");
  download_ = work_ / FileNameOf(url);
  {
    std::ofstream file(download_, std::ios::binary);
    bool ok = HttpGet(url, [&](const char* data, size_t size) {
      file.write(data, std::streamsize(size));
      downloaded_ += size;
      return bool(file);
    }, &total_);
    if (!ok) return Fail("The download failed. Check your connection and try again.");
  }

  state_ = State::kVerifying;
  std::string expected = ExpectedHash(sums, FileNameOf(url));
  std::string actual = Sha256File(download_);
  if (expected.empty() || actual != expected) {
    fs::remove(download_, ec);
    return Fail("The download didn't match its checksum, so nothing was changed.");
  }
  Log("update: %s verified", FileNameOf(url).c_str());

  if (kind_ == InstallKind::kInstalled) {
    state_ = State::kReady;  // the installer runs from Finish(), on the UI thread
    return;
  }

  // Portable: unpack (Windows' own tar reads zips), then swap the files.
  state_ = State::kInstalling;
  fs::path unpacked = work_ / "unpacked";
  fs::create_directories(unpacked, ec);
  if (!RunHidden(L"tar.exe -xf \"" + download_.wstring() + L"\"", unpacked)) {
    return Fail("Couldn't unpack the update.");
  }
  fs::path source;
  for (auto it = fs::recursive_directory_iterator(unpacked, ec); it != fs::recursive_directory_iterator();
       it.increment(ec)) {
    if (it->path().filename() == "Launcher.exe") {
      source = it->path().parent_path();
      break;
    }
  }
  if (source.empty()) return Fail("The update has no launcher in it.");

  std::vector<fs::path> replaced, added;
  auto roll_back = [&] {
    for (const fs::path& path : added) fs::remove(path, ec);
    for (const fs::path& path : replaced) {
      fs::path old = path;
      old += ".old";
      fs::remove(path, ec);
      fs::rename(old, path, ec);
    }
  };
  for (const auto& entry : fs::directory_iterator(source, ec)) {
    if (!entry.is_regular_file()) continue;
    fs::path target = game_folder_ / entry.path().filename();
    if (entry.path().filename() == "portable.txt" && fs::exists(target)) continue;  // kept as it is
    if (fs::exists(target)) {
      fs::path old = target;
      old += ".old";
      fs::remove(old, ec);
      fs::rename(target, old, ec);
      if (ec) {
        roll_back();
        return Fail("Couldn't replace " + entry.path().filename().string() + ". Is the game running?");
      }
      replaced.push_back(target);
    } else {
      added.push_back(target);
    }
    fs::copy_file(entry.path(), target, fs::copy_options::overwrite_existing, ec);
    if (ec) {
      roll_back();
      return Fail("Couldn't write " + entry.path().filename().string() + " into the game's folder.");
    }
  }
  Log("update: %zu files replaced, %zu added", replaced.size(), added.size());
  state_ = State::kReady;
}

bool UpdateJob::Finish() {
  if (state_ != State::kReady) return false;
  if (kind_ == InstallKind::kInstalled) {
    // Silent: no pages, only Windows' admin prompt. The installer starts the
    // launcher again when it's done (setup.iss, [Run] for silent installs).
    SHELLEXECUTEINFOW info = {sizeof(info)};
    info.fMask = SEE_MASK_NOASYNC;
    info.lpVerb = L"open";
    std::wstring file = download_.wstring();
    info.lpFile = file.c_str();
    info.lpParameters = L"/VERYSILENT /SUPPRESSMSGBOXES /NORESTART";
    info.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&info)) {
      Fail(GetLastError() == ERROR_CANCELLED ? "The update was cancelled at Windows' prompt."
                                             : "Couldn't start the installer.");
      return false;
    }
    Log("update: installer started");
    return true;
  }
  // Portable: the new launcher, from the same folder.
  fs::path exe = game_folder_ / "Launcher.exe";
  std::wstring command = L"\"" + exe.wstring() + L"\"";
  STARTUPINFOW startup = {sizeof(startup)};
  PROCESS_INFORMATION process = {};
  if (!CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr,
                      game_folder_.c_str(), &startup, &process)) {
    Fail("The update is in place, but the new launcher didn't start. Start it again yourself.");
    return false;
  }
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);
  Log("update: new launcher started");
  return true;
}

}  // namespace launcher
