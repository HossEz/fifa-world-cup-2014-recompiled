"""Look up functions in the .pdata table of an extracted Xbox 360 image (see xex_extract.py).

.pdata holds one 8-byte record per function: start address, then a packed word with
the prolog length (low 8 bits) and function length in instructions (next 22 bits).

Usage: python pdata_lookup.py <image.bin> <base-hex> [<address-hex> ...]
With no addresses, prints a summary of the table.
"""

import struct
import sys


def sections(image: bytes) -> list[tuple[str, int, int]]:
    pe = struct.unpack_from("<I", image, 0x3C)[0]
    count = struct.unpack_from("<H", image, pe + 6)[0]
    opt_size = struct.unpack_from("<H", image, pe + 20)[0]
    table = pe + 24 + opt_size
    out = []
    for i in range(count):
        off = table + i * 40
        name = image[off : off + 8].rstrip(b"\0").decode("ascii", "replace")
        vsize, vaddr = struct.unpack_from("<II", image, off + 8)
        out.append((name, vaddr, vsize))
    return out


def pdata_entries(image: bytes) -> list[tuple[int, int]]:
    for name, vaddr, vsize in sections(image):
        if name == ".pdata":
            entries = []
            for off in range(vaddr, vaddr + vsize, 8):
                begin, packed = struct.unpack_from(">II", image, off)
                if begin == 0:
                    break
                entries.append((begin, ((packed >> 8) & 0x3FFFFF) * 4))
            return entries
    raise ValueError("no .pdata section")


def main() -> None:
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    with open(sys.argv[1], "rb") as f:
        image = f.read()
    base = int(sys.argv[2], 16)
    entries = pdata_entries(image)
    if len(sys.argv) == 3:
        for name, vaddr, vsize in sections(image):
            print(f"{name:8} 0x{base + vaddr:08X} size 0x{vsize:X}")
        print(f".pdata: {len(entries)} functions")
        return
    for arg in sys.argv[3:]:
        addr = int(arg, 16)
        hits = [(b, n) for b, n in entries if b <= addr < b + n]
        if not hits:
            print(f"0x{addr:08X}: not covered by .pdata")
        for b, n in hits:
            print(f"0x{addr:08X}: in function 0x{b:08X}..0x{b + n:08X} (size 0x{n:X})")


if __name__ == "__main__":
    main()
