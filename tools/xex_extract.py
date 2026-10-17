"""Extract the in-memory image of a retail XEX2 (encrypted, basic or no compression).

The output is the raw image as the console maps it at the XEX's base address,
suitable for disassembly, e.g.:

    powerpc-none-elf-objdump -D -b binary -m powerpc:common -EB \
        --adjust-vma=0x82000000 --start-address=0x8249E600 --stop-address=0x8249E700 image.bin

Usage: python xex_extract.py <input.xex> <output.bin>
"""

import struct
import sys

from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes

RETAIL_KEY = bytes.fromhex("20B185A59D28FDC340583FBB0896BF91")

KEY_FILE_FORMAT = 0x000003FF
KEY_IMAGE_BASE = 0x00010201

ENCRYPTION_NONE, ENCRYPTION_NORMAL = 0, 1
COMPRESSION_NONE, COMPRESSION_BASIC = 0, 1


def u32(buf: bytes, off: int) -> int:
    return struct.unpack_from(">I", buf, off)[0]


def optional_headers(xex: bytes) -> dict[int, int]:
    count = u32(xex, 0x14)
    return {u32(xex, 0x18 + i * 8): u32(xex, 0x1C + i * 8) for i in range(count)}


def extract(xex: bytes) -> tuple[int, bytes]:
    if xex[:4] != b"XEX2":
        raise ValueError("not an XEX2 file")
    header_size = u32(xex, 0x08)
    security = u32(xex, 0x10)
    headers = optional_headers(xex)

    fmt = headers[KEY_FILE_FORMAT]
    info_size = u32(xex, fmt)
    encryption, compression = struct.unpack_from(">HH", xex, fmt + 4)

    image_size = u32(xex, security + 0x04)
    base = headers.get(KEY_IMAGE_BASE) or u32(xex, security + 0x110)
    payload = xex[header_size:]

    if encryption == ENCRYPTION_NORMAL:
        # Per-file session key = retail key decrypting the header's AES key field.
        wrapped_key = xex[security + 0x150 : security + 0x160]
        ecb = Cipher(algorithms.AES(RETAIL_KEY), modes.ECB()).decryptor()
        session_key = ecb.update(wrapped_key) + ecb.finalize()
        # One CBC stream (IV = 0) over all data blocks back to back.
        cbc = Cipher(algorithms.AES(session_key), modes.CBC(bytes(16))).decryptor()
        aligned = len(payload) - len(payload) % 16
        payload = cbc.update(payload[:aligned]) + cbc.finalize()
    elif encryption != ENCRYPTION_NONE:
        raise ValueError(f"unsupported encryption type {encryption}")

    if compression == COMPRESSION_NONE:
        image = payload[:image_size]
    elif compression == COMPRESSION_BASIC:
        out = bytearray()
        src = 0
        for i in range((info_size - 8) // 8):
            data_size, zero_size = struct.unpack_from(">II", xex, fmt + 8 + i * 8)
            out += payload[src : src + data_size]
            out += bytes(zero_size)
            src += data_size
        image = bytes(out)
    else:
        raise ValueError(f"unsupported compression type {compression} (LZX not implemented)")

    image = image.ljust(image_size, b"\0")
    if image[:2] != b"MZ":
        raise ValueError("decrypted image has no PE header; wrong key or format")
    return base, image


def main() -> None:
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    with open(sys.argv[1], "rb") as f:
        base, image = extract(f.read())
    with open(sys.argv[2], "wb") as f:
        f.write(image)
    print(f"base 0x{base:08X} size 0x{len(image):08X} -> {sys.argv[2]}")


if __name__ == "__main__":
    main()
