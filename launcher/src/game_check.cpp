#include "game_check.h"

#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <optional>
#include <vector>

#include "game_source.h"

namespace launcher {

namespace {

// The executables and the SHA-256 of their unpacked code (as tools/xex_extract.py
// writes it). The same on the European (0.0.0.13) and the US (0.0.0.17) disc.
struct CodeImage {
  const char* path;
  const char* sha256;
};
constexpr CodeImage kCodeImages[] = {
    {"default.xex", "ecfd92d0190656c5524edd76f42411edbf747a5e00f12d864d509d36d063ce89"},
    {"dlc/dlc_footballcompeng/dlc/footballcompeng/footballcompengzf.xex.dll",
     "0fa505950ebe9c92db966b9990740f650fc80c1c0241754dcf302b8de85359b7"},
    {"dlc/dlc_powdll/dlc/powdll/powdllzf.xex.dll",
     "6feab7629ac4f75c7d34524d3b48471a8b80873d375a0b2562ee5268748bccb0"},
};

// Releases known to work, by media ID; only for showing which one it is.
struct KnownRelease {
  uint32_t media_id;
  const char* region;
};
constexpr KnownRelease kKnownReleases[] = {
    {0x5D31A745, "Europe"},
    {0x290A9ADA, "USA"},
};

// The retail XEX key: decrypts each file's own AES key.
constexpr uint8_t kRetailKey[16] = {0x20, 0xB1, 0x85, 0xA5, 0x9D, 0x28, 0xFD, 0xC3,
                                    0x40, 0x58, 0x3F, 0xBB, 0x08, 0x96, 0xBF, 0x91};

constexpr uint32_t kHeaderFileFormat = 0x000003FF;
constexpr uint32_t kHeaderExecutionInfo = 0x00040006;

uint32_t Be32(const uint8_t* p) {
  return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | uint32_t(p[3]);
}
uint16_t Be16(const uint8_t* p) { return uint16_t(p[0] << 8 | p[1]); }

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

// AES-128 decryption, ECB or CBC with a zero IV; `data` is a whole number of
// blocks. False on failure.
bool AesDecrypt(const uint8_t key[16], const wchar_t* mode, const uint8_t* data, size_t size,
                uint8_t* out) {
  BCRYPT_ALG_HANDLE algorithm = nullptr;
  BCRYPT_KEY_HANDLE handle = nullptr;
  bool ok = false;
  if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_AES_ALGORITHM, nullptr, 0) == 0 &&
      BCryptSetProperty(algorithm, BCRYPT_CHAINING_MODE, (PUCHAR)mode,
                        ULONG((wcslen(mode) + 1) * sizeof(wchar_t)), 0) == 0 &&
      BCryptGenerateSymmetricKey(algorithm, &handle, nullptr, 0, const_cast<PUCHAR>(key), 16,
                                 0) == 0) {
    uint8_t iv[16] = {};
    bool cbc = wcscmp(mode, BCRYPT_CHAIN_MODE_CBC) == 0;
    ULONG written = 0;
    ok = BCryptDecrypt(handle, const_cast<PUCHAR>(data), ULONG(size), nullptr,
                       cbc ? iv : nullptr, cbc ? sizeof(iv) : 0, out, ULONG(size), &written,
                       0) == 0 &&
         written == size;
  }
  if (handle) BCryptDestroyKey(handle);
  if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
  return ok;
}

// An optional header's value or offset; nothing if absent.
std::optional<uint32_t> OptionalHeader(const std::vector<uint8_t>& xex, uint32_t key) {
  uint32_t count = Be32(&xex[0x14]);
  for (uint32_t i = 0; i < count && 0x18 + size_t(i) * 8 + 8 <= xex.size(); ++i) {
    const uint8_t* header = &xex[0x18 + i * 8];
    if (Be32(header) == key) return Be32(header + 4);
  }
  return std::nullopt;
}

// The executable's code image as the console loads it: the payload decrypted
// (AES-CBC with the file's key) and its basic compression expanded (data
// blocks, each followed by zeros). The same as tools/xex_extract.py. Empty if
// it isn't a format this handles (LZX compression, another key).
std::vector<uint8_t> UnpackXex(const std::vector<uint8_t>& xex) {
  if (xex.size() < 0x18 || std::memcmp(xex.data(), "XEX2", 4)) return {};
  uint32_t header_size = Be32(&xex[0x08]);
  uint32_t security = Be32(&xex[0x10]);
  std::optional<uint32_t> format = OptionalHeader(xex, kHeaderFileFormat);
  if (!format || size_t(*format) + 8 > xex.size() || header_size > xex.size() ||
      size_t(security) + 0x160 > xex.size()) {
    return {};
  }
  uint32_t info_size = Be32(&xex[*format]);
  uint16_t encryption = Be16(&xex[*format + 4]);
  uint16_t compression = Be16(&xex[*format + 6]);
  uint32_t image_size = Be32(&xex[security + 4]);
  if (info_size < 8 || size_t(*format) + info_size > xex.size() || image_size > (256u << 20)) {
    return {};
  }

  std::vector<uint8_t> payload(xex.begin() + header_size, xex.end());
  if (encryption == 1) {
    uint8_t session_key[16];
    if (!AesDecrypt(kRetailKey, BCRYPT_CHAIN_MODE_ECB, &xex[security + 0x150], 16,
                    session_key)) {
      return {};
    }
    // One CBC stream over all data blocks back to back.
    payload.resize(payload.size() - payload.size() % 16);
    std::vector<uint8_t> plain(payload.size());
    if (!AesDecrypt(session_key, BCRYPT_CHAIN_MODE_CBC, payload.data(), payload.size(),
                    plain.data())) {
      return {};
    }
    payload = std::move(plain);
  } else if (encryption != 0) {
    return {};
  }

  std::vector<uint8_t> image;
  if (compression == 0) {
    image.assign(payload.begin(), payload.begin() + std::min<size_t>(payload.size(), image_size));
  } else if (compression == 1) {
    size_t source = 0;
    for (uint32_t i = 0; i < (info_size - 8) / 8; ++i) {
      uint32_t data_size = Be32(&xex[*format + 8 + i * 8]);
      uint32_t zero_size = Be32(&xex[*format + 12 + i * 8]);
      if (source + data_size > payload.size() ||
          image.size() + size_t(data_size) + zero_size > image_size) {
        return {};
      }
      image.insert(image.end(), payload.begin() + source, payload.begin() + source + data_size);
      image.resize(image.size() + zero_size);
      source += data_size;
    }
  } else {
    return {};
  }
  if (image.size() < image_size) image.resize(image_size);
  if (image.size() < 2 || image[0] != 'M' || image[1] != 'Z') return {};
  return image;
}

}  // namespace

GameCheck CheckGame(const GameSource& source) {
  GameCheck check;
  std::vector<uint8_t> xex = source.ReadAll("default.xex");
  if (xex.size() < 0x18 || std::memcmp(xex.data(), "XEX2", 4)) return check;

  std::optional<uint32_t> offset = OptionalHeader(xex, kHeaderExecutionInfo);
  if (!offset || size_t(*offset) + 24 > xex.size()) return check;
  const uint8_t* info = &xex[*offset];
  check.media_id = Be32(info);
  uint32_t version = Be32(info + 4);
  check.title_id = Be32(info + 12);
  check.disc = info[18];
  check.discs = info[19];
  char text[32];
  std::snprintf(text, sizeof(text), "%u.%u.%u.%u", version >> 28, (version >> 24) & 0xF,
                (version >> 8) & 0xFFFF, version & 0xFF);
  check.version = text;
  for (const KnownRelease& release : kKnownReleases) {
    if (release.media_id == check.media_id) check.region = release.region;
  }

  if (check.title_id != kGameTitleId) {
    check.result = GameCheck::Result::kOtherGame;
    return check;
  }
  for (const CodeImage& code : kCodeImages) {
    std::vector<uint8_t> data = code.path == std::string("default.xex") ? std::move(xex)
                                                                       : source.ReadAll(code.path);
    check.file = code.path;
    if (data.empty()) {
      check.result = GameCheck::Result::kIncomplete;
      return check;
    }
    if (Sha256(UnpackXex(data)) != code.sha256) {
      check.result = GameCheck::Result::kOtherCode;
      return check;
    }
  }
  check.file.clear();
  check.result = GameCheck::Result::kFound;
  return check;
}

}  // namespace launcher
