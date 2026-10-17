#include "icon.h"

#include <windows.h>
#include <shlobj.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <vector>

#include "game_files.h"
#include "log.h"
#include "textures.h"

namespace launcher {

namespace fs = std::filesystem;

namespace {

// The game's cover tile: the EA Sports logo over "2014 FIFA World Cup Brazil",
// 128x128, filling its square (the player's pick, 2026-10-16).
constexpr const char* kPicture = "data/ui/imgassets/custompanels/custompanels_77.dds";
// Up to the picture's own size: Windows scales 128 for its larger views itself.
constexpr int kSizes[] = {16, 20, 24, 32, 40, 48, 64, 96, 128};

struct Image {
  int width = 0, height = 0;
  std::vector<uint8_t> rgba;
  const uint8_t* at(int x, int y) const { return &rgba[(size_t(y) * width + x) * 4]; }
};

// The part with alpha in [x0, x1) x [y0, y1).
Image Crop(const Image& image, int x0, int y0, int x1, int y1) {
  Image out{x1 - x0, y1 - y0, {}};
  out.rgba.resize(size_t(out.width) * out.height * 4);
  for (int y = 0; y < out.height; ++y) {
    std::memcpy(&out.rgba[size_t(y) * out.width * 4], image.at(x0, y0 + y), size_t(out.width) * 4);
  }
  return out;
}

// The bounding box of the visible pixels.
void Bounds(const Image& image, int* x0, int* y0, int* x1, int* y1) {
  *x0 = image.width, *y0 = image.height, *x1 = 0, *y1 = 0;
  for (int y = 0; y < image.height; ++y) {
    for (int x = 0; x < image.width; ++x) {
      if (image.at(x, y)[3] <= 16) continue;
      *x0 = std::min(*x0, x), *y0 = std::min(*y0, y);
      *x1 = std::max(*x1, x + 1), *y1 = std::max(*y1, y + 1);
    }
  }
}

// The visible part (alpha > 16).
Image Visible(const Image& image) {
  int x0, y0, x1, y1;
  Bounds(image, &x0, &y0, &x1, &y1);
  return x1 > x0 ? Crop(image, x0, y0, x1, y1) : Image{};
}

// `image` centred on a transparent square of `size`, scaled down by area
// averaging (with premultiplied alpha, so edges don't darken).
Image Square(const Image& image, int size) {
  Image out{size, size, std::vector<uint8_t>(size_t(size) * size * 4)};
  float scale = size / float(std::max(image.width, image.height));
  float w = image.width * scale, h = image.height * scale;
  float left = (size - w) * 0.5f, top = (size - h) * 0.5f;
  for (int y = 0; y < size; ++y) {
    for (int x = 0; x < size; ++x) {
      // The output pixel's footprint in the source.
      float sx0 = (x - left) / scale, sx1 = (x + 1 - left) / scale;
      float sy0 = (y - top) / scale, sy1 = (y + 1 - top) / scale;
      int ix0 = std::max(0, int(sx0)), ix1 = std::min(image.width, int(sx1) + 1);
      int iy0 = std::max(0, int(sy0)), iy1 = std::min(image.height, int(sy1) + 1);
      double sum[4] = {}, area = (sx1 - sx0) * (sy1 - sy0);
      for (int iy = iy0; iy < iy1; ++iy) {
        double cover_y = std::min<double>(sy1, iy + 1) - std::max<double>(sy0, iy);
        if (cover_y <= 0) continue;
        for (int ix = ix0; ix < ix1; ++ix) {
          double cover = cover_y * (std::min<double>(sx1, ix + 1) - std::max<double>(sx0, ix));
          if (cover <= 0) continue;
          const uint8_t* p = image.at(ix, iy);
          double a = p[3] / 255.0 * cover;
          sum[0] += p[0] * a, sum[1] += p[1] * a, sum[2] += p[2] * a, sum[3] += a;
        }
      }
      uint8_t* q = &out.rgba[(size_t(y) * size + x) * 4];
      if (sum[3] > 0) {
        for (int k = 0; k < 3; ++k) q[k] = uint8_t(std::clamp(sum[k] / sum[3], 0.0, 255.0));
        q[3] = uint8_t(std::clamp(sum[3] / area * 255.0 + 0.5, 0.0, 255.0));
      }
    }
  }
  return out;
}

// One .ico entry: a 32-bit bitmap (BGRA, bottom-up) and its empty AND mask.
std::vector<uint8_t> IconBitmap(const Image& image) {
  int size = image.width;
  size_t mask_row = size_t((size + 31) / 32) * 4;
  BITMAPINFOHEADER header = {};
  header.biSize = sizeof(header);
  header.biWidth = size;
  header.biHeight = size * 2;  // the colour bitmap and the mask
  header.biPlanes = 1;
  header.biBitCount = 32;
  header.biSizeImage = DWORD(size_t(size) * size * 4 + mask_row * size);
  std::vector<uint8_t> data(sizeof(header) + header.biSizeImage);
  std::memcpy(data.data(), &header, sizeof(header));
  uint8_t* pixels = data.data() + sizeof(header);
  for (int y = 0; y < size; ++y) {
    for (int x = 0; x < size; ++x) {
      const uint8_t* p = image.at(x, size - 1 - y);
      uint8_t* q = pixels + (size_t(y) * size + x) * 4;
      q[0] = p[2], q[1] = p[1], q[2] = p[0], q[3] = p[3];
    }
  }
  return data;
}

}  // namespace

const char* IconSource() { return kPicture; }

fs::path IconPath(const fs::path& game_folder) { return game_folder / "icon.ico"; }

bool WriteGameIcon(const GameFiles& files, const fs::path& ico) {
  auto dds = files.Read(kPicture);
  if (!dds) return false;
  Image picture;
  picture.rgba = DecodeDds(*dds, &picture.width, &picture.height);
  if (picture.rgba.empty()) return false;
  Image whole = Visible(picture);
  if (whole.rgba.empty()) return false;

  std::vector<std::vector<uint8_t>> entries;
  for (int size : kSizes) entries.push_back(IconBitmap(Square(whole, size)));
  std::vector<uint8_t> file(6 + 16 * entries.size());
  auto put16 = [&](size_t at, uint16_t v) { std::memcpy(&file[at], &v, 2); };
  auto put32 = [&](size_t at, uint32_t v) { std::memcpy(&file[at], &v, 4); };
  put16(2, 1);  // an icon
  put16(4, uint16_t(entries.size()));
  for (size_t i = 0; i < entries.size(); ++i) {
    size_t at = 6 + 16 * i;
    file[at] = file[at + 1] = uint8_t(kSizes[i]);
    put16(at + 4, 1);   // planes
    put16(at + 6, 32);  // bits per pixel
    put32(at + 8, uint32_t(entries[i].size()));
    put32(at + 12, uint32_t(file.size()));
    file.insert(file.end(), entries[i].begin(), entries[i].end());
  }
  // Written beside and then moved over, so a window never loads half a file.
  fs::path partial = ico;
  partial += ".partial";
  {
    std::ofstream out(partial, std::ios::binary);
    if (!out.write(reinterpret_cast<const char*>(file.data()), std::streamsize(file.size()))) {
      return false;
    }
  }
  std::error_code ec;
  fs::rename(partial, ico, ec);
  if (ec) return false;
  Log("icon: %s, from the cover tile (%dx%d)", ico.u8string().c_str(), whole.width,
      whole.height);
  return true;
}

void SetWindowIcon(void* window, const fs::path& ico) {
  std::error_code ec;
  if (!window || !fs::exists(ico, ec)) return;
  auto load = [&](int metric_x, int metric_y) {
    return static_cast<HICON>(LoadImageW(nullptr, ico.c_str(), IMAGE_ICON,
                                         GetSystemMetrics(metric_x), GetSystemMetrics(metric_y),
                                         LR_LOADFROMFILE));
  };
  HWND hwnd = static_cast<HWND>(window);
  if (HICON big = load(SM_CXICON, SM_CYICON)) SendMessageW(hwnd, WM_SETICON, ICON_BIG, LPARAM(big));
  if (HICON little = load(SM_CXSMICON, SM_CYSMICON)) {
    SendMessageW(hwnd, WM_SETICON, ICON_SMALL, LPARAM(little));
  }
}

void RefreshShellIcons() { SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr); }

}  // namespace launcher
