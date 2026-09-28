#!/usr/bin/env bash
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"; tree="$here/tree"; out="${1:-$here}"
# A deterministic multi-cluster file (1 MiB) so the Lens-1 transfer-overhead
# proxy (tests/efatfs_core.rs) exercises a real cluster chain — the small tree
# fixtures are all <= 1 cluster. Content is a fixed byte (0x55); only its
# length/cluster-count matters. Generated (not committed) to keep the repo lean.
#
# A second, much larger file (64 MiB = 2048 clusters) so the adversarial
# seek-cost test (efatfs_core_adversarial_seek_transfer_overhead) has a chain
# long enough for the FAT re-walk cost to be measurable at all: at 32 clusters
# (big_multicluster.bin) the whole chain fits in one 512-byte FAT sector and
# the re-walk cost is under measurement noise. FAT32 image only — the FAT16
# image is 64 MB total and cannot hold a 64 MB file plus the rest of the tree.
# Content is per-512-byte-sector-varying (byte = (offset>>9)&0xff), not a
# constant fill: the streaming-pattern non-vacuity test
# (efatfs_streaming_pattern_nonvacuous, tests/efatfs_core.rs) needs distinct
# cluster contents to prove the differential can detect a wrong-offset read.
big="$(mktemp)"; huge="$(mktemp)"; trap 'rm -f "$big" "$huge"' EXIT
head -c 1048576 /dev/zero | tr '\0' '\125' > "$big"
python3 -c "import sys; sys.stdout.buffer.write(bytes((i>>9)&0xff for i in range(67108864)))" > "$huge"
# FAT32: >= 2.5 GB / 32 KB clusters (matches src/bsp/rust/src/sd_image.rs)
truncate -s 2560M "$out/fat32.img"
mformat -F -c 64 -i "$out/fat32.img" ::
mcopy -s -i "$out/fat32.img" "$tree"/* ::
mcopy -i "$out/fat32.img" "$big" ::/SAMPLES/big_multicluster.bin
mcopy -i "$out/fat32.img" "$huge" ::/SAMPLES/huge.bin
# FAT16: 64 MB, let mtools choose FAT16
truncate -s 64M "$out/fat16.img"
mformat -c 4 -i "$out/fat16.img" ::
mcopy -s -i "$out/fat16.img" "$tree"/* ::
mcopy -i "$out/fat16.img" "$big" ::/SAMPLES/big_multicluster.bin
echo "wrote $out/fat16.img $out/fat32.img"
