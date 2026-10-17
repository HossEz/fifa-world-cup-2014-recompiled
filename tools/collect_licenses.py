"""Writes licenses/THIRD-PARTY-NOTICES.txt: the licence of every third-party
component in the PC release (launcher, game, SDK runtime and GPU plugin),
copied from an SDK checkout.

    python tools/collect_licenses.py <rexglue-sdk checkout>

The list below is what the release's files contain (checked 2026-10-16 against
the SDK's CMake targets and the shipped binaries). The SDK's PowerPC
disassembler (GNU binutils, GPL) is only in its code generator, which isn't
shipped. Rerun after updating the SDK; add an entry when a library is added.
"""

import os
import re
import sys

SDK_COMMIT = "bd833a2"  # the patch series' base (patches/rexglue-sdk/README.md)

MIT = """MIT License

Copyright (c) {}

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE."""

# name, where it is in the release, licence, source, how to get the text:
# ("file", path) a licence file, ("header", path) the file's first comment,
# ("tail", path, marker) from a marker to the end of the comment,
# ("until", path, marker) the file's start up to a marker, or
# ("text", str) written here (the SDK's copy has no licence file).
COMPONENTS = [
    ("ReXGlue SDK (includes code from the Xenia project)", "game runtime, GPU plugin, game",
     "BSD 3-Clause", "https://github.com/rexglue/rexglue-sdk", ("file", "LICENSE")),
    ("Dear ImGui", "launcher, game overlay", "MIT", "https://github.com/ocornut/imgui",
     ("file", "thirdparty/imgui/LICENSE.txt")),
    ("JSON for Modern C++ (nlohmann/json 3.11.3)", "launcher (the update check)", "MIT",
     "https://github.com/nlohmann/json", ("text", MIT.format("2013-2023 Niels Lohmann"))),
    ("SDL 3", "launcher (controllers), game runtime", "zlib", "https://github.com/libsdl-org/SDL",
     ("file", "thirdparty/sdl3/LICENSE.txt")),
    ("FFmpeg (libavcodec, libavutil)", "launcher (menu music), game runtime (game audio)",
     "LGPL 2.1 or later", "see the LGPL section at the end",
     ("file", "thirdparty/FFmpeg/COPYING.LGPLv2.1")),
    ("libmspack (LZX decompressor)", "launcher (game archives), game runtime (executables)",
     "LGPL 2.1", "see the LGPL section at the end",
     ("file", "thirdparty/libmspack/libmspack/COPYING.LIB")),
    ("stb_image", "launcher, game runtime", "MIT (or public domain)", "https://github.com/nothings/stb",
     ("tail", "thirdparty/stb/stb_image.h", "ALTERNATIVE A - MIT License")),
    ("{fmt}", "game runtime, GPU plugin, game", "MIT", "https://github.com/fmtlib/fmt",
     ("file", "thirdparty/fmt/LICENSE")),
    ("spdlog", "game runtime, GPU plugin, game", "MIT", "https://github.com/gabime/spdlog",
     ("file", "thirdparty/spdlog/LICENSE")),
    ("toml++", "game runtime, game", "MIT", "https://github.com/marzer/tomlplusplus",
     ("file", "thirdparty/tomlplusplus/LICENSE")),
    ("xxHash", "game runtime, GPU plugin", "BSD 2-Clause", "https://github.com/Cyan4973/xxHash",
     ("file", "thirdparty/xxHash/LICENSE")),
    ("o1heap", "game runtime", "MIT", "https://github.com/pavel-kirienko/o1heap",
     ("file", "thirdparty/o1heap/LICENSE")),
    ("UTF8-CPP", "game runtime", "Boost Software License 1.0", "https://github.com/nemtrif/utfcpp",
     ("file", "thirdparty/utfcpp/LICENSE")),
    ("SIMD Everywhere", "game runtime", "MIT", "https://github.com/simd-everywhere/simde",
     ("file", "thirdparty/simde/COPYING")),
    ("disruptorplus", "game runtime", "MIT", "https://github.com/lewissbaker/disruptorplus",
     ("text", "MIT License. Copyright (c) Lewis Baker. (The SDK's copy carries no licence\n"
              "file; see the project's repository for the full text.)")),
    ("aes_128", "game runtime", "MIT", "the SDK's thirdparty/aes_128",
     ("file", "thirdparty/aes_128/LICENSE")),
    ("tiny-AES-c", "game runtime", "The Unlicense (public domain)", "https://github.com/kokke/tiny-AES-c",
     ("text", "Released into the public domain under The Unlicense. (The SDK's copy\n"
              "carries no licence file.)")),
    ("DES (Faraz Fallahi)", "game runtime", "MIT", "the SDK's thirdparty/crypto/des",
     ("file", "thirdparty/crypto/des/LICENSE")),
    ("TinySHA1", "game runtime", "ISC-style", "the SDK's thirdparty/crypto",
     ("header", "thirdparty/crypto/TinySHA1.hpp")),
    ("RC4 (Whistle Communications)", "game runtime", "BSD-style", "the SDK's thirdparty/crypto",
     ("header", "thirdparty/crypto/rc4.c")),
    ("Rijndael reference code", "game runtime", "public domain", "the SDK's thirdparty/crypto",
     ("header", "thirdparty/crypto/rijndael-alg-fst.c")),
    ("SHA-256 (Stephan Brumme)", "game runtime", "zlib", "the SDK's thirdparty/crypto",
     ("header", "thirdparty/crypto/sha256.cpp")),
    ("DXBC checksum (AMD), derived from the RSA Data Security, Inc. MD5 Message Digest Algorithm",
     "GPU plugin", "RSA MD5 licence", "the SDK's thirdparty/dxbc",
     ("until", "thirdparty/dxbc/DXBCChecksum.cpp", "#include <stdio.h>")),
    ("RenderDoc in-application API header", "GPU plugin", "MIT", "https://github.com/baldurk/renderdoc",
     ("header", "thirdparty/renderdoc/renderdoc_app.h")),
    ("Microsoft Visual C++ runtime (msvcp140*.dll, vcruntime140*.dll)", "all programs",
     "Microsoft Visual Studio redistributable", "https://learn.microsoft.com/cpp/windows/redistributing-visual-cpp-files",
     ("text", "Distributed under the redistribution terms of Microsoft Visual Studio\n"
              "(the files listed as redistributable in its Redist folder).")),
]

LGPL_NOTE = f"""\
LGPL components: source and relinking
-------------------------------------

FFmpeg and libmspack are under the GNU Lesser General Public License 2.1 (FFmpeg:
2.1 or later; built without any GPL or non-free parts: decoders mp3, wmav2 and
xmaframes only). Their complete source, as used here:

  * the ReXGlue SDK at commit {SDK_COMMIT} (https://github.com/rexglue/rexglue-sdk),
    which carries them as submodules: thirdparty/FFmpeg (a fork with Xenia's
    XMA decoder, https://github.com/wmarti/FFmpeg) and thirdparty/libmspack
    (https://github.com/kyz/libmspack, v1.11); the SDK's thirdparty/ffmpeg-overlay
    adds an XMA2 decoder;
  * with this project's patches to the SDK (patches/rexglue-sdk in this
    project's repository).

In the game they're part of the SDK runtime DLL (rexruntime.dll), which the game
loads at run time: you may replace it with one built from modified sources. The
launcher links them in; its complete source and build instructions are in this
project's repository (launcher/), so it can be rebuilt with modified versions.
"""


def first_comment(text: str) -> str:
    """The file's first comment block (/* ... */ or consecutive // lines)."""
    text = text.lstrip()
    if text.startswith("/*"):
        return text[: text.index("*/") + 2]
    lines = []
    for line in text.splitlines():
        if not line.lstrip().startswith("//"):
            if lines:
                break
            continue
        lines.append(line)
    return "\n".join(lines)


def licence_text(sdk: str, how) -> str:
    kind = how[0]
    if kind == "text":
        return how[1]
    path = os.path.join(sdk, how[1])
    with open(path, encoding="utf-8", errors="replace") as f:
        text = f.read()
    if kind == "file":
        return text.strip()
    if kind == "header":
        return first_comment(text).strip()
    if kind == "until":
        return text[: text.index(how[2])].strip()
    if kind == "tail":
        start = text.index(how[2])
        end = text.find("*/", start)
        return text[start:end if end > 0 else None].rstrip("\n *").strip()
    raise ValueError(kind)


def main():
    sdk = sys.argv[1]
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    out_dir = os.path.join(root, "licenses")
    os.makedirs(out_dir, exist_ok=True)
    parts = [
        "2014 FIFA World Cup Brazil: Recompiled - third-party notices",
        "=" * 61,
        "",
        "The release contains these components. Each is listed with where it's used,",
        "its licence and where it comes from, followed by its licence text.",
        "",
    ]
    for i, (name, used, licence, source, _) in enumerate(COMPONENTS, 1):
        parts.append(f"{i:2}. {name}: {licence}")
    parts.append("")
    for i, (name, used, licence, source, how) in enumerate(COMPONENTS, 1):
        title = f"{i}. {name}"
        parts += ["", title, "-" * len(title), f"Used in: {used}", f"Licence: {licence}",
                  f"Source: {source}", "", licence_text(sdk, how), ""]
    parts += ["", LGPL_NOTE]
    text = "\n".join(parts)
    text = re.sub(r"[ \t]+\n", "\n", text)  # no trailing spaces
    with open(os.path.join(out_dir, "THIRD-PARTY-NOTICES.txt"), "w", encoding="utf-8",
              newline="\n") as f:
        f.write(text)
    print(f"{len(COMPONENTS)} components -> licenses/THIRD-PARTY-NOTICES.txt "
          f"({len(text) // 1024} KB)")


if __name__ == "__main__":
    main()
