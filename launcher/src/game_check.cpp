#include "game_check.h"

#include <windows.h>
#include <bcrypt.h>

#include <cstdio>
#include <cstring>
#include <vector>

#include "game_source.h"

namespace launcher {

namespace {

constexpr uint32_t kMediaId = 0x5D31A745;

struct Fingerprint {
  const char* path;
  const char* sha256;
};
constexpr Fingerprint kFingerprints[] = {
    {"default.xex", "cb3947735a6f28579868151b040876ad75133b9688527eec65d3d529ed24e2bf"},
    {"dlc/dlc_footballcompeng/dlc/footballcompeng/footballcompengzf.xex.dll",
     "b5ea8f262b47cb2523e84d7c9f33744c4128860945771c54264f0eb119ed5c85"},
    {"dlc/dlc_powdll/dlc/powdll/powdllzf.xex.dll",
     "c34cedd51b829b1ed6fa6eaffdaeb59436c912e0635afcb90d4a58609e9e480d"},
};

uint32_t Be32(const uint8_t* p) {
  return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | uint32_t(p[3]);
}

std::string Sha256(const std::vector<uint8_t>& data) {
  BCRYPT_ALG_HANDLE algorithm = nullptr;
  uint8_t digest[32] = {};
  if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) {
    return {};
  }
  NTSTATUS status = BCryptHash(algorithm, nullptr, 0, const_cast<PUCHAR>(data.data()),
                               ULONG(data.size()), digest, sizeof(digest));
  BCryptCloseAlgorithmProvider(algorithm, 0);
  if (status != 0) return {};
  std::string hex;
  char byte[3];
  for (uint8_t b : digest) {
    std::snprintf(byte, sizeof(byte), "%02x", b);
    hex += byte;
  }
  return hex;
}

}  // namespace

GameCheck CheckGame(const GameSource& source) {
  GameCheck check;
  std::vector<uint8_t> xex = source.ReadAll("default.xex");
  if (xex.size() < 0x18 || std::memcmp(xex.data(), "XEX2", 4)) return check;

  // Optional headers from 0x18: u32 key, u32 value or offset. Keys whose low
  // byte is 0 or 1 hold the value itself; others point at their data.
  uint32_t count = Be32(&xex[0x14]);
  const uint8_t* info = nullptr;
  for (uint32_t i = 0; i < count && 0x18 + i * 8 + 8 <= xex.size(); ++i) {
    const uint8_t* header = &xex[0x18 + i * 8];
    uint32_t offset = Be32(header + 4);
    if (Be32(header) == 0x00040006 && offset + 24 <= xex.size()) info = &xex[offset];
  }
  if (!info) return check;
  check.media_id = Be32(info);
  uint32_t version = Be32(info + 4);
  check.title_id = Be32(info + 12);
  check.disc = info[18];
  check.discs = info[19];
  char text[32];
  std::snprintf(text, sizeof(text), "%u.%u.%u.%u", version >> 28, (version >> 24) & 0xF,
                (version >> 8) & 0xFFFF, version & 0xFF);
  check.version = text;

  if (check.title_id != kGameTitleId) {
    check.result = GameCheck::Result::kOtherGame;
    return check;
  }
  if (check.version != kGameVersion || check.media_id != kMediaId) {
    check.result = GameCheck::Result::kOtherVersion;
    return check;
  }
  for (const Fingerprint& fingerprint : kFingerprints) {
    std::vector<uint8_t> data = fingerprint.path == std::string("default.xex")
                                    ? std::move(xex)
                                    : source.ReadAll(fingerprint.path);
    check.file = fingerprint.path;
    if (data.empty()) {
      check.result = GameCheck::Result::kIncomplete;
      return check;
    }
    if (Sha256(data) != fingerprint.sha256) {
      check.result = GameCheck::Result::kChanged;
      return check;
    }
  }
  check.file.clear();
  check.result = GameCheck::Result::kFound;
  return check;
}

}  // namespace launcher
