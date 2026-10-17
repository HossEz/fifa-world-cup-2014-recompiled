"""Xbox 360 textures to RGBA images (Pillow).

Two containers occur in the UI data:
- DDS files: PC layout already (little-endian header, linear pixel data).
- XPR2 resource packages ("TX2D" textures) inside EA texture archives, with
  the texture described by a GPU texture fetch constant. Their pixel data is
  in the Xbox layout: tiled (32x32-block tiles, address formula below) and
  byte-swapped (8-in-16 for block-compressed formats). It is converted to the
  PC layout and decoded by Pillow's DDS reader.
"""

import io
import struct

import numpy as np
from PIL import Image

# DXT formats: (bytes per 4x4 block, DDS fourcc).
BLOCK_FORMATS = {"DXT1": 8, "DXT3": 16, "DXT5": 16}


def tiled_offsets(width_blocks: int, height_blocks: int, bytes_per_block: int) -> np.ndarray:
    """Byte offset in tiled memory of each block, in row-major block order.

    The Xbox 360 2D tiling formula (as in Xenia's GetTiledOffset2D, from
    UModel)."""
    log2_bpb = bytes_per_block.bit_length() - 1
    pitch = (width_blocks + 31) & ~31
    y, x = np.mgrid[0:height_blocks, 0:width_blocks].astype(np.int64)
    macro = ((x >> 5) + (y >> 5) * (pitch >> 5)) << (log2_bpb + 7)
    micro = ((x & 7) + ((y & 0xE) << 2)) << log2_bpb
    offset = macro + ((micro & ~0xF) << 1) + (micro & 0xF) + ((y & 1) << 4)
    return (((offset & ~0x1FF) << 3) + ((y & 16) << 7) + ((offset & 0x1C0) << 2) +
            (((((y & 8) >> 2) + (x >> 3)) & 3) << 6) + (offset & 0x3F)).ravel()


def untile(data: bytes, width_blocks: int, height_blocks: int, bytes_per_block: int) -> bytes:
    offsets = tiled_offsets(width_blocks, height_blocks, bytes_per_block)
    padded_size = int(offsets.max()) + bytes_per_block
    source = np.frombuffer(data.ljust(padded_size, b"\0"), np.uint8)
    gather = offsets[:, None] + np.arange(bytes_per_block)
    return source[gather].tobytes()


def swap_8in16(data: bytes) -> bytes:
    return np.frombuffer(data, ">u2").astype("<u2").tobytes()


def swap_8in32(data: bytes) -> bytes:
    return np.frombuffer(data, ">u4").astype("<u4").tobytes()


def _dds_header(width: int, height: int, fourcc: str) -> bytes:
    linear_size = ((width + 3) // 4) * ((height + 3) // 4) * BLOCK_FORMATS[fourcc]
    header = struct.pack("<4s7I44x", b"DDS ", 124, 0x81007, height, width, linear_size, 0, 0)
    header += struct.pack("<2I4s5I", 32, 4, fourcc.encode(), 0, 0, 0, 0, 0)
    header += struct.pack("<5I", 0x1000, 0, 0, 0, 0)
    return header


def decode_blocks(data: bytes, width: int, height: int, fourcc: str, tiled: bool = True,
                  swap: bool = True) -> Image.Image:
    """A DXT1/3/5 Xbox texture's top mip level as an RGBA image."""
    bytes_per_block = BLOCK_FORMATS[fourcc]
    width_blocks, height_blocks = (width + 3) // 4, (height + 3) // 4
    if tiled:
        data = untile(data, width_blocks, height_blocks, bytes_per_block)
    else:
        data = data[:width_blocks * height_blocks * bytes_per_block]
    if swap:
        data = swap_8in16(data)
    image = Image.open(io.BytesIO(_dds_header(width, height, fourcc) + data))
    return image.convert("RGBA")


def decode_dds(data: bytes) -> Image.Image:
    """A game .dds file as an RGBA image."""
    if data[:4] != b"DDS ":
        raise ValueError("not a DDS file")
    height, width = struct.unpack("<II", data[12:20])
    fourcc = data[84:88].decode("latin-1")
    if fourcc not in BLOCK_FORMATS:
        raise ValueError(f"DDS format {fourcc!r} not supported")
    return decode_blocks(data[128:], width, height, fourcc, tiled=False, swap=False)


# Xenos texture formats (fetch constant dword 1, bits 0-5) Pillow can decode.
XENOS_FORMATS = {18: "DXT1", 19: "DXT3", 20: "DXT5"}


def decode_xpr2(data: bytes) -> dict[str, Image.Image]:
    """The 2D textures of an XPR2 resource package, by resource name.

    XPR2 (big-endian): "XPR2", u32 header size (after the first 12 bytes),
    u32 data size, u32 resource count, then per resource a 4-character type,
    u32 offset (from byte 12), u32 size, u32 name offset. A "TX2D" resource is
    a D3DTexture: 7 dwords, then the 6-dword GPU texture fetch constant."""
    if data[:4] != b"XPR2":
        raise ValueError("not an XPR2 package")
    header_size, _data_size, count = struct.unpack(">III", data[4:16])
    data_start = 12 + header_size
    images = {}
    for i in range(count):
        kind, offset, size, name_offset = struct.unpack(">4sIII", data[16 + i * 16:32 + i * 16])
        if kind != b"TX2D":
            continue
        name_start = 12 + name_offset
        name = data[name_start:data.index(b"\0", name_start)].decode("latin-1")
        fetch = struct.unpack(">6I", data[12 + offset + 28:12 + offset + 52])
        tiled = bool(fetch[0] >> 31)
        xenos_format = fetch[1] & 0x3F
        base = (fetch[1] >> 12) << 12
        width = (fetch[2] & 0x1FFF) + 1
        height = ((fetch[2] >> 13) & 0x1FFF) + 1
        if xenos_format not in XENOS_FORMATS:
            raise ValueError(f"XPR2 {name}: texture format {xenos_format} not supported")
        images[name] = decode_blocks(data[data_start + base:], width, height,
                                     XENOS_FORMATS[xenos_format], tiled=tiled, swap=True)
    return images
