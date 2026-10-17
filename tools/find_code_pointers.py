"""Find likely function entry points that the recompiler missed.

Collects every 32-bit value that points into .text, from two sources:
  * data sections (vtables, callback tables, thread entry pointers), and
  * `lis rX,hi` + `addi/ori rY,rX,lo` pairs in code (address materialised in a register).
Keeps targets that are not known functions and look like a function start: the previous
instruction ends a routine (blr / bctr / unconditional b) or is zero padding.

Usage: python find_code_pointers.py <image.bin> <base-hex> <generated-dir> [<out.toml>]
"""

import pathlib
import re
import struct
import sys

sys.path.insert(0, str(pathlib.Path(__file__).parent))
from pdata_lookup import sections  # noqa: E402


def known_addresses(generated: pathlib.Path) -> tuple[set[int], set[int]]:
    """Function entries and internal labels (jump targets inside functions) already emitted."""
    func_pat = re.compile(r"^DEFINE_REX_FUNC\(\w*?([0-9A-Fa-f]{8})\)", re.M)
    label_pat = re.compile(r"^loc_([0-9A-Fa-f]{8}):", re.M)
    funcs, labels = set(), set()
    for path in generated.glob("*.cpp"):
        text = path.read_text(errors="ignore")
        funcs.update(int(m, 16) for m in func_pat.findall(text))
        labels.update(int(m, 16) for m in label_pat.findall(text))
    return funcs, labels


def ends_routine(word: int) -> bool:
    if word in (0x4E800020, 0x4E800420, 0x00000000):  # blr, bctr, padding
        return True
    return (word >> 26) == 18 and (word & 3) == 0  # b (not bl / absolute)


def main() -> None:
    if len(sys.argv) < 4:
        sys.exit(__doc__)
    image = pathlib.Path(sys.argv[1]).read_bytes()
    base = int(sys.argv[2], 16)
    known, labels = known_addresses(pathlib.Path(sys.argv[3]))

    secs = sections(image)
    text = [(v, s) for n, v, s in secs if n == ".text"]
    tlo, thi = base + text[0][0], base + text[0][0] + text[0][1]

    def word_at(addr: int) -> int:
        return struct.unpack_from(">I", image, addr - base)[0]

    sources: dict[int, str] = {}
    for name, vaddr, vsize in secs:
        if name in (".text", ".pdata", ".reloc"):
            continue
        for off in range(vaddr, vaddr + vsize - 3, 4):
            v = struct.unpack_from(">I", image, off)[0]
            if tlo <= v < thi and v % 4 == 0:
                sources.setdefault(v, f"data {name} @0x{base + off:08X}")

    # lis rX,hi ; addi/ori rY,rX,lo within the next 8 instructions
    for addr in range(tlo, thi - 4, 4):
        w = word_at(addr)
        if (w >> 26) != 15 or ((w >> 16) & 31) != 0:  # lis = addis rX,0,imm
            continue
        rx, hi = (w >> 21) & 31, w & 0xFFFF
        for a2 in range(addr + 4, min(addr + 36, thi), 4):
            w2 = word_at(a2)
            op = w2 >> 26
            if op in (14, 24) and ((w2 >> 16) & 31) == rx:  # addi / ori with rA == rX
                lo = w2 & 0xFFFF
                v = ((hi << 16) + (lo - 0x10000 if op == 14 and lo & 0x8000 else lo)) & 0xFFFFFFFF
                if tlo <= v < thi and v % 4 == 0:
                    sources.setdefault(v, f"code lis/addi @0x{addr:08X}")
                break

    candidates = []
    for v, src in sorted(sources.items()):
        if v in known or v in labels or v == tlo:
            continue
        if word_at(v) == 0:  # pointing at padding, not code
            continue
        if tlo <= word_at(v) < thi:  # a jump table (list of code addresses), not code
            continue
        if (word_at(v) >> 24) in (1, 2) and word_at(v + 8) == 0x7D6903A6:
            continue  # import thunk record (library/ordinal words, then mtctr r11; bctr)
        if ends_routine(word_at(v - 4)):
            candidates.append((v, src))

    print(f"{len(sources)} code pointers, {len(known)} known functions, "
          f"{len(labels)} labels, {len(candidates)} candidates")
    lines = [f'0x{v:08X} = {{ name = "sub_{v:08X}" }} # {src}' for v, src in candidates]
    if len(sys.argv) > 4:
        pathlib.Path(sys.argv[4]).write_text("\n".join(lines) + "\n")
    for line in lines[:40]:
        print(line)


if __name__ == "__main__":
    main()
