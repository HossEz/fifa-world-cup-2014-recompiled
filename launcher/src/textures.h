// The game's UI images as Direct3D 11 textures.
//
// The front-end art the launcher uses (data/ui/imgassets/...) is stored as PC
// .dds files with DXT1/3/5 blocks, which Direct3D 11 samples directly (BC1-3):
// no decoding, the blocks go to the GPU as they are.

#pragma once

#include <d3d11.h>
#include <wrl/client.h>

#include <cstdint>
#include <string>
#include <vector>

namespace launcher {

class GameFiles;

struct Texture {
  Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view;
  // The visible part (alpha > 0), cropped like the game's own
  // art: its size in pixels and texture coordinates. The game's UI textures
  // often hold a small picture in a larger power-of-two texture.
  int width = 0;
  int height = 0;
  float u0 = 0, v0 = 0, u1 = 1, v1 = 1;

  explicit operator bool() const { return view != nullptr; }
  // For ImGui (ImTextureID is a 64-bit integer holding the view).
  unsigned long long id() const { return reinterpret_cast<unsigned long long>(view.Get()); }
};

// A .dds file's top mip level (DXT1/3/5). An empty texture if the data isn't one.
Texture LoadDds(ID3D11Device* device, const std::vector<uint8_t>& dds);

// A .dds file's top mip level decoded to RGBA (8 bits each, rows top to
// bottom), for images made on the CPU (the icon). Empty if it isn't DXT1/3/5.
std::vector<uint8_t> DecodeDds(const std::vector<uint8_t>& dds, int* width, int* height);

// A .dds entry of the game's archives.
Texture LoadGameTexture(ID3D11Device* device, const GameFiles& files, const std::string& path);

}  // namespace launcher
