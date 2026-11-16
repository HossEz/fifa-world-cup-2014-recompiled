#include "game_text.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <string>

namespace {

enum Kind { kPc, kButtons };
struct Phrase {
  const char* language;
  Kind kind;
  bool at_start;  // only where a line starts (Dutch writes a capital only there)
  const char* from;
  const char* to;
};

// Every phrase as it occurs in the nine language files of the five 2014 discs
// (Europe En/De/It and Fr/Es/Nl, USA En/Fr/Es, Japan, Brazil); a phrase that
// maps to itself keeps a line as it is ("Erfolge für die Xbox 360 Konsole":
// achievements).
constexpr Phrase kPhrases[] = {
#include "game_text_phrases.inc"
};

uint32_t Be32(const uint8_t* p) {
  return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
}
uint32_t Be16(const uint8_t* p) { return uint32_t(p[0]) << 8 | p[1]; }
void PutBe32(std::vector<uint8_t>& v, size_t at, uint32_t x) {
  v[at] = uint8_t(x >> 24), v[at + 1] = uint8_t(x >> 16), v[at + 2] = uint8_t(x >> 8), v[at + 3] = uint8_t(x);
}

// The file's checksums: CRC-32/MPEG-2 (MSB first, no final xor).
uint32_t Crc(const uint8_t* data, size_t size) {
  static const auto table = [] {
    std::array<uint32_t, 256> t{};
    for (uint32_t i = 0; i < 256; ++i) {
      uint32_t c = i << 24;
      for (int k = 0; k < 8; ++k) c = c & 0x80000000u ? (c << 1) ^ 0x04C11DB7u : c << 1;
      t[i] = c;
    }
    return t;
  }();
  uint32_t c = 0xFFFFFFFFu;
  for (size_t i = 0; i < size; ++i) c = (c << 8) ^ table[((c >> 24) ^ data[i]) & 0xFF];
  return c;
}

// The strings' Huffman code: a tree of nodes {next, char} x2 (left, right),
// next 0 meaning a leaf. A string is read MSB first and ends after its count of
// bytes; the game's encoder always adds a partly filled last byte.
class Huffman {
 public:
  Huffman(const uint8_t* tree, size_t size) : tree_(tree), size_(size) { Walk(0, {}); }

  // The `count` bytes at `bits` (at most `avail` bytes); returns the bytes used, 0 if it runs out.
  size_t Decode(const uint8_t* bits, size_t avail, size_t count, std::string& out) const {
    size_t bit = 0, node = 0;
    while (out.size() < count) {
      if (bit / 8 >= avail) return 0;
      size_t at = node * 4 + ((bits[bit / 8] >> (7 - bit % 8)) & 1) * 2;
      ++bit;
      if (at + 1 >= size_) return 0;
      if (tree_[at] == 0) {
        out.push_back(char(tree_[at + 1]));
        node = 0;
      } else {
        node = tree_[at];
      }
    }
    return bit / 8 + 1;
  }

  // Appends `s` coded; false if a character has no code in this file's tree.
  bool Encode(std::string_view s, std::vector<uint8_t>& out) const {
    std::vector<uint8_t> coded(1, 0);
    size_t bit = 0;
    for (unsigned char ch : s) {
      if (codes_[ch].empty()) return false;
      for (uint8_t b : codes_[ch]) {
        if (bit / 8 >= coded.size()) coded.push_back(0);
        coded[bit / 8] |= uint8_t(b << (7 - bit % 8));
        ++bit;
      }
    }
    coded.resize(bit / 8 + 1);
    out.insert(out.end(), coded.begin(), coded.end());
    return true;
  }

 private:
  void Walk(size_t node, std::vector<uint8_t> prefix) {
    if (prefix.size() > 255 || node * 4 + 3 >= size_) return;  // a broken tree
    for (uint8_t b = 0; b < 2; ++b) {
      uint8_t next = tree_[node * 4 + b * 2], ch = tree_[node * 4 + b * 2 + 1];
      std::vector<uint8_t> code = prefix;
      code.push_back(b);
      if (next == 0) {
        codes_[ch] = std::move(code);
      } else {
        Walk(next, std::move(code));
      }
    }
  }
  const uint8_t* tree_;
  size_t size_;
  std::array<std::vector<uint8_t>, 256> codes_;
};

// `text` with the phrases replaced: left to right, the longest phrase first (and
// of two as long, the one for the line's start).
std::string Reword(const std::string& text, const std::vector<const Phrase*>& phrases) {
  std::string out;
  size_t i = 0;
  while (i < text.size()) {
    const Phrase* hit = nullptr;
    for (const Phrase* p : phrases) {
      if ((!p->at_start || i == 0) && text.compare(i, std::strlen(p->from), p->from) == 0) {
        hit = p;
        break;
      }
    }
    if (hit) {
      out += hit->to;
      i += std::strlen(hit->from);
    } else {
      out += text[i++];
    }
  }
  return out;
}

}  // namespace

std::optional<std::vector<uint8_t>> RewordLanguageFile(std::string_view language,
                                                       std::span<const uint8_t> db,
                                                       bool playstation, int* changed) {
  *changed = 0;
  std::vector<const Phrase*> phrases;
  for (const Phrase& p : kPhrases) {
    if (language == p.language && (p.kind == kPc || playstation)) phrases.push_back(&p);
  }
  std::stable_sort(phrases.begin(), phrases.end(), [](const Phrase* a, const Phrase* b) {
    size_t la = std::strlen(a->from), lb = std::strlen(b->from);
    return la != lb ? la > lb : a->at_start > b->at_start;
  });
  // The layout: a 0x48-byte header, the table's fields to 0x78, n records of
  // {stringid offset, text offset, hash, 0}, the Huffman tree, the strings
  // (offsets from the tree), padding to 8, and a CRC of everything from 0x48.
  constexpr size_t kRecords = 0x78;
  constexpr uint32_t kNone = 0xFFFFFFFF;
  if (phrases.empty() || db.size() < kRecords || std::memcmp(db.data(), "DB\0\x08", 4) != 0) {
    return std::nullopt;
  }
  const uint8_t* d = db.data();
  const size_t n = Be16(d + 0x34), tree = kRecords + n * 16;
  if (tree >= db.size()) return std::nullopt;
  uint32_t first = kNone;  // the tree's size: the strings start after it
  for (size_t i = 0; i < n; ++i) {
    for (int k = 0; k < 2; ++k) {
      uint32_t off = Be32(d + kRecords + i * 16 + k * 4);
      if (off != kNone) first = std::min(first, off);
    }
  }
  if (first == kNone || tree + first > db.size()) return std::nullopt;
  const Huffman huffman(d + tree, first);

  std::vector<uint8_t> records(d + kRecords, d + tree), strings;
  for (size_t i = 0; i < n; ++i) {
    for (int k = 0; k < 2; ++k) {  // the stringid (1-byte count), the text (2-byte count)
      const uint32_t off = Be32(d + kRecords + i * 16 + k * 4);
      if (off == kNone) continue;
      const size_t at = tree + off, count_bytes = k == 0 ? 1 : 2;
      if (at + count_bytes > db.size()) return std::nullopt;
      const size_t count = k == 0 ? d[at] : Be16(d + at);
      std::string text;
      size_t used = huffman.Decode(d + at + count_bytes, db.size() - at - count_bytes, count, text);
      if (!used) return std::nullopt;
      PutBe32(records, i * 16 + k * 4, first + uint32_t(strings.size()));
      std::string new_text = k == 1 ? Reword(text, phrases) : text;
      if (new_text != text && new_text.size() <= 0xFFFF) {
        size_t start = strings.size();
        strings.push_back(uint8_t(new_text.size() >> 8));
        strings.push_back(uint8_t(new_text.size()));
        if (huffman.Encode(new_text, strings)) {
          ++*changed;
          continue;
        }
        strings.resize(start);  // a letter this file can't write: the line stays
      }
      strings.insert(strings.end(), d + at, d + at + count_bytes + used);
    }
  }
  if (*changed == 0) return std::nullopt;

  std::vector<uint8_t> out(d, d + 0x48);
  out.insert(out.end(), d + 0x48, d + kRecords);
  out.insert(out.end(), records.begin(), records.end());
  out.insert(out.end(), d + tree, d + tree + first);
  out.insert(out.end(), strings.begin(), strings.end());
  out.resize((out.size() + 7) & ~size_t(7));
  out.resize(out.size() + 4);
  PutBe32(out, 0x08, uint32_t(out.size()));
  PutBe32(out, 0x30, first + uint32_t(strings.size()));
  PutBe32(out, 0x14, Crc(out.data(), 0x14));
  PutBe32(out, 0x44, Crc(out.data() + 0x24, 0x20));
  PutBe32(out, out.size() - 4, Crc(out.data() + 0x48, out.size() - 4 - 0x48));
  return out;
}
