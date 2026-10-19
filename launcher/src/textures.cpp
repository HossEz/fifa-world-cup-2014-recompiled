#include "textures.h"

#include <algorithm>
#include <cstring>

#include "game_files.h"

namespace launcher {

namespace {

// Alpha of the 16 pixels of a DXT1/3/5 block (row-major).
void BlockAlpha(const uint8_t* block, uint32_t block_bytes, bool dxt3, uint8_t alpha[16]) {
  if (block_bytes == 8) {  // DXT1: index 3 is transparent when color0 <= color1
    uint16_t c0 = uint16_t(block[0] | block[1] << 8), c1 = uint16_t(block[2] | block[3] << 8);
    uint32_t indices = uint32_t(block[4] | block[5] << 8 | block[6] << 16 | uint32_t(block[7]) << 24);
    for (int i = 0; i < 16; ++i) {
      alpha[i] = (c0 <= c1 && ((indices >> (2 * i)) & 3) == 3) ? 0 : 255;
    }
  } else if (dxt3) {  // explicit 4-bit alpha
    for (int i = 0; i < 16; ++i) alpha[i] = uint8_t(((block[i / 2] >> (4 * (i & 1))) & 15) * 17);
  } else {  // DXT5: two endpoints, 3-bit indices
    uint8_t a0 = block[0], a1 = block[1], palette[8] = {a0, a1};
    for (int i = 2; i < 8; ++i) {
      palette[i] = a0 > a1 ? uint8_t(((8 - i) * a0 + (i - 1) * a1) / 7)
                           : (i < 6 ? uint8_t(((6 - i) * a0 + (i - 1) * a1) / 5) : (i == 6 ? 0 : 255));
    }
    uint64_t bits = 0;
    for (int i = 0; i < 6; ++i) bits |= uint64_t(block[2 + i]) << (8 * i);
    for (int i = 0; i < 16; ++i) alpha[i] = palette[(bits >> (3 * i)) & 7];
  }
}

// Colours of the 16 pixels of a DXT colour block (RGB, row-major). DXT1 has a
// three-colour mode (index 3 transparent, see BlockAlpha); DXT3/5 always four.
void BlockColor(const uint8_t* block, bool four_always, uint8_t rgb[16][3]) {
  uint16_t c0 = uint16_t(block[0] | block[1] << 8), c1 = uint16_t(block[2] | block[3] << 8);
  int palette[4][3];
  for (int e = 0; e < 2; ++e) {
    uint16_t c = e ? c1 : c0;
    palette[e][0] = ((c >> 11) & 31) * 255 / 31;
    palette[e][1] = ((c >> 5) & 63) * 255 / 63;
    palette[e][2] = (c & 31) * 255 / 31;
  }
  for (int k = 0; k < 3; ++k) {
    if (four_always || c0 > c1) {
      palette[2][k] = (2 * palette[0][k] + palette[1][k]) / 3;
      palette[3][k] = (palette[0][k] + 2 * palette[1][k]) / 3;
    } else {
      palette[2][k] = (palette[0][k] + palette[1][k]) / 2;
      palette[3][k] = 0;
    }
  }
  uint32_t indices = uint32_t(block[4] | block[5] << 8 | block[6] << 16 | uint32_t(block[7]) << 24);
  for (int i = 0; i < 16; ++i) {
    for (int k = 0; k < 3; ++k) rgb[i][k] = uint8_t(palette[(indices >> (2 * i)) & 3][k]);
  }
}

// DXT1/3/5 blocks in row-major order (the PC layout) decoded to RGBA.
std::vector<uint8_t> DecodeBlocks(const uint8_t* blocks, uint32_t width, uint32_t height,
                                  uint32_t block_bytes, bool dxt3) {
  uint32_t blocks_x = (width + 3) / 4, blocks_y = (height + 3) / 4;
  std::vector<uint8_t> rgba(size_t(width) * height * 4);
  for (uint32_t by = 0; by < blocks_y; ++by) {
    for (uint32_t bx = 0; bx < blocks_x; ++bx) {
      const uint8_t* block = &blocks[(by * blocks_x + bx) * block_bytes];
      uint8_t alpha[16], rgb[16][3];
      BlockAlpha(block, block_bytes, dxt3, alpha);
      BlockColor(block_bytes == 8 ? block : block + 8, block_bytes == 16, rgb);
      for (int i = 0; i < 16; ++i) {
        uint32_t x = bx * 4 + i % 4, y = by * 4 + i / 4;
        if (x >= width || y >= height) continue;
        uint8_t* p = &rgba[(size_t(y) * width + x) * 4];
        p[0] = rgb[i][0], p[1] = rgb[i][1], p[2] = rgb[i][2], p[3] = alpha[i];
      }
    }
  }
  return rgba;
}

uint32_t ReadBe32(const uint8_t* p) {
  return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
}

// Byte offset of block (x, y) in a tiled Xbox 360 texture: Xenia's
// GetTiledOffset2D (the formula in tools/xtexture.py).
uint32_t TiledOffset(uint32_t x, uint32_t y, uint32_t width_blocks, uint32_t log2_bpb) {
  uint32_t pitch = (width_blocks + 31) & ~31u;
  uint32_t macro = ((x >> 5) + (y >> 5) * (pitch >> 5)) << (log2_bpb + 7);
  uint32_t micro = ((x & 7) + ((y & 0xE) << 2)) << log2_bpb;
  uint32_t offset = macro + ((micro & ~0xFu) << 1) + (micro & 0xF) + ((y & 1) << 4);
  return ((offset & ~0x1FFu) << 3) + ((y & 16) << 7) + ((offset & 0x1C0) << 2) +
         ((((y & 8) >> 2) + (x >> 3)) & 3) * 64 + (offset & 0x3F);
}

}  // namespace

std::vector<uint8_t> DecodeDds(const std::vector<uint8_t>& dds, int* width_out,
                               int* height_out) {
  if (dds.size() < 128 || std::memcmp(dds.data(), "DDS ", 4) != 0) return {};
  uint32_t height, width;
  std::memcpy(&height, &dds[12], 4);
  std::memcpy(&width, &dds[16], 4);
  uint32_t block_bytes;
  bool dxt3 = false;
  if (std::memcmp(&dds[84], "DXT1", 4) == 0) {
    block_bytes = 8;
  } else if (std::memcmp(&dds[84], "DXT3", 4) == 0) {
    block_bytes = 16;
    dxt3 = true;
  } else if (std::memcmp(&dds[84], "DXT5", 4) == 0) {
    block_bytes = 16;
  } else {
    return {};
  }
  uint32_t blocks_x = (width + 3) / 4, blocks_y = (height + 3) / 4;
  if (dds.size() < 128 + size_t(blocks_x) * blocks_y * block_bytes) return {};
  *width_out = int(width);
  *height_out = int(height);
  return DecodeBlocks(&dds[128], width, height, block_bytes, dxt3);
}

std::vector<uint8_t> DecodeXpr2(const std::vector<uint8_t>& xpr2, int* width_out,
                                int* height_out) {
  if (xpr2.size() < 16 || std::memcmp(xpr2.data(), "XPR2", 4) != 0) return {};
  size_t data_start = 12 + size_t(ReadBe32(&xpr2[4]));
  uint32_t count = ReadBe32(&xpr2[12]);
  for (uint32_t i = 0; i < count && 32 + i * 16 <= xpr2.size(); ++i) {
    const uint8_t* resource = &xpr2[16 + i * 16];
    if (std::memcmp(resource, "TX2D", 4) != 0) continue;
    size_t fetch_at = 12 + size_t(ReadBe32(resource + 4)) + 28;
    if (fetch_at + 24 > xpr2.size()) return {};
    uint32_t fetch[6];
    for (int k = 0; k < 6; ++k) fetch[k] = ReadBe32(&xpr2[fetch_at + 4 * k]);
    bool tiled = (fetch[0] >> 31) != 0;
    uint32_t format = fetch[1] & 0x3F;
    size_t base = data_start + ((fetch[1] >> 12) << 12);
    uint32_t width = (fetch[2] & 0x1FFF) + 1, height = ((fetch[2] >> 13) & 0x1FFF) + 1;
    uint32_t block_bytes = format == 18 ? 8 : 16;  // 18 DXT1, 19 DXT3, 20 DXT5
    if (format < 18 || format > 20) return {};
    uint32_t log2_bpb = block_bytes == 8 ? 3 : 4;
    uint32_t blocks_x = (width + 3) / 4, blocks_y = (height + 3) / 4;
    // To the PC layout: untiled, and the 16-bit words byte-swapped.
    std::vector<uint8_t> blocks(size_t(blocks_x) * blocks_y * block_bytes);
    for (uint32_t by = 0; by < blocks_y; ++by) {
      for (uint32_t bx = 0; bx < blocks_x; ++bx) {
        size_t from = base + (tiled ? TiledOffset(bx, by, blocks_x, log2_bpb)
                                    : (size_t(by) * blocks_x + bx) * block_bytes);
        if (from + block_bytes > xpr2.size()) return {};
        uint8_t* to = &blocks[(size_t(by) * blocks_x + bx) * block_bytes];
        for (uint32_t b = 0; b < block_bytes; b += 2) to[b] = xpr2[from + b + 1], to[b + 1] = xpr2[from + b];
      }
    }
    *width_out = int(width);
    *height_out = int(height);
    return DecodeBlocks(blocks.data(), width, height, block_bytes, format == 19);
  }
  return {};
}

Texture LoadRgba(ID3D11Device* device, const std::vector<uint8_t>& rgba, int width, int height) {
  Texture texture;
  if (width <= 0 || height <= 0 || rgba.size() < size_t(width) * height * 4) return texture;
  D3D11_TEXTURE2D_DESC desc = {};
  desc.Width = UINT(width);
  desc.Height = UINT(height);
  desc.MipLevels = 1;
  desc.ArraySize = 1;
  desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.SampleDesc.Count = 1;
  desc.Usage = D3D11_USAGE_IMMUTABLE;
  desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
  D3D11_SUBRESOURCE_DATA initial = {rgba.data(), UINT(width) * 4, 0};
  Microsoft::WRL::ComPtr<ID3D11Texture2D> resource;
  if (FAILED(device->CreateTexture2D(&desc, &initial, &resource))) return texture;
  if (FAILED(device->CreateShaderResourceView(resource.Get(), nullptr, &texture.view))) {
    return texture;
  }
  texture.width = width;
  texture.height = height;
  return texture;
}

Texture LoadDds(ID3D11Device* device, const std::vector<uint8_t>& dds) {
  Texture texture;
  if (dds.size() < 128 || std::memcmp(dds.data(), "DDS ", 4) != 0) return texture;
  uint32_t height, width;
  std::memcpy(&height, &dds[12], 4);
  std::memcpy(&width, &dds[16], 4);
  DXGI_FORMAT format;
  uint32_t block_bytes;
  if (std::memcmp(&dds[84], "DXT1", 4) == 0) {
    format = DXGI_FORMAT_BC1_UNORM;
    block_bytes = 8;
  } else if (std::memcmp(&dds[84], "DXT3", 4) == 0) {
    format = DXGI_FORMAT_BC2_UNORM;
    block_bytes = 16;
  } else if (std::memcmp(&dds[84], "DXT5", 4) == 0) {
    format = DXGI_FORMAT_BC3_UNORM;
    block_bytes = 16;
  } else {
    return texture;
  }
  uint32_t row_bytes = (width + 3) / 4 * block_bytes;
  if (dds.size() < 128 + size_t(row_bytes) * ((height + 3) / 4)) return texture;

  D3D11_TEXTURE2D_DESC desc = {};
  desc.Width = width;
  desc.Height = height;
  desc.MipLevels = 1;
  desc.ArraySize = 1;
  desc.Format = format;
  desc.SampleDesc.Count = 1;
  desc.Usage = D3D11_USAGE_IMMUTABLE;
  desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
  D3D11_SUBRESOURCE_DATA initial = {&dds[128], row_bytes, 0};
  Microsoft::WRL::ComPtr<ID3D11Texture2D> resource;
  if (FAILED(device->CreateTexture2D(&desc, &initial, &resource))) return texture;
  if (FAILED(device->CreateShaderResourceView(resource.Get(), nullptr, &texture.view))) {
    return texture;
  }
  // The visible part: the bounding box of pixels with alpha > 0.
  uint32_t blocks_x = (width + 3) / 4, blocks_y = (height + 3) / 4;
  bool dxt3 = format == DXGI_FORMAT_BC2_UNORM;
  int x0 = int(width), y0 = int(height), x1 = -1, y1 = -1;
  for (uint32_t by = 0; by < blocks_y; ++by) {
    for (uint32_t bx = 0; bx < blocks_x; ++bx) {
      uint8_t alpha[16];
      BlockAlpha(&dds[128 + (by * blocks_x + bx) * block_bytes], block_bytes, dxt3, alpha);
      for (int i = 0; i < 16; ++i) {
        if (!alpha[i]) continue;
        int x = int(bx * 4 + i % 4), y = int(by * 4 + i / 4);
        x0 = std::min(x0, x), y0 = std::min(y0, y), x1 = std::max(x1, x), y1 = std::max(y1, y);
      }
    }
  }
  if (x1 < 0) x0 = y0 = 0, x1 = int(width) - 1, y1 = int(height) - 1;
  texture.width = x1 - x0 + 1;
  texture.height = y1 - y0 + 1;
  texture.u0 = float(x0) / width, texture.v0 = float(y0) / height;
  texture.u1 = float(x1 + 1) / width, texture.v1 = float(y1 + 1) / height;
  return texture;
}

Texture LoadGameTexture(ID3D11Device* device, const GameFiles& files, const std::string& path) {
  auto data = files.Read(path);
  return data ? LoadDds(device, *data) : Texture{};
}

}  // namespace launcher
