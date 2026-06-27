#!/usr/bin/env bash
#
# Differential DSP test: build the portable DSP primitives twice — native ARM/NEON (Cortex-A9,
# under qemu, taking the `#if defined(__arm__)` asm fast paths) and the host x86 fallback (the
# `#else` portable branch the host-sim/SIMDe build uses) — and diff the outputs. They must be
# byte-identical; any difference is a host-portability bug.
#
# Requires: arm-linux-gnueabihf-g++ + qemu-arm (+ /usr/arm-linux-gnueabihf sysroot).
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "$here/../.." && pwd)"
src="$here/dsp_diff.cpp"
out="$(mktemp -d)"
trap 'rm -rf "$out"' EXIT

# Match the firmware/host builds: C++23, signed char. Test at -O0 and -O2 (catches
# optimisation-dependent divergence, e.g. UB the optimiser exploits differently).
common=(-std=gnu++23 -fsigned-char -I "$root/src/deluge" -I "$root/src")

host_cxx="${HOST_CXX:-g++}"
arm_cxx="${ARM_CXX:-arm-linux-gnueabihf-g++}"
qemu="${QEMU:-qemu-arm}"
qemu_args=(-L /usr/arm-linux-gnueabihf -cpu cortex-a9)

status=0
for opt in -O0 -O2; do
	echo "=== optimisation $opt ==="

	# Host (x86, 32-bit to match the sim ABI) — uses the portable #else branch.
	"$host_cxx" -m32 "$opt" "${common[@]}" "$src" -o "$out/host" 2>/dev/null

	# ARM Cortex-A9 with NEON — uses the #if defined(__arm__) asm. Dynamic link; qemu's -L
	# sysroot resolves the arm libstdc++/libc at run time.
	"$arm_cxx" -mcpu=cortex-a9 -mfpu=neon -mfloat-abi=hard "$opt" "${common[@]}" "$src" -o "$out/arm"

	"$out/host" >"$out/host.txt"
	"$qemu" "${qemu_args[@]}" "$out/arm" >"$out/arm.txt"

	if diff -q "$out/host.txt" "$out/arm.txt" >/dev/null; then
		echo "  MATCH ($(wc -l <"$out/host.txt") lines)"
	else
		status=1
		nlines=$(diff "$out/host.txt" "$out/arm.txt" | grep -c '^[<>]' || true)
		echo "  DIVERGENCE: $nlines differing output lines. First few:"
		diff "$out/host.txt" "$out/arm.txt" | head -12 | sed 's/^/    /'
	fi
done

if [[ $status -eq 0 ]]; then
	echo "ALL MATCH — host portable DSP primitives are bit-identical to native NEON."
else
	echo "FAILED — host divergence(s) above."
fi
exit $status
