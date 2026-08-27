#!/usr/bin/env bash
# Configures (if needed) and builds the deluge_loadcheck target into sim/build.
#
# Exists so the "audio-loadcheck" harness entry (harness/registry.toml) is self-sufficient on a
# clean checkout: CI's pr-tier harness job (.github/workflows/rust.yml, job `harness`) checks out,
# installs mtools+clang, and runs `./dbt harness run <name>` directly -- it does not configure any
# sim/ CMake project itself. `sim/build` is gitignored and not restored by any cache step, so without
# this script the harness's own probe/build has to reconstruct it from nothing every time.
#
# -DDELUGE_SIM_X64=ON is REQUIRED, not optional, on a bare x86 Linux runner: the sim defaults to a
# -m32 configuration, which link-fails against deluge_loadcheck's own 64-bit host Rust component
# (the sim_deluge_rust_build target) on a runner without 32-bit multilib packages installed. See
# nightly-build.yml's harness-nightly job, which carries the same flag for the same reason ahead of
# the Embassy Rust harnesses -- this is the same failure mode, not a different one, because
# deluge_loadcheck links its own host-Rust piece into the same deluge_app closure.
#
# No -G is passed: the platform default (Unix Makefiles on Linux) needs only `make` + a C/C++
# compiler, already present on any runner that can build this repo at all -- unlike Ninja, which the
# pr-tier harness job does not install.
set -euo pipefail
cd "$(dirname "$0")"
cmake -S . -B build -DDELUGE_SIM_X64=ON
cmake --build build --target deluge_loadcheck
