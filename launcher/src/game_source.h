// Where the game's files come from: an extracted folder (default.xex next to
// the game's data) or a disc image (.iso). Everything that reads the game
// (archives, sound banks, the game check) goes through this.
//
// An Xbox 360 disc image holds the game in Microsoft's XDVDFS file system: a
// volume descriptor ("MICROSOFT*XBOX*MEDIA") at sector 32 of the game
// partition, then directories as binary trees of entries. The game partition
// starts at a different place depending on how the disc was imaged: 0 for a
// trimmed game-only image (xiso), 0x2080000 for an XGD3 disc (2014 FIFA World
// Cup Brazil is XGD3), 0xFD90000 for XGD2, 0x18300000 for XGD1.

#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace launcher {

class GameSource {
 public:
  virtual ~GameSource() = default;
  // Every file, as lower-case paths with forward slashes ("data/ui/main.big").
  virtual const std::vector<std::string>& Files() const = 0;
  // A file's size, or -1 if there's no such file (path as in Files()).
  virtual int64_t Size(const std::string& path) const = 0;
  // Up to `size` bytes from `offset`; fewer at the file's end, empty if missing.
  virtual std::vector<uint8_t> Read(const std::string& path, uint64_t offset,
                                    uint32_t size) const = 0;
  // A whole file (empty if missing).
  std::vector<uint8_t> ReadAll(const std::string& path) const;
  // The folder or .iso this reads.
  virtual const std::filesystem::path& location() const = 0;
};

// A folder or an .iso file. Null if it's neither (or the image isn't XDVDFS).
std::unique_ptr<GameSource> OpenGameSource(const std::filesystem::path& location);

}  // namespace launcher
