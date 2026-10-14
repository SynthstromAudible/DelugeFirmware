# Host-sim build for the portable Deluge application.
#
# A NATIVE build (x86-64 / ARM64), separate from the arm-none-eabi firmware build.
# It builds the SAME deluge_app library + the portable libs for the host, which
# the Embassy host harnesses and `dbt sim` link (tests/: the `host-app` build
# product, built into build/host-app). Nothing links here: the harnesses' build
# scripts archive these objects.

# 3.27: src/scenes/CMakeLists.txt (add_subdirectory'd below) links deluge_app via
# $<COMPILE_ONLY:...> to take its usage requirements without archiving its objects into
# libdeluge_scenes.a -- that generator expression needs CMake >= 3.27.
cmake_minimum_required(VERSION 3.27)

# ---------------------------------------------------------------------------
# Compiler selection — MUST precede project(), which is where CMake probes and
# locks in the toolchain.
#
# The sim builds at C++26 (CMAKE_CXX_STANDARD 26, below). Apple's bundled
# "AppleClang" tracks the Xcode release, not the mainline LLVM train, and lags
# it by enough that the portable app's C++26 does not compile there. So on
# macOS, unless the user has explicitly chosen a compiler (-DCMAKE_CXX_COMPILER
# or CC/CXX in the environment), prefer a mainline LLVM clang from Homebrew or
# MacPorts over /usr/bin/c++. Elsewhere (Linux) the default GCC is fine.
# ---------------------------------------------------------------------------
if(APPLE AND NOT CMAKE_CXX_COMPILER AND NOT DEFINED ENV{CXX})
    # `brew --prefix llvm` resolves the keg on both Apple Silicon (/opt/homebrew)
    # and Intel (/usr/local); the literal HINTS cover a brew-less PATH and MacPorts.
    execute_process(
        COMMAND brew --prefix llvm
        OUTPUT_VARIABLE SIM_BREW_LLVM_PREFIX
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    set(_sim_clang_hints
        "${SIM_BREW_LLVM_PREFIX}/bin"
        /opt/homebrew/opt/llvm/bin   # Apple Silicon Homebrew
        /usr/local/opt/llvm/bin      # Intel Homebrew
        /opt/local/bin               # MacPorts (clang-mp-*, symlinked clang)
    )
    # NO_DEFAULT_PATH so we never fall back to /usr/bin's AppleClang here.
    find_program(SIM_NATIVE_CLANGXX NAMES clang++ HINTS ${_sim_clang_hints} NO_DEFAULT_PATH)
    find_program(SIM_NATIVE_CLANG   NAMES clang   HINTS ${_sim_clang_hints} NO_DEFAULT_PATH)
    if(SIM_NATIVE_CLANGXX AND SIM_NATIVE_CLANG)
        set(CMAKE_C_COMPILER   "${SIM_NATIVE_CLANG}"   CACHE FILEPATH "mainline clang for C++26")
        set(CMAKE_CXX_COMPILER "${SIM_NATIVE_CLANGXX}" CACHE FILEPATH "mainline clang++ for C++26")
        message(STATUS "Host-sim: mainline LLVM clang for C++26 → ${SIM_NATIVE_CLANGXX}")
    else()
        message(FATAL_ERROR
            "Host-sim on macOS needs a mainline LLVM clang for C++26 — AppleClang lacks it.\n"
            "  Install it:  brew install llvm\n"
            "Or point the build at your own with -DCMAKE_CXX_COMPILER=… (and -DCMAKE_C_COMPILER=…).")
    endif()
endif()

# VERSION mirrors the firmware top-level (project(Deluge VERSION ...)) so the
# generated util/firmware_version.h gets non-empty PROJECT_VERSION_* components.
project(DelugeHostSim VERSION 1.3.0 LANGUAGES C CXX)
include(${CMAKE_CURRENT_LIST_DIR}/../cmake/ccache.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/../cmake/deps.cmake)

set(REPO_ROOT ${CMAKE_CURRENT_LIST_DIR}/..)

# The app's CMake invokes the codegen scripts as `${Python_EXECUTABLE} generate.py ...`
# (l10n strings, menu generator). Without this they'd be exec'd directly and fail on the
# missing +x bit — same find_package the firmware top-level does.
find_package(Python REQUIRED COMPONENTS Interpreter)

# ---------------------------------------------------------------------------
# Shared knobs the src/ subdirectories expect (normally set by the firmware
# top-level). DELUGE_HOST switches off the arm-only bits (e.g. dsp's -marm).
# ---------------------------------------------------------------------------
set(DELUGE_HOST ON)
set(DELUGE_C_STANDARD 23)
set(DELUGE_CXX_STANDARD 26)
set(CMAKE_C_STANDARD 23)
set(CMAKE_CXX_STANDARD 26)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)
cmake_path(SET SHARED_INCLUDE ${REPO_ROOT}/src)

# ---------------------------------------------------------------------------
# Target matrix. DELUGE_SIM_NEON_NATIVE: the SIMD backend is real NEON (<arm_neon.h> exists)
# rather than SIMDe's x86 emulation — true on the arm-linux cross and on any arm64/aarch64 host
# (e.g. Apple Silicon).
# Host configurations:
#   default (Linux x86-64)  SIMDe on real SSE/AVX.
#   DELUGE_SIM_ARM=ON       arm-linux/NEON cross (32-bit armv7), run under qemu.
#   macOS (auto)            native 64-bit; Apple Silicon uses native NEON, Intel Macs use SIMDe.
# ---------------------------------------------------------------------------
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64)$")
    set(DELUGE_SIM_ARM64_HOST ON) # native 64-bit ARM host (Apple Silicon, arm64 Linux)
endif()

if(DELUGE_SIM_ARM OR DELUGE_SIM_ARM64_HOST)
    set(DELUGE_SIM_NEON_NATIVE ON) # real <arm_neon.h>, no SIMDe
endif()

# Char signedness matches the firmware (ARM default is unsigned; be explicit).
set(DELUGE_COMMON_COMPILE_OPTIONS -fsigned-char)
add_compile_options(-fsigned-char)

# ...then append the project's shared warning set (MAINTAINER_MODE, DELUGE_C_STANDARD /
# DELUGE_CXX_STANDARD, DELUGE_COMMON_COMPILE_OPTIONS), the same file the device and tests
# projects include. list(APPEND) inside, so -fsigned-char above survives.
include(${REPO_ROOT}/cmake/deluge_common_options.cmake)

# Debug info — this is a host debug / CI harness; -g makes the inevitable runtime
# triage (gdb backtraces, member inspection) tractable.
add_compile_options(-g)

# Clang-specific host-sim adjustments (default host toolchain is GCC; Clang/AppleClang are
# supported alternatives — note macOS reports "AppleClang", hence MATCHES not STREQUAL).
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    # validateParams() (modulation/params/param.cpp) does O(N^2) constexpr string matching in
    # a static_assert. That exceeds Clang's default -fconstexpr-steps budget; GCC's equivalent
    # -fconstexpr-ops-limit default is far higher, so it compiles there. Raise the ceiling.
    add_compile_options(-fconstexpr-steps=100000000)

    # A mainline clang on macOS (COMPILER_ID "Clang", i.e. NOT AppleClang) compiles against its
    # own newer libc++ headers but would otherwise link/run against the older libc++ in the macOS
    # SDK — a header/runtime mismatch for the C++26 library bits. Point the linker + rpath at the
    # keg's libc++ (<llvm>/bin/clang++ → <llvm>/lib/c++). Harmless if the dir is absent.
    if(APPLE AND CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        cmake_path(GET CMAKE_CXX_COMPILER PARENT_PATH _sim_llvm_bin)
        cmake_path(GET _sim_llvm_bin PARENT_PATH _sim_llvm_prefix)
        if(EXISTS "${_sim_llvm_prefix}/lib/c++")
            add_link_options("-L${_sim_llvm_prefix}/lib/c++" "LINKER:-rpath,${_sim_llvm_prefix}/lib/c++")
        endif()
    endif()
endif()

# Section GC, mirroring the firmware link. Beyond size, it strips genuinely-dead code
# whose refs would otherwise fail to resolve — e.g. the NE10 float32 FFT path (Deluge
# only uses the int32 FFT), which references ne10_fft_alloc_c2c_float32_c (never compiled).
add_compile_options(-ffunction-sections -fdata-sections)
if(APPLE)
    add_link_options(LINKER:-dead_strip) # ld64 equivalent of --gc-sections
else()
    add_link_options(LINKER:--gc-sections)
endif()

# Opt-in AddressSanitizer + UndefinedBehaviorSanitizer for the host-sim. Enable with -DDELUGE_SIM_SANITIZE=ON.
# -fsanitize=address defines __SANITIZE_ADDRESS__, which (a) activates the heap_poison.h hooks in the custom
# allocator so use-after-free / use-after-steal within a region is caught, and (b) disables MEM_GUARD (which would
# otherwise fight ASan over the freed-block shadow — see definitions.h). LeakSanitizer rides along with ASan and
# reports leaks at exit. -fno-omit-frame-pointer keeps the reports readable.
option(DELUGE_SIM_SANITIZE "Build the host-sim with AddressSanitizer + UndefinedBehaviorSanitizer" OFF)
if(DELUGE_SIM_SANITIZE)
    # vptr: the real firmware builds -fno-rtti, so the vptr check isn't representative of shipping behaviour (and the
    # app's view hierarchy leans on static downcasts). alignment: the custom allocator does intentional unaligned
    # boundary-tag reads. shift: simde's portable NEON emulation (and DSP code) relies on implementation-defined signed
    # shift behaviour that mirrors the ARM saturating instructions; this path only exists in the sim (the device uses
    # real NEON), so it isn't shipping behaviour. GCC has no per-file ignorelist, so these are excluded globally - they
    # mirror the firmware's own DELUGE_UBSAN exclusions.
    add_compile_options(-fsanitize=address,undefined -fno-sanitize=vptr,alignment,shift
        -fno-omit-frame-pointer -fno-sanitize-recover=all)
    add_link_options(-fsanitize=address,undefined)
endif()

# SIMDe (x86 hosts only): argon's non-native backend does `#include <arm/neon.h>`, which must
# resolve to <simde/arm/neon.h>. Native-NEON hosts (the arm-linux cross, Apple Silicon, arm64
# Linux) have a real <arm_neon.h> and skip all of this.
if(NOT DELUGE_SIM_NEON_NATIVE)
    # SIMDe provides the NEON intrinsics on non-ARM hosts. It is fetched at the pinned revision (cmake/deps.cmake)
    # rather than taken from a system package, so the portable-NEON emulation, and with it the golden captures, is
    # the same on every host.
    FetchContent_MakeAvailable(simde)
    # Repo layout is ${simde_SOURCE_DIR}/simde/{arm,x86,...}: the root resolves <simde/arm/neon.h>
    # (the compat shim), the simde/ subdir resolves argon's bare <arm/neon.h>. Both are added below.
    set(SIMDE_INCLUDE_DIR ${simde_SOURCE_DIR})

endif()

# The app includes <execution>. On host glibc libstdc++ the parallel STL backend is oneTBB,
# whose headers pull in the full <immintrin.h> — which collides with SIMDe's NEON alias macros
# (real _mm_* functions vs SIMDe's _mm_* #defines). The firmware target has no TBB; mirror that
# by selecting libstdc++'s serial parallel backend (std::execution::par runs serially — fine here).
add_compile_definitions(_GLIBCXX_USE_TBB_PAR_BACKEND=0)

# Zero every block at allocation / pool-reuse / operator new (see memory/general_memory_allocator.cpp,
# object_pool.h, operators.cpp). On the SoC, SDRAM is never cleared, so the firmware's many
# read-before-write paths resolve to whatever the previous occupant left — values that vary with the
# allocation layout. Off-target that non-determinism makes latent uninitialised-read bugs flaky (and
# the golden WAV captures layout-dependent). Zeroing makes the host run deterministic and turns those
# latent reads into a defined 0. Firmware builds leave this off (the guards inline away).
add_compile_definitions(DELUGE_DETERMINISTIC_ALLOC)

# Compile-time marker for host-only code paths (the CMake `DELUGE_HOST` variable above is
# build-system only). Used e.g. to drop the FATFS sector-buffer cache-line over-alignment,
# which has no meaning without DMA and which the host allocator (8-byte aligned) can't honour.
add_compile_definitions(DELUGE_HOST)

# Enable D_PRINTLN (gated on ENABLE_TEXT_OUTPUT; the firmware turns it on for debug builds). The host routes it to
# stderr (deluge_log), so the app's debug log — storage activity, "FULL internal" memory pressure, MEM_GUARD /
# reason_check reports — is visible when reproducing issues on the sim.
add_compile_definitions(ENABLE_TEXT_OUTPUT=1)

# Both sim references take the scalar DX7 operator kernel: the firmware's NEON asm
# kernel (neon_fm_kernel.s) computes sine by polynomial approximation — legitimately
# different output from the scalar LUT path — so the arm-linux reference must not
# use it or x86 and ARM golden renders of DX7 presets could never match.
add_compile_definitions(DELUGE_DX_SCALAR)

if(DELUGE_SIM_NEON_NATIVE)
    # Real NEON (arm-linux cross or an arm64 host): the toolchain provides <arm_neon.h>; argon's
    # <arm/neon.h> and the SIMDe compat shim are unused.
    include_directories(BEFORE ${REPO_ROOT}/include ${REPO_ROOT}/crates/deluge_resource/include)
else()
    include_directories(BEFORE
        ${CMAKE_CURRENT_LIST_DIR}/compat # <arm_neon.h> -> SIMDe shim
        ${REPO_ROOT}/include             # <libdeluge/...> boundary
        ${REPO_ROOT}/crates/deluge_resource/include # deluge_resource.h
        ${SIMDE_INCLUDE_DIR}               # compat shim's <simde/arm/neon.h> -> SIMDe
        ${SIMDE_INCLUDE_DIR}/simde         # argon's bare <arm/neon.h> -> SIMDe
    )
endif()

# ===========================================================================
# The application.
# ===========================================================================

# NE10 (int32 FFT) — native, via SIMDe. deluge_app links the target name `NE10`,
# so build the host version under that name. Its compute is all NEON intrinsics +
# C; the common/*.s boilerplate is omitted (see the smoke-test history).
add_library(NE10 STATIC
    ${REPO_ROOT}/src/NE10/common/NE10_mask_table.c
    ${REPO_ROOT}/src/NE10/modules/NE10_init.c
    ${REPO_ROOT}/src/NE10/modules/dsp/NE10_fft.c
    ${REPO_ROOT}/src/NE10/modules/dsp/NE10_fft_generic_int32.cpp
    ${REPO_ROOT}/src/NE10/modules/dsp/NE10_fft_generic_int32.neonintrinsic.cpp
    ${REPO_ROOT}/src/NE10/modules/dsp/NE10_fft_int32.c
    ${REPO_ROOT}/src/NE10/modules/dsp/NE10_fft_int32.neonintrinsic.c
    ${REPO_ROOT}/src/NE10/modules/dsp/NE10_fir_init.c
    ${REPO_ROOT}/src/NE10/modules/dsp/NE10_init_dsp.c
)
target_include_directories(NE10 PUBLIC
    $<$<NOT:$<BOOL:${DELUGE_SIM_NEON_NATIVE}>>:${CMAKE_CURRENT_LIST_DIR}/compat> # SIMDe shim (x86 only)
    ${REPO_ROOT}/src/NE10/inc ${REPO_ROOT}/src/NE10/common ${REPO_ROOT}/src/NE10
)
target_include_directories(NE10 PRIVATE ${REPO_ROOT}/src ${REPO_ROOT}/include)
# NE10_fft_int32.c calls routineWithClusterLoading() (resolved at the exe link).

# The portable libraries (each subdir is platform-agnostic). Out-of-tree, so give
# each an explicit binary dir.
add_subdirectory(${REPO_ROOT}/lib            ${CMAKE_BINARY_DIR}/_lib)        # argon, etl
add_subdirectory(${REPO_ROOT}/src/engine-api/types ${CMAKE_BINARY_DIR}/engine-types) # deluge_engine_types
add_subdirectory(${REPO_ROOT}/src/foundation ${CMAKE_BINARY_DIR}/foundation) # deluge_foundation
add_subdirectory(${REPO_ROOT}/src/serialization ${CMAKE_BINARY_DIR}/serialization) # deluge_serialization
add_subdirectory(${REPO_ROOT}/src/engine-api ${CMAKE_BINARY_DIR}/engine-api) # deluge_engine_api
add_subdirectory(${REPO_ROOT}/src/engines/effects ${CMAKE_BINARY_DIR}/engines-effects) # deluge_effects
add_subdirectory(${REPO_ROOT}/src/engines/deluge ${CMAKE_BINARY_DIR}/engines-deluge) # deluge_instrument
add_subdirectory(${REPO_ROOT}/src/music-theory ${CMAKE_BINARY_DIR}/music-theory) # deluge_music_theory
add_subdirectory(${REPO_ROOT}/src/deluge-project ${CMAKE_BINARY_DIR}/deluge-project) # deluge_project (model)
add_subdirectory(${REPO_ROOT}/src/midi       ${CMAKE_BINARY_DIR}/midi)       # deluge_midi
add_subdirectory(${REPO_ROOT}/src/gfx        ${CMAKE_BINARY_DIR}/gfx)        # deluge_gfx_oled / deluge_gfx_grid
add_subdirectory(${REPO_ROOT}/src/ui         ${CMAKE_BINARY_DIR}/ui)         # deluge_ui_core / deluge_ui_oled / deluge_ui_grid
add_subdirectory(${REPO_ROOT}/src/notify     ${CMAKE_BINARY_DIR}/notify)     # deluge_notify
add_subdirectory(${REPO_ROOT}/src/shell      ${CMAKE_BINARY_DIR}/shell)      # deluge_ui_shell
add_subdirectory(${REPO_ROOT}/src/lib        ${CMAKE_BINARY_DIR}/printf)     # eyalroz_printf
add_subdirectory(${REPO_ROOT}/src/OSLikeStuff ${CMAKE_BINARY_DIR}/scheduler) # deluge_scheduler
add_subdirectory(${REPO_ROOT}/src/deluge     ${CMAKE_BINARY_DIR}/app)        # deluge_app + deluge_dsp + generators
add_subdirectory(${REPO_ROOT}/tests/integration/scenarios ${CMAKE_BINARY_DIR}/scenarios) # deluge_scenarios: host only
# deluge_scenes: the first UI layer allowed to include the model. Links deluge_app, so it comes after it.
add_subdirectory(${REPO_ROOT}/src/scenes     ${CMAKE_BINARY_DIR}/scenes)     # deluge_scenes

# TODO(ui-integration): the UI targets that ran on the C host BSP, re-hosted on golden_vt_render:
#   deluge_host's scripted headless runs (DELUGE_HOST_SCRIPT, the level-3 UI harness),
#   deluge_loadcheck, deluge_menu_dump with its menu_dump_matches_committed test
#   (scripts/menu_dump_check.sh), the deluge_scenes_spec suite (sim/scenes_spec, CppSpec via
#   cmake/deluge_specs.cmake, support library tests/harness/ui_script.cpp + snapshot.cpp), the
#   DELUGE_HOST_DISPLAYS `--board-info` tests, and the ui_app_scripts_<name> snapshot tests with
#   their menu A/B leg (scripts/ui_snapshot_diff.py, scripts/ui_app_ab_check.py).
#   The C host BSP versions are in clean/ui-perf-1:sim/CMakeLists.txt.
