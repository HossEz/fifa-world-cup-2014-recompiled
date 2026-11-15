#include "squads_import.h"

#include <windows.h>
#include <shobjidl.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "i18n.h"
#include "watchdog.h"

namespace launcher {

namespace fs = std::filesystem;
using Microsoft::WRL::ComPtr;

namespace {

constexpr uint32_t kTitleId = 0x454109D4;            // 2014 FIFA World Cup Brazil
constexpr uint64_t kProfile = 0xB13EBABEBABEBABEull;  // the SDK's one profile
constexpr std::string_view kSquadsType("SaveType_Squads\0", 16);
constexpr size_t kMaxFile = 32 * 1024 * 1024;

uint32_t Be32(const std::vector<uint8_t>& d, size_t at) {
  return uint32_t(d[at]) << 24 | uint32_t(d[at + 1]) << 16 | uint32_t(d[at + 2]) << 8 | d[at + 3];
}
uint32_t Le24(const std::vector<uint8_t>& d, size_t at) {
  return d[at] | uint32_t(d[at + 1]) << 8 | uint32_t(d[at + 2]) << 16;
}

uint32_t Crc32(const uint8_t* data, size_t size) {
  static const std::array<uint32_t, 256> table = [] {
    std::array<uint32_t, 256> t{};
    for (uint32_t i = 0; i < 256; ++i) {
      uint32_t c = i;
      for (int k = 0; k < 8; ++k) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
      t[i] = c;
    }
    return t;
  }();
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = 0; i < size; ++i) crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
  return ~crc;
}

// A squads save as the game writes it: its type, a CRC32 of everything from
// 0x1C, then the squad database. This game's (not FIFA 14's or 15's, which use
// the same type) has format 0x23 and the players table ("rkmV") first.
bool IsSquadsSave(const std::vector<uint8_t>& save) {
  return save.size() > 0x40 && std::memcmp(save.data(), kSquadsType.data(), 16) == 0 &&
         Be32(save, 0x10) == Crc32(save.data() + 0x1C, save.size() - 0x1C) &&
         Be32(save, 0x14) == 0x23 && std::memcmp(save.data() + 0x34, "rkmV", 4) == 0;
}

// The squads save in an Xbox 360 save package (STFS), block addresses as in
// Xenia's StfsContainerDevice. nullopt if it isn't one of this game's packages.
// shortcut: only files stored in consecutive blocks (every squads package seen
// is); a fragmented one fails the CRC check and is refused.
std::optional<std::vector<uint8_t>> FromPackage(const std::vector<uint8_t>& d, bool* other_game) {
  if (d.size() < 0xC000) return std::nullopt;
  if (Be32(d, 0x360) != kTitleId) {
    *other_game = true;
    return std::nullopt;
  }
  const uint32_t header_size = Be32(d, 0x340);
  const size_t vd = 0x379;  // volume descriptor
  const bool read_only = d[vd + 2] & 1;
  const uint32_t shift = ((header_size + 0xFFF) & 0xB000) == 0xB000 || read_only ? 0 : 1;
  auto offset = [&](uint64_t block) {
    uint64_t at = block + (((block + 0xAA) / 0xAA) << shift);
    if (block >= 0xAA) {
      at += ((block + 0x70E4) / 0x70E4) << shift;
      if (block >= 0x70E4) at += ((block + 0x4AF768) / 0x4AF768) << shift;
    }
    return ((uint64_t(header_size) + 0xFFF) & ~0xFFFull) + (at << 12);
  };
  auto read_blocks = [&](uint32_t start, uint32_t count, size_t size) -> std::optional<std::vector<uint8_t>> {
    std::vector<uint8_t> out;
    for (uint32_t i = 0; i < count && out.size() < size; ++i) {
      uint64_t at = offset(start + i);
      if (at + 0x1000 > d.size()) return std::nullopt;
      out.insert(out.end(), d.begin() + at, d.begin() + at + 0x1000);
    }
    if (out.size() < size) return std::nullopt;
    out.resize(size);
    return out;
  };
  const uint32_t table_blocks = d[vd + 3] | d[vd + 4] << 8;
  auto table = read_blocks(Le24(d, vd + 5), table_blocks, size_t(table_blocks) * 0x1000);
  if (!table) return std::nullopt;
  for (size_t e = 0; e + 0x40 <= table->size(); e += 0x40) {
    const uint8_t flags = (*table)[e + 0x28];
    if ((flags & 0x3F) == 0) break;  // no name: the end of the table
    if (flags & 0x80) continue;      // a folder
    auto data = read_blocks(Le24(*table, e + 0x2F), Le24(*table, e + 0x29), Be32(*table, e + 0x34));
    if (data && IsSquadsSave(*data)) return data;
  }
  return std::nullopt;
}

// "Squads 20261115174500", the game's own naming, with the time of the import.
std::string NameNow() {
  SYSTEMTIME t;
  GetLocalTime(&t);
  char name[32];
  std::snprintf(name, sizeof(name), "Squads %04u%02u%02u%02u%02u%02u", t.wYear, t.wMonth, t.wDay,
                t.wHour, t.wMinute, t.wSecond);
  return name;
}

// The SDK's XCONTENT_AGGREGATE_DATA (328 bytes): device, content type (saved
// game), display name (UTF-16BE), file name, profile, title.
std::vector<uint8_t> Header(const std::string& name) {
  std::vector<uint8_t> h(0x148, 0);
  auto be = [&](size_t at, uint64_t value, int bytes) {
    for (int i = 0; i < bytes; ++i) h[at + i] = uint8_t(value >> (8 * (bytes - 1 - i)));
  };
  be(0, 1, 4);
  be(4, 1, 4);
  const char* shown = "Imported squads";
  for (size_t i = 0; shown[i]; ++i) be(8 + 2 * i, uint8_t(shown[i]), 2);
  std::memcpy(&h[0x108], name.data(), name.size());
  be(0x138, kProfile, 8);
  be(0x140, kTitleId, 4);
  return h;
}

// The saves an earlier import wrote: their header still has its display name
// (the game writes its own when the player saves over one, and then it's theirs).
std::vector<std::string> EarlierImports(const fs::path& title, const std::string& keep) {
  const std::vector<uint8_t> ours = Header("");
  std::vector<std::string> names;
  std::error_code ec;
  for (const auto& entry : fs::directory_iterator(title / "Headers" / "00000001", ec)) {
    std::u8string utf8 = entry.path().stem().u8string();
    std::string name(utf8.begin(), utf8.end());
    if (entry.path().extension() != ".header" || name == keep || name.rfind("Squads ", 0) != 0) {
      continue;
    }
    std::ifstream file(entry.path(), std::ios::binary);
    std::vector<uint8_t> header(0x108);
    if (file.read(reinterpret_cast<char*>(header.data()), std::streamsize(header.size())) &&
        std::equal(header.begin(), header.end(), ours.begin())) {
      names.push_back(name);
    }
  }
  return names;
}

bool WriteFile(const fs::path& path, const std::vector<uint8_t>& data) {
  std::error_code ec;
  fs::create_directories(path.parent_path(), ec);
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  file.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
  return bool(file);
}

fs::path PickFile() {
  ComPtr<IFileOpenDialog> dialog;
  if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                              IID_PPV_ARGS(&dialog)))) {
    return {};
  }
  DWORD options = 0;
  dialog->GetOptions(&options);
  dialog->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST);
  dialog->SetTitle(fs::path(reinterpret_cast<const char8_t*>(T("The squads file"))).c_str());
  ComPtr<IShellItem> item;
  PWSTR picked = nullptr;
  WatchdogPause();
  if (FAILED(dialog->Show(GetActiveWindow())) || FAILED(dialog->GetResult(&item)) ||
      FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &picked))) {
    return {};
  }
  fs::path file = picked;
  CoTaskMemFree(picked);
  return file;
}

}  // namespace

SquadsImport PickAndImportSquads(const fs::path& user_folder) {
  fs::path file = PickFile();
  if (file.empty()) return SquadsImport::kCancelled;

  std::error_code ec;
  uintmax_t size = fs::file_size(file, ec);
  if (ec || size > kMaxFile) return SquadsImport::kNotSquads;
  std::vector<uint8_t> bytes(size);
  std::ifstream in(file, std::ios::binary);
  if (!in.read(reinterpret_cast<char*>(bytes.data()), std::streamsize(size))) {
    return SquadsImport::kNotSquads;
  }

  std::optional<std::vector<uint8_t>> save;
  if (size >= 4 && std::memcmp(bytes.data(), "CON ", 4) == 0) {
    bool other_game = false;
    save = FromPackage(bytes, &other_game);
    if (!save) return other_game ? SquadsImport::kOtherGame : SquadsImport::kNotSquads;
  } else if (IsSquadsSave(bytes)) {
    save = std::move(bytes);
  } else {
    return std::memcmp(bytes.data(), kSquadsType.data(), std::min<size_t>(16, size)) == 0
               ? SquadsImport::kOtherGame
               : SquadsImport::kNotSquads;
  }

  // <saves>\<profile>\<title>\00000001\<name>\<name>, its header in Headers\00000001.
  std::string name = NameNow();
  fs::path title = user_folder / "B13EBABEBABEBABE" / "454109D4";
  bool ok = WriteFile(title / "00000001" / name / name, *save) &&
            WriteFile(title / "Headers" / "00000001" / (name + ".header"), Header(name));
  if (!ok) return SquadsImport::kWriteFailed;
  // One imported squads save at a time, so there's no doubt which one the game loads.
  for (const std::string& old : EarlierImports(title, name)) {
    fs::remove_all(title / "00000001" / old, ec);
    fs::remove(title / "Headers" / "00000001" / (old + ".header"), ec);
  }
  return SquadsImport::kOk;
}

}  // namespace launcher
