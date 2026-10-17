"""Turns an F9 GPU capture (captures/NNN/*.bin from the PC build) into PNGs.

    python tools/capture_to_png.py <capture folder> [--out <folder>]

Each .bin is guest memory: a resolve destination (rNN_<address>_info<RB_COPY_DEST_INFO>)
or a texture a draw sampled (dNNN_tN_<address>_<w>x<h>_f<format>_tiled<0|1>_pitch<p>).
Only 32-bit formats are converted; resolves are assumed to be 1280x720, tiled
(pitch 1280). Each image is written twice: as colour (RGB) and its alpha alone,
so black-with-alpha and empty images are easy to tell apart.
"""

import argparse
import re
import sys
from pathlib import Path

import numpy as np
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
from xtexture import untile  # noqa: E402

TEXTURE = re.compile(r"d\d+_t\d+_[0-9A-F]+_(\d+)x(\d+)_f(\d+)_tiled(\d)_pitch(\d+)")
RESOLVE = re.compile(r"r\d+_[0-9A-F]+_info([0-9A-F]+)")
# 32-bit texture formats: k_8_8_8_8 (6), k_2_10_10_10 (7), k_24_8 (22), k_24_8_FLOAT (23).
FORMATS_32BPP = {6, 7, 22, 23}


def to_image(data: bytes, width: int, height: int, pitch: int, tiled: bool) -> np.ndarray:
    aligned_height = (height + 31) & ~31
    if tiled:
        data = untile(data, pitch, aligned_height, 4)
    words = np.frombuffer(data[: pitch * aligned_height * 4].ljust(pitch * aligned_height * 4, b"\0"),
                          ">u4").reshape(aligned_height, pitch)[:height, :width]
    # k_8_8_8_8 in a big-endian word: A (bits 24-31), B, G, R (bits 0-7)?
    # Shown as written: byte 3 = red, 2 = green, 1 = blue, 0 = alpha.
    rgba = np.stack([(words >> s) & 0xFF for s in (0, 8, 16, 24)], axis=-1).astype(np.uint8)
    return rgba


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("capture", type=Path)
    parser.add_argument("--out", type=Path)
    args = parser.parse_args()
    out = args.out or args.capture / "png"
    out.mkdir(parents=True, exist_ok=True)
    for path in sorted(args.capture.glob("*.bin")):
        data = path.read_bytes()
        if m := TEXTURE.fullmatch(path.stem):
            width, height, fmt, tiled, pitch = (int(v) for v in m.groups())
            if fmt not in FORMATS_32BPP:
                continue
            pitch *= 32
        elif RESOLVE.fullmatch(path.stem):
            width, height, tiled, pitch = 1280, 720, 1, 1280
        else:
            continue
        rgba = to_image(data, width, height, pitch, bool(tiled))
        Image.fromarray(rgba[..., :3], "RGB").save(out / f"{path.stem}.rgb.png")
        Image.fromarray(rgba[..., 3], "L").save(out / f"{path.stem}.a.png")
        print(path.stem, "mean rgba", rgba.reshape(-1, 4).mean(axis=0).round(1).tolist())


if __name__ == "__main__":
    main()
