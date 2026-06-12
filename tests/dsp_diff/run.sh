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
# Test source selectable as $1 (default dsp_diff.cpp); extra sources to compile
# in (e.g. a table definition) follow.
src="$here/${1:-dsp_diff}.cpp"
shift || true
extra_srcs=("$@")
out="$(mktemp -d)"
trap 'rm -rf "$out"' EXIT

# Match the firmware/host builds: C++23, signed char. Test at -O0 and -O2 (catches
# optimisation-dependent divergence, e.g. UB the optimiser exploits differently).
common=(-std=gnu++23 -fsigned-char -I "$root/src/deluge" -I "$root/src")

# argon (vendored via the sim build's FetchContent) for tests touching the
# vectorised DSP. On x86 its <arm/neon.h> backend maps to SIMDe portable mode,
# mirroring sim/CMakeLists.txt; the ARM build uses the real <arm_neon.h>.
argon_inc="$root/build-sim/_deps/argon-src/include"
if [[ -d $argon_inc ]]; then
	common+=(-I "$argon_inc")
fi
# /usr/include/simde holds arm/neon.h (the SIMDe NEON impl); sim/compat maps
# <arm_neon.h> includes onto it. Mirrors sim/CMakeLists.txt's x86 configuration.
host_flags=(-m32 -msse2 -mfpmath=sse)
if [[ -d /usr/include/simde/arm ]]; then
	host_flags+=(-DSIMDE_NO_NATIVE -I /usr/include/simde -I "$root/sim/compat")
fi

host_cxx="${HOST_CXX:-g++}"
arm_cxx="${ARM_CXX:-arm-linux-gnueabihf-g++}"
qemu="${QEMU:-qemu-arm}"
qemu_args=(-L /usr/arm-linux-gnueabihf -cpu cortex-a9)

status=0
for opt in -O0 -O2; do
	echo "=== optimisation $opt ==="

	# Host (x86, 32-bit to match the sim ABI) — uses the portable/SIMDe branch.
	"$host_cxx" "${host_flags[@]}" "$opt" "${common[@]}" "$src" "${extra_srcs[@]}" -o "$out/host"

	# ARM Cortex-A9 with NEON — uses the #if defined(__arm__) asm. Dynamic link; qemu's -L
	# sysroot resolves the arm libstdc++/libc at run time.
	"$arm_cxx" -mcpu=cortex-a9 -mfpu=neon -mfloat-abi=hard "$opt" "${common[@]}" "$src" "${extra_srcs[@]}" -o "$out/arm"

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
