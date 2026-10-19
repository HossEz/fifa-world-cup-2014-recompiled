#include "game_files.h"

#include "game_source.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>

// stb_image for its raw-deflate decoder (only the PNG loader, which needs it).
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include <stb_image.h>

extern "C" {
#include <lzx.h>
#include <mspack.h>
}

namespace launcher {

namespace {

uint32_t ReadBe32(const uint8_t* p) {
  return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
}

std::string Lower(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(),
                 [](unsigned char c) { return char(std::tolower(c)); });
  return text;
}

// ------------------------------------------------------------------ LZX

// libmspack reads and writes through an mspack_system; these "files" are
// memory buffers.
struct MemoryFile {
  const uint8_t* data;
  uint8_t* out;
  size_t size;
  size_t position;
};

int MemRead(mspack_file* file, void* buffer, int bytes) {
  auto* f = reinterpret_cast<MemoryFile*>(file);
  size_t n = std::min(size_t(bytes), f->size - f->position);
  std::memcpy(buffer, f->data + f->position, n);
  f->position += n;
  return int(n);
}

int MemWrite(mspack_file* file, void* buffer, int bytes) {
  auto* f = reinterpret_cast<MemoryFile*>(file);
  size_t n = std::min(size_t(bytes), f->size - f->position);
  std::memcpy(f->out + f->position, buffer, n);
  f->position += n;
  return int(n);
}

void* MemAlloc(mspack_system*, size_t bytes) { return std::malloc(bytes); }
void MemFree(void* p) { std::free(p); }
void MemCopy(void* src, void* dst, size_t bytes) { std::memmove(dst, src, bytes); }
void MemMessage(mspack_file*, const char*, ...) {}

mspack_system MemorySystem() {
  mspack_system sys = {};
  sys.read = MemRead;
  sys.write = MemWrite;
  sys.alloc = MemAlloc;
  sys.free = MemFree;
  sys.copy = MemCopy;
  sys.message = MemMessage;
  return sys;
}

// XMemCompress framing: each 32 KB output frame's compressed data is prefixed
// with a big-endian u16 size, or with 0xFF, u16 output size, u16 size for a
// shorter (last) frame; a zero size ends the stream. libmspack wants the bare
// LZX bitstream.
std::vector<uint8_t> Deframe(const uint8_t* data, size_t size) {
  std::vector<uint8_t> out;
  size_t pos = 0;
  while (pos + 2 <= size) {
    uint32_t length;
    if (data[pos] == 0xFF) {
      if (pos + 5 > size) break;
      length = uint32_t(data[pos + 3]) << 8 | data[pos + 4];
      pos += 5;
    } else {
      length = uint32_t(data[pos]) << 8 | data[pos + 1];
      pos += 2;
    }
    if (!length || pos + length > size) break;
    out.insert(out.end(), data + pos, data + pos + length);
    pos += length;
  }
  return out;
}

bool DecompressLzx(const uint8_t* data, size_t size, uint8_t* out, size_t out_size) {
  std::vector<uint8_t> stream = Deframe(data, size);
  mspack_system sys = MemorySystem();
  MemoryFile input = {stream.data(), nullptr, stream.size(), 0};
  MemoryFile output = {nullptr, out, out_size, 0};
  // 128 KB window (17 bits), 32 KB frames, no reset interval.
  lzxd_stream* lzx = lzxd_init(&sys, reinterpret_cast<mspack_file*>(&input),
                               reinterpret_cast<mspack_file*>(&output), 17, 0, 0x8000,
                               off_t(out_size), 0);
  if (!lzx) return false;
  int result = lzxd_decompress(lzx, off_t(out_size));
  lzxd_free(lzx);
  return result == MSPACK_ERR_OK && output.position == out_size;
}

}  // namespace

// ------------------------------------------------------------------ chunked

std::vector<uint8_t> DecompressChunked(std::vector<uint8_t> data) {
  // Header (big-endian), 0x28 bytes: magic, u32 version, u32 decompressed
  // size, u32 chunk size, u32 chunk count, u32 alignment. Each chunk: u32
  // compressed size, u32 method, data; chunk data starts on an alignment
  // boundary. Every chunk is an independent stream.
  bool zip = data.size() >= 8 && std::memcmp(data.data(), "chunkzip", 8) == 0;
  bool lzx = data.size() >= 8 && std::memcmp(data.data(), "chunklzx", 8) == 0;
  if (!zip && !lzx) return data;
  if (data.size() < 0x28) return {};
  uint32_t size = ReadBe32(&data[12]);
  uint32_t chunk_size = ReadBe32(&data[16]);
  uint32_t count = ReadBe32(&data[20]);
  uint32_t alignment = ReadBe32(&data[24]);
  std::vector<uint8_t> out(size);
  size_t written = 0;
  size_t pos = 0x28;
  for (uint32_t i = 0; i < count; ++i) {
    if (pos + 8 > data.size()) return {};
    uint32_t compressed = ReadBe32(&data[pos]);
    const uint8_t* chunk = &data[pos + 8];
    if (pos + 8 + compressed > data.size()) return {};
    size_t expected = std::min<size_t>(chunk_size, size - written);
    if (zip) {
      int n = stbi_zlib_decode_noheader_buffer(reinterpret_cast<char*>(&out[written]),
                                               int(expected),
                                               reinterpret_cast<const char*>(chunk),
                                               int(compressed));
      if (n != int(expected)) return {};
    } else if (!DecompressLzx(chunk, compressed, &out[written], expected)) {
      return {};
    }
    written += expected;
    pos += 8 + compressed;
    if (alignment > 1) {
      pos = (pos + 8 + alignment - 1) / alignment * alignment - 8;
    }
  }
  return written == size ? out : std::vector<uint8_t>{};
}

// ------------------------------------------------------------------ archives

GameFiles::GameFiles(const GameSource& source) : source_(source) {
  for (const std::string& path : source.Files()) {  // already sorted
    if (path.size() < 4 || path.compare(path.size() - 4, 4, ".big")) continue;
    std::vector<uint8_t> head = source.Read(path, 0, 16);
    if (head.size() < 16) continue;
    if (std::memcmp(head.data(), "BIG4", 4) && std::memcmp(head.data(), "BIGF", 4)) continue;
    uint32_t count = ReadBe32(&head[8]);
    uint32_t header_size = ReadBe32(&head[12]);
    std::vector<uint8_t> header = source.Read(path, 16, header_size);
    if (header.size() < header_size) continue;
    uint32_t archive = uint32_t(archives_.size());
    archives_.push_back(path);
    // The entry table follows the 16 bytes already read.
    size_t pos = 0;
    for (uint32_t i = 0; i < count && pos + 8 < header.size(); ++i) {
      uint32_t offset = ReadBe32(&header[pos]);
      uint32_t size = ReadBe32(&header[pos + 4]);
      auto name_start = header.begin() + pos + 8;
      auto name_end = std::find(name_start, header.end(), uint8_t(0));
      index_[Lower(std::string(name_start, name_end))] = {archive, offset, size};
      pos = size_t(name_end - header.begin()) + 1;
    }
  }
}

std::optional<std::vector<uint8_t>> GameFiles::Read(const std::string& path) const {
  auto found = index_.find(Lower(path));
  if (found == index_.end()) return std::nullopt;
  const Location& location = found->second;
  std::vector<uint8_t> data = source_.Read(archives_[location.archive], location.offset,
                                           location.size);
  if (data.size() < location.size) return std::nullopt;
  data = DecompressChunked(std::move(data));
  if (data.empty()) return std::nullopt;
  return data;
}

namespace {

// The first entry of an in-memory BIG archive that `match(name, data, size)` accepts.
template <typename Match>
std::optional<std::vector<uint8_t>> FindNested(const std::vector<uint8_t>& archive, Match match) {
  if (archive.size() < 16 ||
      (std::memcmp(archive.data(), "BIG4", 4) && std::memcmp(archive.data(), "BIGF", 4))) {
    return std::nullopt;
  }
  uint32_t count = ReadBe32(&archive[8]);
  size_t header_end = std::min(archive.size(), size_t(16) + ReadBe32(&archive[12]));
  size_t pos = 16;
  for (uint32_t i = 0; i < count && pos + 8 < header_end; ++i) {
    uint32_t offset = ReadBe32(&archive[pos]);
    uint32_t size = ReadBe32(&archive[pos + 4]);
    auto name_start = archive.begin() + pos + 8;
    auto name_end = std::find(name_start, archive.begin() + header_end, uint8_t(0));
    pos = size_t(name_end - archive.begin()) + 1;
    if (size_t(offset) + size > archive.size()) continue;
    if (match(std::string(name_start, name_end), &archive[offset], size)) {
      return std::vector<uint8_t>(archive.begin() + offset, archive.begin() + offset + size);
    }
  }
  return std::nullopt;
}

}  // namespace

std::optional<std::vector<uint8_t>> ReadNested(const std::vector<uint8_t>& archive,
                                               const std::string& name) {
  std::string wanted = Lower(name);
  return FindNested(archive, [&](const std::string& entry, const uint8_t*, uint32_t) {
    return Lower(entry) == wanted;
  });
}

std::optional<std::vector<uint8_t>> ReadNestedByMagic(const std::vector<uint8_t>& archive,
                                                      const char* magic) {
  return FindNested(archive, [&](const std::string&, const uint8_t* data, uint32_t size) {
    return size >= 4 && std::memcmp(data, magic, 4) == 0;
  });
}

std::vector<uint8_t> GameFiles::ReadRaw(const std::string& path, uint64_t offset,
                                        uint32_t size) const {
  auto found = index_.find(Lower(path));
  if (found == index_.end() || offset >= found->second.size) return {};
  const Location& location = found->second;
  size = uint32_t(std::min<uint64_t>(size, location.size - offset));
  std::vector<uint8_t> data = source_.Read(archives_[location.archive], location.offset + offset,
                                           size);
  if (data.size() < size) return {};
  return data;
}

}  // namespace launcher
