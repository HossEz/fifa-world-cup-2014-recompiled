#include "game_files.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <cctype>
#include <set>
#include <span>
#include <unordered_set>
#include <string>
#include <vector>

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/filesystem/devices/host_path_device.h>
#include <rex/filesystem/entry.h>
#include <rex/filesystem/file.h>
#include <rex/filesystem/vfs.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>
#include <rex/system/lzx.h>
#include <rex/system/xmemory.h>
#include <rex/types.h>
#include <rex/system/xtypes.h>

#ifdef _WIN32
#include <windows.h>
#endif

#include "game_text.h"

REX_EXTERN(__imp__sub_8241B750);

REXCVAR_DEFINE_STRING(button_icons, "auto", "Game",
                      "Button icons: \"auto\" (PlayStation's when a PlayStation controller is "
                      "connected as the game starts), \"xbox\" or \"playstation\"");

namespace {

using Bytes = std::vector<uint8_t>;
using rex::X_STATUS;
namespace fs = rex::filesystem;

// Where the game's data is mounted (the SDK's Runtime::SetupVfs; d: links to it),
// and where the files built here are.
constexpr const char* kGame = "\\Device\\Harddisk0\\Partition1\\";
constexpr const char* kBuilt = "\\Device\\GameFilesPC";

uint32_t Be32(const uint8_t* p) {
  return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
}
void PutBe32(Bytes& v, size_t at, uint32_t x) {
  v[at] = uint8_t(x >> 24), v[at + 1] = uint8_t(x >> 16), v[at + 2] = uint8_t(x >> 8), v[at + 3] = uint8_t(x);
}

// `size` bytes at `offset` of a game file (all of it: size 0).
std::optional<Bytes> Read(fs::VirtualFileSystem* vfs, const std::string& path, size_t offset = 0,
                          size_t size = 0) {
  fs::Entry* entry = vfs->ResolvePath(path);
  if (!entry) return std::nullopt;
  if (!size) size = entry->size() - offset;
  fs::File* file = nullptr;
  if (entry->Open(fs::FileAccess::kFileReadData, &file) != X_STATUS_SUCCESS || !file) return std::nullopt;
  Bytes data(size);
  size_t got = 0;
  X_STATUS status = file->ReadSync(data, offset, &got);
  file->Destroy();
  if (status != X_STATUS_SUCCESS || got != size) return std::nullopt;
  return data;
}

// The archives' tables: an EA BIG4 is "BIG4", its size (little-endian), the
// entry count and the table's size, then per entry {offset, size, name}.
struct Archive {
  std::string path;
  std::map<std::string, std::pair<uint32_t, uint32_t>> entries;
};
std::optional<Archive> ReadArchive(fs::VirtualFileSystem* vfs, const std::string& path) {
  auto head = Read(vfs, path, 0, 16);
  if (!head || std::memcmp(head->data(), "BIG4", 4) != 0) return std::nullopt;
  auto table = Read(vfs, path, 16, Be32(head->data() + 12) - 16);
  if (!table) return std::nullopt;
  Archive archive{path, {}};
  const uint32_t count = Be32(head->data() + 8);
  size_t at = 0;
  for (uint32_t i = 0; i < count && at + 9 <= table->size(); ++i) {
    const uint8_t* e = table->data() + at;
    auto end = std::find(table->begin() + at + 8, table->end(), 0);
    std::string name(table->begin() + at + 8, end);
    archive.entries[name] = {Be32(e), Be32(e + 4)};
    at = (end - table->begin()) + 1;
  }
  return archive;
}

// An Xbox "chunklzx" file uncompressed: chunks of XMemCompress LZX frames
// (window 2^17), each frame {0xFF, its size, compressed size} or {compressed
// size} for a full 32 KB.
std::optional<Bytes> Unpack(const Bytes& d) {
  if (d.size() < 0x1C || std::memcmp(d.data(), "chunklzx", 8) != 0) return d;  // not compressed
  const uint32_t size = Be32(&d[12]), chunk = Be32(&d[16]), chunks = Be32(&d[20]);
  Bytes out(size);
  size_t at = 0x1C, done = 0;
  for (uint32_t c = 0; c < chunks; ++c) {
    while (at + 8 <= d.size() && Be32(&d[at]) == 0) at += 4;
    if (at + 8 > d.size()) return std::nullopt;
    const size_t packed = Be32(&d[at]), end = at + 8 + packed;
    if (end > d.size()) return std::nullopt;
    Bytes raw;
    for (size_t p = at + 8; p + 2 <= end;) {
      size_t length;
      if (d[p] == 0xFF) {
        if (p + 5 > end) return std::nullopt;
        length = size_t(d[p + 3]) << 8 | d[p + 4];
        p += 5;
      } else {
        length = size_t(d[p]) << 8 | d[p + 1];
        p += 2;
      }
      if (!length || p + length > end) break;
      raw.insert(raw.end(), d.begin() + p, d.begin() + p + length);
      p += length;
    }
    const size_t want = std::min<size_t>(chunk, size - done);
    if (lzx_decompress(raw.data(), raw.size(), out.data() + done, want, 1 << 17, nullptr, 0) != 0) {
      return std::nullopt;
    }
    done += want;
    at = (end + 7) & ~size_t(7);  // chunks start on 8 bytes
  }
  return out;
}

// The BIGF inside a UI file: "BIGF", size, count, table size, then {offset,
// size, name} per entry (big-endian).
std::map<std::string, std::pair<uint32_t, uint32_t>> BigfEntries(const Bytes& d) {
  std::map<std::string, std::pair<uint32_t, uint32_t>> entries;
  if (d.size() < 16 || std::memcmp(d.data(), "BIGF", 4) != 0) return entries;
  size_t at = 16;
  for (uint32_t i = 0, n = Be32(&d[8]); i < n && at + 9 <= d.size(); ++i) {
    auto end = std::find(d.begin() + at + 8, d.end(), 0);
    if (end == d.end()) break;
    entries[std::string(d.begin() + at + 8, end)] = {Be32(&d[at]), Be32(&d[at + 4])};
    at = (end - d.begin()) + 1;
  }
  return entries;
}

// Byte offset of each 4x4 block of a tiled Xbox texture, in row order (Xenia's
// GetTiledOffset2D; tools/xtexture.py).
size_t TiledOffset(uint32_t x, uint32_t y, uint32_t width_blocks, uint32_t log2_bpb) {
  const uint32_t pitch = (width_blocks + 31) & ~31u;
  const uint32_t macro = ((x >> 5) + (y >> 5) * (pitch >> 5)) << (log2_bpb + 7);
  const uint32_t micro = ((x & 7) + ((y & 0xE) << 2)) << log2_bpb;
  const uint32_t offset = macro + ((micro & ~0xFu) << 1) + (micro & 0xF) + ((y & 1) << 4);
  return ((offset & ~0x1FFu) << 3) + ((y & 16) << 7) + ((offset & 0x1C0) << 2) +
         ((((y & 8) >> 2) + (x >> 3)) & 3) * 64 + (offset & 0x3F);
}

// Puts a picture (DXT5 blocks in row order) into the XPR2 texture of a BIGF
// entry: same size and format, tiled and byte-swapped as the Xbox has it.
bool PutPicture(Bytes& d, std::pair<uint32_t, uint32_t> entry, uint32_t width, uint32_t height,
                const uint8_t* blocks) {
  const size_t o = entry.first, size = entry.second;
  if (o + size > d.size() || size < 0x60 || std::memcmp(&d[o], "XPR2", 4) != 0 ||
      std::memcmp(&d[o + 16], "TX2D", 4) != 0) {
    return false;
  }
  const size_t header = 12 + Be32(&d[o + 4]), texture = o + 12 + Be32(&d[o + 20]);
  if (texture + 52 > o + size) return false;
  const uint32_t format = Be32(&d[texture + 32]), dims = Be32(&d[texture + 36]);
  const uint32_t w = (dims & 0x1FFF) + 1, h = ((dims >> 13) & 0x1FFF) + 1;
  if ((format & 0x3F) != 0x14 || w != width || h != height) return false;  // DXT5 of this size
  const size_t start = o + header + ((format >> 12) << 12);
  const uint32_t bw = w / 4, bh = h / 4;
  if (start + TiledOffset(bw - 1, bh - 1, bw, 4) + 16 > o + size) return false;
  std::fill(d.begin() + start, d.begin() + o + size, 0);
  for (uint32_t y = 0; y < bh; ++y) {
    for (uint32_t x = 0; x < bw; ++x) {
      uint8_t* to = &d[start + TiledOffset(x, y, bw, 4)];
      const uint8_t* from = blocks + (size_t(y) * bw + x) * 16;
      for (int i = 0; i < 16; i += 2) to[i] = from[i + 1], to[i + 1] = from[i];  // 8-in-16
    }
  }
  return true;
}

// playstation_buttons.bin (the exe's resource 300; pc/src/game_files.rc).
std::span<const uint8_t> Pictures() {
#ifdef _WIN32
  HRSRC found = FindResourceW(nullptr, MAKEINTRESOURCEW(300), MAKEINTRESOURCEW(10));  // RT_RCDATA
  if (!found) return {};
  HGLOBAL loaded = LoadResource(nullptr, found);
  if (!loaded) return {};
  return {static_cast<const uint8_t*>(LockResource(loaded)), SizeofResource(nullptr, found)};
#else
  return {};
#endif
}

// A PlayStation controller (Sony's) among the connected game controllers, also
// one DS4Windows and the like show as an Xbox 360 pad: the buttons in the
// player's hands are PlayStation's. The launcher's hints use the same test
// (launcher/src/controllers.cpp).
bool PlayStationControllerConnected() {
#ifdef _WIN32
  UINT count = 0;
  if (GetRawInputDeviceList(nullptr, &count, sizeof(RAWINPUTDEVICELIST)) != 0 || !count) return false;
  std::vector<RAWINPUTDEVICELIST> devices(count);
  if (GetRawInputDeviceList(devices.data(), &count, sizeof(RAWINPUTDEVICELIST)) == UINT(-1)) return false;
  for (const RAWINPUTDEVICELIST& device : devices) {
    if (device.dwType != RIM_TYPEHID) continue;
    RID_DEVICE_INFO info{};
    info.cbSize = sizeof(info);
    UINT size = sizeof(info);
    if (GetRawInputDeviceInfoW(device.hDevice, RIDI_DEVICEINFO, &info, &size) == UINT(-1)) continue;
    const auto& hid = info.hid;
    if (hid.dwVendorId == 0x054C && hid.usUsagePage == 1 && (hid.usUsage == 4 || hid.usUsage == 5)) {
      return true;
    }
  }
#endif
  return false;
}

class Builder {
 public:
  Builder(fs::VirtualFileSystem* vfs, std::filesystem::path folder) : vfs_(vfs), folder_(std::move(folder)) {}

  // An entry of the game's archives (the first that has it).
  std::optional<Bytes> FromArchives(const std::string& name) {
    if (archives_.empty()) {
      for (std::string n : {"0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "x"}) {
        if (auto archive = ReadArchive(vfs_, std::string(kGame) + "data" + n + ".big")) {
          archives_.push_back(std::move(*archive));
        }
      }
    }
    for (const Archive& archive : archives_) {
      auto it = archive.entries.find(name);
      if (it != archive.entries.end()) return Read(vfs_, archive.path, it->second.first, it->second.second);
    }
    return std::nullopt;
  }

  // Every file the archives have, as the game names it on disk ("d:\data\...",
  // lower case).
  std::unordered_set<std::string> ArchivedPaths() const {
    std::unordered_set<std::string> paths;
    for (const Archive& archive : archives_) {
      for (const auto& [name, place] : archive.entries) {
        std::string path = "d:\\" + name;
        std::replace(path.begin(), path.end(), '/', '\\');
        paths.insert(std::move(path));
      }
    }
    return paths;
  }

  // Writes `data` as the built file for the game's `path` (data/... with slashes),
  // at the same path in the folder.
  bool Put(const std::string& path, const Bytes& data) {
    std::filesystem::path file_path = folder_ / rex::to_path(path);
    std::error_code ec;
    std::filesystem::create_directories(file_path.parent_path(), ec);
    std::ofstream file(file_path, std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
    if (!file) return false;
    std::string folder = path.substr(0, path.rfind('/'));
    std::replace(folder.begin(), folder.end(), '/', '\\');
    folders_.insert(folder);
    return true;
  }

  // Links each game folder that has a built file to the built one, once every
  // file is written (the device reads the folder once). The SDK opens a file
  // through its folder, so a folder is what's linked: the game reads loose files
  // before its archives, and finds the others in the archives.
  void Link() {
    if (folders_.empty()) return;
    auto device = std::make_unique<fs::HostPathDevice>(kBuilt, folder_, true);
    if (!device->Initialize() || !vfs_->RegisterDevice(std::move(device))) {
      REXLOG_ERROR("game files: couldn't mount {}", folder_.string());
      return;
    }
    for (const std::string& folder : folders_) {
      vfs_->RegisterSymbolicLink(kGame + folder, std::string(kBuilt) + "\\" + folder);
    }
  }

 private:
  fs::VirtualFileSystem* vfs_;
  std::filesystem::path folder_;
  std::vector<Archive> archives_;
  std::set<std::string> folders_;
};

// The language files in the PC's words: a new data\loc\locale.big (a BIG4 like
// the archives: its table, a "L286" tag, entries aligned to 64 bytes).
int BuildLanguageFiles(fs::VirtualFileSystem* vfs, Builder& builder, bool playstation) {
  auto big = Read(vfs, std::string(kGame) + "data\\loc\\locale.big");
  if (!big || big->size() < 16 || std::memcmp(big->data(), "BIG4", 4) != 0) return 0;
  const Bytes& d = *big;
  const uint32_t count = Be32(&d[8]);
  struct Item {
    size_t table_at;
    Bytes data;
  };
  std::vector<Item> items;
  size_t at = 16, first = d.size();
  int reworded = 0;
  for (uint32_t i = 0; i < count && at + 9 <= d.size(); ++i) {
    const uint32_t off = Be32(&d[at]), size = Be32(&d[at + 4]);
    auto end = std::find(d.begin() + at + 8, d.end(), 0);
    if (end == d.end() || size_t(off) + size > d.size()) return 0;
    std::string name(d.begin() + at + 8, end);
    Item item{at, Bytes(d.begin() + off, d.begin() + off + size)};
    first = std::min<size_t>(first, off);
    if (name.size() > 3 && name.compare(name.size() - 3, 3, ".db") == 0) {
      std::string language = name.substr(name.rfind('/') + 1);
      language.resize(language.size() - 3);
      int changed = 0;
      if (auto new_db = RewordLanguageFile(language, item.data, playstation, &changed)) {
        item.data = std::move(*new_db);
        REXLOG_INFO("game files: {} lines reworded in {}", changed, language);
        ++reworded;
      }
    }
    items.push_back(std::move(item));
    at = (end - d.begin()) + 1;
  }
  if (!reworded) return 0;
  Bytes out(d.begin(), d.begin() + first);  // the header and table as they are, then the entries
  for (Item& item : items) {
    out.resize((out.size() + 63) & ~size_t(63));
    PutBe32(out, item.table_at, uint32_t(out.size()));
    PutBe32(out, item.table_at + 4, uint32_t(item.data.size()));
    out.insert(out.end(), item.data.begin(), item.data.end());
  }
  const uint32_t total = uint32_t(out.size());
  std::memcpy(&out[4], &total, 4);  // little-endian
  return builder.Put("data/loc/locale.big", out) ? reworded : 0;
}

// The button icons, Side Select pads and pad outlines (the player's files with
// the pictures of playstation_buttons.bin: "PSB1", file count, then per file
// its path and pictures {entry name, width, height, DXT5 blocks}), and the
// controller pictures (the PS3 ones in the Xbox data).
int BuildPlayStationFiles(Builder& builder) {
  std::span<const uint8_t> pack = Pictures();
  if (pack.size() < 8 || std::memcmp(pack.data(), "PSB1", 4) != 0) return 0;
  auto u16 = [&](size_t at) { return uint32_t(pack[at]) | uint32_t(pack[at + 1]) << 8; };
  const uint32_t files = u16(4) | u16(6) << 16;
  size_t at = 8;
  int built = 0;
  for (uint32_t f = 0; f < files; ++f) {
    if (at + 2 > pack.size()) return built;
    const size_t path_size = u16(at);
    std::string path(pack.begin() + at + 2, pack.begin() + at + 2 + path_size);
    at += 2 + path_size;
    const uint32_t pictures = pack[at++];
    std::optional<Bytes> file;
    if (auto packed = builder.FromArchives(path)) file = Unpack(*packed);
    auto entries = file ? BigfEntries(*file) : decltype(BigfEntries({})){};
    bool ok = file && !entries.empty();
    for (uint32_t p = 0; p < pictures; ++p) {
      const size_t name_size = pack[at];
      std::string entry(pack.begin() + at + 1, pack.begin() + at + 1 + name_size);
      at += 1 + name_size;
      const uint32_t w = u16(at), h = u16(at + 2);
      at += 4;
      const uint8_t* blocks = pack.data() + at;
      at += size_t(w / 4) * (h / 4) * 16;
      if (at > pack.size()) return built;
      auto it = entries.find(entry);
      ok = ok && it != entries.end() && PutPicture(*file, it->second, w, h, blocks);
    }
    if (ok && builder.Put(path, *file)) {
      ++built;
    } else {
      REXLOG_WARN("game files: {} not built (not as expected in this copy)", path);
    }
  }
  const std::string pictures = "data/ui/artassets/controllerimage/";
  for (auto [from, to] : {std::pair{"controller_ps3", "controller_xbox360"},
                          std::pair{"controller_ps3", "controller_xbox360_h"},
                          std::pair{"singlecontroller_ps3", "singlecontroller_x360"}}) {
    if (auto file = builder.FromArchives(pictures + from + ".big")) {
      if (builder.Put(pictures + to + ".big", *file)) ++built;
    }
  }
  return built;
}

// The Brazuca Final Rio (ball 92) is encrypted with a key that came from EA's
// servers with the catalogue, so the game can't read it and the ball is
// invisible. Since the catalogue is unlocked the game uses it in the final, so
// ball 92 gets the normal Brazuca's files (ball 91).
int BuildFinalBall(Builder& builder) {
  const std::string ball = "data/sceneassets/ball/ball_";
  int built = 0;
  for (const char* part : {".rx3", "_textures.rx3"}) {
    if (auto file = builder.FromArchives(ball + "91" + part)) {
      if (builder.Put(ball + "92" + part, *file)) ++built;
    }
  }
  return built;
}

bool g_loose_files_first = false;

}  // namespace

// The game's "does this file exist" (sub_82F83610 asks it for loose_files_first
// before mounting the archives): yes for loose_files_first when the built
// files need the game to read loose files first. Its root folder can't be
// linked like the others.
REX_HOOK_RAW(sub_8241B750) {
  if (g_loose_files_first) {
    const char* name = REX_KERNEL_MEMORY()->TranslateVirtual<const char*>(ctx.r3.u32);
    if (std::strcmp(name, "loose_files_first") == 0) {
      ctx.r3.u64 = 1;
      return;
    }
  }
  __imp__sub_8241B750(ctx, base);
}

bool PlayStationButtons() {
  static const bool playstation = [] {
    const std::string choice = REXCVAR_GET(button_icons);
    return choice == "playstation" || (choice != "xbox" && PlayStationControllerConnected());
  }();
  return playstation;
}

void SetUpGameFiles(rex::Runtime* runtime) {
  if (!runtime || !runtime->file_system()) return;
  fs::VirtualFileSystem* vfs = runtime->file_system();
  const std::string choice = REXCVAR_GET(button_icons);
  const bool playstation = PlayStationButtons();
  // A fresh folder each start, so nothing of an earlier choice is linked.
  std::filesystem::path folder = runtime->cache_root() / "game_files";
  std::error_code ec;
  std::filesystem::remove_all(folder, ec);
  Builder builder(vfs, folder);
  int languages = BuildLanguageFiles(vfs, builder, playstation);
  int files = 0;
  if (playstation) files = BuildPlayStationFiles(builder);
  const int ball = BuildFinalBall(builder);
  g_loose_files_first = files + ball > 0;
  if (g_loose_files_first) {
    // Reading loose files first, the game looks on disk for every file it loads;
    // a miss of one its archives have is expected, so it isn't logged.
    rex::system::SetQuietMissingFiles(
        [archived = builder.ArchivedPaths()](std::string_view path) {
          std::string key(path);
          std::transform(key.begin(), key.end(), key.begin(),
                         [](unsigned char c) { return char(std::tolower(c)); });
          return archived.count(key) != 0;
        });
  }
  builder.Link();
  REXLOG_INFO("game files: {} buttons ({}), {} files; {} language files reworded; final ball {}",
              playstation ? "PlayStation" : "Xbox",
              choice == "playstation" || choice == "xbox" ? "chosen"
              : playstation                               ? "a PlayStation controller is connected"
                                                          : "no PlayStation controller",
              files, languages, ball == 2 ? "replaced" : "not replaced");
}
