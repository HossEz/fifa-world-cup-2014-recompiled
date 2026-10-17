// Reading the game's own files: the .big archives under the game folder and
// the EA chunked compression their entries use.
//
// BIG archives ("BIG4", and "BIGF" for small archives nested in entries),
// big-endian: magic, u32 total size (little-endian, unused), u32 entry count,
// u32 header size, then per entry u32 offset, u32 size and a NUL-terminated
// path. Entries are often compressed: "chunkzip" (raw deflate chunks) or
// "chunklzx" (LZX chunks in XMemCompress framing). Indexing all 19 archives of
// the game takes well under 0.1 s, so nothing is cached between runs.

#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace launcher {

class GameSource;

class GameFiles {
 public:
  // Indexes every .big archive in the game's folder or disc image (which must
  // outlive this). A path in several archives resolves to the last one in
  // sorted order.
  explicit GameFiles(const GameSource& source);

  size_t entry_count() const { return index_.size(); }

  // An entry's contents, decompressed. Paths are case-insensitive, with
  // forward slashes ("data/ui/fonts/futurastd-bold.ttf").
  std::optional<std::vector<uint8_t>> Read(const std::string& path) const;

  // Part of an entry as stored (no decompression), for large uncompressed
  // entries such as music banks. Shorter at the entry's end; empty if missing.
  std::vector<uint8_t> ReadRaw(const std::string& path, uint64_t offset, uint32_t size) const;

 private:
  struct Location {
    uint32_t archive;
    uint32_t offset;
    uint32_t size;
  };
  const GameSource& source_;
  std::vector<std::string> archives_;
  std::unordered_map<std::string, Location> index_;
};

// "chunkzip" / "chunklzx" contents decompressed; other data is returned as is.
// Empty on a damaged entry.
std::vector<uint8_t> DecompressChunked(std::vector<uint8_t> data);

}  // namespace launcher
