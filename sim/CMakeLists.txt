# Host-sim build for the portable Deluge application.
#
# A NATIVE build (x86-64 / ARM64), separate from the arm-none-eabi firmware build.
# It assembles the SAME deluge_app library + the portable libs against the host BSP
# (src/bsp/host), so it is both the definitive proof the app has no transitive HAL
# dependency (no RZA1/ on the include path) and the substrate for host CI + the
# audio WAV-diff harness.
#
#   cmake -B build-sim -S sim -G Ninja && cmake --build build-sim
#   ./build-sim/deluge_host_boundary    # BSP-boundary smoke target
#   ./build-sim/deluge_host             # the full application (in progress)

cmake_minimum_required(VERSION 3.24)

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
        message(WARNING
            "Host-sim on macOS needs a mainline LLVM clang for C++26 — AppleClang lacks it.\n"
            "  Install it:  brew install llvm\n"
            "Falling back to the default compiler; the C++26 build will very likely fail.")
    endif()
endif()

# VERSION mirrors the firmware top-level (project(Deluge VERSION ...)) so the
# generated util/firmware_version.h gets non-empty PROJECT_VERSION_* components.
project(DelugeHostSim VERSION 1.3.0 LANGUAGES C CXX)

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
# Target matrix. Rather than enumerate modes, derive two orthogonal facts:
#   DELUGE_SIM_M32          build 32-bit x86 — the firmware's pointer model, and the golden
#                           reference. Only the plain Linux-x86 host; never on macOS (no 32-bit
#                           runtime), an arm64 host, or the explicit 64-bit modes.
#   DELUGE_SIM_NEON_NATIVE  the SIMD backend is real NEON (<arm_neon.h> exists) rather than
#                           SIMDe's x86 emulation — true on the arm-linux cross and on any
#                           arm64/aarch64 host (e.g. Apple Silicon).
# Host configurations:
#   default (Linux x86)     -m32, SIMDe portable-C fallbacks. Bit-matches the firmware.
#   DELUGE_SIM_X64=ON       x86-64 Linux, SIMDe on real SSE/AVX. 64-bit-pointer-clean.
#   DELUGE_SIM_ARM=ON       arm-linux/NEON cross (32-bit armv7), run under qemu.
#   macOS (auto)            native 64-bit; Apple Silicon uses native NEON, Intel Macs use SIMDe.
# ---------------------------------------------------------------------------
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64)$")
    set(DELUGE_SIM_ARM64_HOST ON) # native 64-bit ARM host (Apple Silicon, arm64 Linux)
endif()

if(NOT DELUGE_SIM_ARM AND NOT DELUGE_SIM_X64 AND NOT APPLE AND NOT DELUGE_SIM_ARM64_HOST)
    set(DELUGE_SIM_M32 ON)
endif()

if(DELUGE_SIM_ARM OR DELUGE_SIM_ARM64_HOST)
    set(DELUGE_SIM_NEON_NATIVE ON) # real <arm_neon.h>, no SIMDe
endif()

if(DELUGE_SIM_M32)
    add_compile_options(-m32)
    add_link_options(-m32)

    # Use SSE for float math instead of the 32-bit x87 default. x87 evaluates float
    # intermediates at 80-bit extended precision, which silently diverges from the
    # firmware's strict IEEE-754 single-precision VFP — enough to perturb marginally
    # stable float feedback loops (e.g. the Mutable reverb) away from the device's
    # behaviour. -mfpmath=sse keeps host float arithmetic bit-comparable to the target.
    # (64-bit hosts already default to SSE / use NEON, so this only applies to the -m32 build.)
    add_compile_options(-msse2 -mfpmath=sse)
endif()

# Char signedness matches the firmware (ARM default is unsigned; be explicit).
set(DELUGE_COMMON_COMPILE_OPTIONS -fsigned-char)
add_compile_options(-fsigned-char)

# Debug info — this is a host debug / CI harness; -g makes the inevitable runtime
# triage (gdb backtraces, member inspection) tractable.
add_compile_options(-g)

# Clang-specific host-sim adjustments (default host toolchain is GCC; Clang/AppleClang are
# supported alternatives — note macOS reports "AppleClang", hence MATCHES not STREQUAL).
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    if(DELUGE_SIM_M32)
        # The -m32 sim links against GCC's runtime, but a multilib system may ship no 32-bit
        # libgcc_s.so link stub (only libgcc_s.so.1). GCC resolves -lgcc_s via collect2; Clang
        # does not, and the link fails with "cannot find -lgcc_s". Link libgcc statically to drop
        # the shared -lgcc_s dependency at link time (only meaningful on the 32-bit x86 path; macOS
        # clang has no libgcc at all).
        add_link_options(-static-libgcc)
    endif()

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
    # SIMDe provides the NEON intrinsics on non-ARM hosts — argon's <arm/neon.h> and the compat
    # <arm_neon.h> shim resolve into it. Fetch it (pinned) rather than depending on a system package,
    # so the host-sim builds anywhere without a distro simde install. v0.8.2 matches the version this
    # was developed against, so the portable-NEON emulation (and thus the golden captures) is unchanged.
    include(FetchContent)
    FetchContent_Declare(simde
        GIT_REPOSITORY https://github.com/simd-everywhere/simde
        GIT_TAG v0.8.2
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(simde)
    # Repo layout is ${simde_SOURCE_DIR}/simde/{arm,x86,...}: the root resolves <simde/arm/neon.h>
    # (the compat shim), the simde/ subdir resolves argon's bare <arm/neon.h>. Both are added below.
    set(SIMDE_INCLUDE_DIR ${simde_SOURCE_DIR})

    if(DELUGE_SIM_M32)
        # Force SIMDe to its portable-C fallbacks instead of native x86 intrinsics. Under -m32 SIMDe's
        # native path drags in GCC's AVX512/FMA <immintrin.h> sub-headers, which don't parse in 32-bit
        # mode. The portable path is bit-accurate to NEON (the whole point of SIMDe) and deterministic —
        # exactly what we want for host CI + the audio WAV-diff reference; the speed hit is irrelevant here.
        # Must be global: argon includes <simde/arm/neon.h> directly, not via our compat shim.
        # On x86-64 we drop this and let SIMDe lower to real SSE/AVX.
        add_compile_definitions(SIMDE_NO_NATIVE)
    endif()
endif()

# The app includes <execution>. On host glibc libstdc++ the parallel STL backend is oneTBB,
# whose headers pull in the full <immintrin.h> — which collides with SIMDe's NEON alias macros
# (real _mm_* functions vs SIMDe's _mm_* #defines). The firmware target has no TBB; mirror that
# by selecting libstdc++'s serial parallel backend (std::execution::par runs serially — fine here).
add_compile_definitions(_GLIBCXX_USE_TBB_PAR_BACKEND=0)

# Use the cooperative task scheduler (registerTasks + startTaskManager), exactly as the
# firmware does — the `#else mainLoop()` path in deluge_main is legacy/dead on real hardware
# and doesn't pump the scheduler (so MIDI/playback/UI tasks never run). Matching the firmware
# execution model is both more faithful and what makes input (e.g. MIDI) reach the engine.
add_compile_definitions(USE_TASK_MANAGER)

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

if(DELUGE_SIM_NEON_NATIVE)
    # Real NEON (arm-linux cross or an arm64 host): the toolchain provides <arm_neon.h>; argon's
    # <arm/neon.h> and the SIMDe compat shim are unused.
    include_directories(BEFORE ${REPO_ROOT}/include ${REPO_ROOT}/crates/deluge_resource/include)
    set(HOST_INCLUDE_DIRS ${REPO_ROOT}/include ${REPO_ROOT}/crates/deluge_resource/include ${REPO_ROOT}/src)
else()
    include_directories(BEFORE
        ${CMAKE_CURRENT_LIST_DIR}/compat # <arm_neon.h> -> SIMDe shim
        ${REPO_ROOT}/include             # <libdeluge/...> boundary
        ${REPO_ROOT}/crates/deluge_resource/include # deluge_resource.h
        ${SIMDE_INCLUDE_DIR}               # compat shim's <simde/arm/neon.h> -> SIMDe
        ${SIMDE_INCLUDE_DIR}/simde         # argon's bare <arm/neon.h> -> SIMDe
    )
    set(HOST_INCLUDE_DIRS
        ${CMAKE_CURRENT_LIST_DIR}/compat # arm_neon.h -> SIMDe shim
        ${REPO_ROOT}/include             # <libdeluge/...> boundary
        ${REPO_ROOT}/crates/deluge_resource/include # deluge_resource.h (Rust resource manager C ABI)
        ${REPO_ROOT}/src                 # SHARED_INCLUDE
    )
endif()

# ===========================================================================
# Stage 1: boundary smoke target (host BSP + a main that exercises it).
# ===========================================================================
add_executable(deluge_host_boundary
    ${REPO_ROOT}/src/bsp/host/host_bsp.c
    ${REPO_ROOT}/src/bsp/host/host_main.c
)
target_include_directories(deluge_host_boundary PRIVATE ${HOST_INCLUDE_DIRS})
find_library(RT_LIB rt)
if(RT_LIB)
    target_link_libraries(deluge_host_boundary PRIVATE ${RT_LIB})
endif()

# ===========================================================================
# Stage 2: the full application.
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
add_subdirectory(${REPO_ROOT}/src/foundation ${CMAKE_BINARY_DIR}/foundation) # deluge_foundation
add_subdirectory(${REPO_ROOT}/src/midi       ${CMAKE_BINARY_DIR}/midi)       # deluge_midi
add_subdirectory(${REPO_ROOT}/src/lib        ${CMAKE_BINARY_DIR}/printf)     # eyalroz_printf
add_subdirectory(${REPO_ROOT}/src/fatfs      ${CMAKE_BINARY_DIR}/fatfs)      # fatfs
add_subdirectory(${REPO_ROOT}/src/OSLikeStuff ${CMAKE_BINARY_DIR}/scheduler) # deluge_scheduler
add_subdirectory(${REPO_ROOT}/src/deluge     ${CMAKE_BINARY_DIR}/app)        # deluge_app + deluge_dsp + generators

# --- Rust core (crates/ workspace: deluge_rust umbrella = alloc + slab + resource) -
# Build the umbrella as a host staticlib matching the sim's word size and link it
# into the sim executables. The deluge_app allocator (heaps + the GMA coordinator)
# is unconditionally backed by it; the legacy C++ MemoryRegion path has been retired.
# Pick the Rust std target triple to match the C++ host ABI (word size + platform).
if(APPLE AND DELUGE_SIM_ARM64_HOST)
    set(SIM_RUST_TARGET aarch64-apple-darwin) # Apple Silicon
elseif(APPLE)
    set(SIM_RUST_TARGET x86_64-apple-darwin) # Intel Mac
elseif(DELUGE_SIM_ARM64_HOST)
    set(SIM_RUST_TARGET aarch64-unknown-linux-gnu) # arm64 Linux
elseif(DELUGE_SIM_X64)
    set(SIM_RUST_TARGET x86_64-unknown-linux-gnu)
else()
    set(SIM_RUST_TARGET i686-unknown-linux-gnu) # the default -m32 sim (and the arm-linux cross)
endif()
set(SIM_RUST_WS ${REPO_ROOT}/crates)
set(SIM_RUST_LIB ${SIM_RUST_WS}/target/${SIM_RUST_TARGET}/release/libdeluge_rust.a)
file(GLOB_RECURSE SIM_RUST_SOURCES CONFIGURE_DEPENDS
    ${SIM_RUST_WS}/deluge_alloc/src/*.rs
    ${SIM_RUST_WS}/deluge_resource/src/*.rs
    ${SIM_RUST_WS}/deluge_rust/src/*.rs)
# Under the ASan sim build, turn on the allocator's poison hooks so use-after-free
# within a heap is caught (the __asan_*_memory_region calls resolve from libasan at
# the final C++ link). The feature is a no-op in the crate otherwise.
set(SIM_RUST_FEATURES "")
if(DELUGE_SIM_SANITIZE)
    set(SIM_RUST_FEATURES --features deluge_rust/asan)
endif()
add_custom_command(
    OUTPUT ${SIM_RUST_LIB}
    COMMAND cargo build -p deluge_rust --release --target ${SIM_RUST_TARGET} ${SIM_RUST_FEATURES}
    WORKING_DIRECTORY ${SIM_RUST_WS}
    DEPENDS ${SIM_RUST_SOURCES}
        ${SIM_RUST_WS}/Cargo.lock
        ${SIM_RUST_WS}/Cargo.toml ${SIM_RUST_WS}/deluge_alloc/Cargo.toml
        ${SIM_RUST_WS}/deluge_resource/Cargo.toml ${SIM_RUST_WS}/deluge_rust/Cargo.toml
    COMMENT "Building Rust deluge_rust staticlib (${SIM_RUST_TARGET})"
    VERBATIM
)
add_custom_target(sim_deluge_rust_build DEPENDS ${SIM_RUST_LIB})
add_library(deluge_rust_sim STATIC IMPORTED GLOBAL)
set_target_properties(deluge_rust_sim PROPERTIES IMPORTED_LOCATION ${SIM_RUST_LIB})
add_dependencies(deluge_rust_sim sim_deluge_rust_build)
# Libs the host Rust std staticlib references; linked into the sim executables below.
set(SIM_ALLOC_LINK deluge_rust_sim pthread dl m)
if(APPLE)
    # host_pcm.c's native CoreAudio (AudioQueue) audio backend.
    list(APPEND SIM_ALLOC_LINK "-framework AudioToolbox")
endif()

# The host executable: the portable app + the host BSP + the scheduler. deluge_app
# is an OBJECT lib (its objects embed here); deluge_scheduler is STATIC (resolves
# against them). host_main drives the boundary for now; it becomes the host runtime
# loop (deluge_app_init/render/tick) as the app is brought up.
add_executable(deluge_host
    ${REPO_ROOT}/src/bsp/host/host_bsp.c
    ${REPO_ROOT}/src/bsp/host/host_platform.c
    ${REPO_ROOT}/src/bsp/host/host_audio.c
    ${REPO_ROOT}/src/bsp/host/host_link.c
    ${REPO_ROOT}/src/bsp/host/host_pcm.c
    ${REPO_ROOT}/src/bsp/host/host_midi.c
    ${REPO_ROOT}/src/bsp/host/host_main.c
    ${REPO_ROOT}/src/bsp/host/host_debug.cpp
)
# host_debug.cpp reaches into the app's reason_check (memory/reason_check.h) for the host-sim leak triggers - allowed
# (the boundary check only forbids the app depending on the BSP, not the reverse), so put src/deluge on the path.
target_include_directories(deluge_host PRIVATE ${HOST_INCLUDE_DIRS} ${REPO_ROOT}/src/deluge)
target_link_libraries(deluge_host PRIVATE deluge_app deluge_scheduler ${SIM_ALLOC_LINK})
if(RT_LIB)
    target_link_libraries(deluge_host PRIVATE ${RT_LIB})
endif()

# deluge_render: the headless "project -> stems" utility. Same app + host BSP as
# deluge_host, but its own main()/render-driver (host_render_main.cpp) instead of
# host_main.c, and it drives the existing offline StemExport engine against a FAT
# disk image (the shared host_platform.c diskio backend). Requires mtools at runtime
# (--project packing). See host_render_main.cpp for usage.
add_executable(deluge_render
    ${REPO_ROOT}/src/bsp/host/host_bsp.c
    ${REPO_ROOT}/src/bsp/host/host_platform.c
    ${REPO_ROOT}/src/bsp/host/host_audio.c
    ${REPO_ROOT}/src/bsp/host/host_link.c
    ${REPO_ROOT}/src/bsp/host/host_pcm.c
    ${REPO_ROOT}/src/bsp/host/host_midi.c
    ${REPO_ROOT}/src/bsp/host/host_render_main.cpp
    ${REPO_ROOT}/src/bsp/host/host_debug.cpp
)
# host_debug.cpp provides deluge_host_debug_poll() (called from host_audio.c) + reaches the app's reason_check;
# src/deluge on the path for memory/reason_check.h (the boundary check only forbids app→BSP, not BSP→app).
target_include_directories(deluge_render PRIVATE ${HOST_INCLUDE_DIRS} ${REPO_ROOT}/src/deluge)
target_link_libraries(deluge_render PRIVATE deluge_app deluge_scheduler ${SIM_ALLOC_LINK})
if(RT_LIB)
    target_link_libraries(deluge_render PRIVATE ${RT_LIB})
endif()

# deluge_loadcheck: headless "load these audio files, print their parsed descriptors" utility — the Tier-2
# regression net for the file-loading redesign. Same app + host BSP as deluge_render, with its own
# main()/driver (host_loadcheck_main.cpp) that drives AudioFileManager::getAudioFileFromFilename (and so
# ClusterByteSource + the parser) against a FAT image. Requires mtools at runtime (--project packing).
add_executable(deluge_loadcheck
    ${REPO_ROOT}/src/bsp/host/host_bsp.c
    ${REPO_ROOT}/src/bsp/host/host_platform.c
    ${REPO_ROOT}/src/bsp/host/host_audio.c
    ${REPO_ROOT}/src/bsp/host/host_link.c
    ${REPO_ROOT}/src/bsp/host/host_pcm.c
    ${REPO_ROOT}/src/bsp/host/host_midi.c
    ${REPO_ROOT}/src/bsp/host/host_loadcheck_main.cpp
    ${REPO_ROOT}/src/bsp/host/host_debug.cpp
)
# host_debug.cpp provides deluge_host_debug_poll() (called from host_audio.c) + reaches the app's reason_check;
# src/deluge on the path for memory/reason_check.h (the boundary check only forbids app→BSP, not BSP→app).
target_include_directories(deluge_loadcheck PRIVATE ${HOST_INCLUDE_DIRS} ${REPO_ROOT}/src/deluge)
target_link_libraries(deluge_loadcheck PRIVATE deluge_app deluge_scheduler ${SIM_ALLOC_LINK})
if(RT_LIB)
    target_link_libraries(deluge_loadcheck PRIVATE ${RT_LIB})
endif()

# ---------------------------------------------------------------------------
# Layout-invariance guard (CTest). Render each fixture at two heap-pad layouts
# (DELUGE_SIM_HEAP_PAD via scripts/golden_mixdown.sh padsweep) and assert the
# outputs are bit-identical — proving no render depends on allocation addresses.
# This is the regression net for the shared-PRNG / overread class of layout-
# coupled bug (a passing `golden check` does NOT prove this; it's pinned to one
# layout). The real backup songs live only on the developer's machine
# (DELUGE_BACKUP), so each test SKIPs (exit 77) when its fixture is absent — a
# fixtureless CI runner skips rather than fails. Slow (two full renders each);
# build deluge_render first, then opt in with `ctest -L padsweep`.
enable_testing()
foreach(fixture cordae highsiderr icoustic)
    add_test(
        NAME padsweep_${fixture}
        COMMAND ${CMAKE_COMMAND} -E env
                FIXTURE=${fixture} PADS=64 NO_BUILD=1 BUILD_DIR=${CMAKE_BINARY_DIR}
                bash ${REPO_ROOT}/scripts/golden_mixdown.sh padsweep
    )
    set_tests_properties(padsweep_${fixture} PROPERTIES
        SKIP_RETURN_CODE 77
        LABELS padsweep
        TIMEOUT 1200
    )
endforeach()
