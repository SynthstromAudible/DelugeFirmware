#!/bin/bash
# Rebuilds tiny.elf, the loaded_data() fixture: .rodata, .data, .init_array, and an SDRAM image (.sdram_init, its
# .sdram_exidx tail) packed into .sdram_init_pack the way the device image packs it. Needs llvm-mc and ld.lld.
set -euo pipefail
cd "$(dirname "$0")"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
python3 - "$work" <<'PY'
import struct, sys, zlib
content = b"sdram text here!" + b"\x00" * 16 + b"SDRAM data......" + b"EXIDXEXIDXEXIDX!"
packed = zlib.compress(content)
open(f"{sys.argv[1]}/sdram.bin", "wb").write(content)
open(f"{sys.argv[1]}/pack.bin", "wb").write(struct.pack("<II", len(content), len(packed)) + packed)
PY
sed "s#\"sdram.bin\"#\"$work/sdram.bin\"#; s#\"pack.bin\"#\"$work/pack.bin\"#" tiny.s > "$work/tiny.s"
llvm-mc -triple=thumbv7a-none-eabi -filetype=obj "$work/tiny.s" -o "$work/tiny.o"
ld.lld -T tiny.ld "$work/tiny.o" -o tiny.elf -z max-page-size=16 -z common-page-size=16 --no-rosegment -e 0
