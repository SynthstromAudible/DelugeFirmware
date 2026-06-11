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
set(DELUGE_CXX_STANDARD 23)
set(CMAKE_C_STANDARD 23)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)
cmake_path(SET SHARED_INCLUDE ${REPO_ROOT}/src)

# The app is written for a 32-bit target (it stores pointers in uint32_t etc.), so
# build the host as 32-bit — matching the existing tests (tests/32bit_unit_tests
# use -m32) and the firmware's memory model. (Requires a multilib toolchain.)
# The arm-linux reference toolchain is already 32-bit, so -m32 only applies to x86.
# Three host configurations:
#   default            x86-32 — matches the firmware's 32-bit pointer model (the app
#                      stores pointers in uint32_t fields). SIMDe runs its portable-C
#                      fallbacks (the native -m32 path won't parse <immintrin.h>).
#   DELUGE_SIM_X64=ON  x86-64 — lets SIMDe use real SSE/AVX (faster, and sidesteps the
#                      vector_size(8)->MMX-register ABI hazard entirely). EXPERIMENTAL:
#                      requires the app to be 64-bit-pointer-clean.
#   DELUGE_SIM_ARM=ON  arm-linux/NEON cross (the native-SIMD reference oracle).
if(NOT DELUGE_SIM_ARM AND NOT DELUGE_SIM_X64)
    add_compile_options(-m32)
    add_link_options(-m32)

    # Use SSE for float math instead of the 32-bit x87 default. x87 evaluates float
    # intermediates at 80-bit extended precision, which silently diverges from the
    # firmware's strict IEEE-754 single-precision VFP — enough to perturb marginally
    # stable float feedback loops (e.g. the Mutable reverb) away from the device's
    # behaviour. -mfpmath=sse keeps host float arithmetic bit-comparable to the target.
    # (64-bit already defaults to SSE, so this only applies to the -m32 build.)
    add_compile_options(-msse2 -mfpmath=sse)
endif()

# Char signedness matches the firmware (ARM default is unsigned; be explicit).
set(DELUGE_COMMON_COMPILE_OPTIONS -fsigned-char)
add_compile_options(-fsigned-char)

# Debug info — this is a host debug / CI harness; -g makes the inevitable runtime
# triage (gdb backtraces, member inspection) tractable.
add_compile_options(-g)

# Section GC, mirroring the firmware link. Beyond size, it strips genuinely-dead code
# whose refs would otherwise fail to resolve — e.g. the NE10 float32 FFT path (Deluge
# only uses the int32 FFT), which references ne10_fft_alloc_c2c_float32_c (never compiled).
add_compile_options(-ffunction-sections -fdata-sections)
add_link_options(LINKER:--gc-sections)

# SIMDe (x86 host only): argon's non-ARM backend does `#include <arm/neon.h>`, which must
# resolve to <simde/arm/neon.h>. The arm-linux reference has real NEON and a real <arm_neon.h>,
# so it skips all of this.
if(NOT DELUGE_SIM_ARM)
    find_path(SIMDE_INCLUDE_DIR simde/arm/neon.h PATHS /usr/include /usr/local/include REQUIRED)

    if(NOT DELUGE_SIM_X64)
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
if(DELUGE_SIM_ARM)
    # Real NEON: the cross toolchain provides <arm_neon.h>; argon's <arm/neon.h> is unused.
    include_directories(BEFORE ${REPO_ROOT}/include)
    set(HOST_INCLUDE_DIRS ${REPO_ROOT}/include ${REPO_ROOT}/src)
else()
    include_directories(BEFORE
        ${CMAKE_CURRENT_LIST_DIR}/compat # <arm_neon.h> -> SIMDe shim
        ${REPO_ROOT}/include             # <libdeluge/...> boundary
        ${SIMDE_INCLUDE_DIR}/simde        # argon's <arm/neon.h> -> SIMDe
    )
    set(HOST_INCLUDE_DIRS
        ${CMAKE_CURRENT_LIST_DIR}/compat # arm_neon.h -> SIMDe shim
        ${REPO_ROOT}/include             # <libdeluge/...> boundary
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
    $<$<NOT:$<BOOL:${DELUGE_SIM_ARM}>>:${CMAKE_CURRENT_LIST_DIR}/compat> # SIMDe shim (x86 only)
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

# The host executable: the portable app + the host BSP + the scheduler. deluge_app
# is an OBJECT lib (its objects embed here); deluge_scheduler is STATIC (resolves
# against them). host_main drives the boundary for now; it becomes the host runtime
# loop (deluge_app_init/render/tick) as the app is brought up.
add_executable(deluge_host
    ${REPO_ROOT}/src/bsp/host/host_bsp.c
    ${REPO_ROOT}/src/bsp/host/host_platform.c
    ${REPO_ROOT}/src/bsp/host/host_audio.c
    ${REPO_ROOT}/src/bsp/host/host_main.c
)
target_include_directories(deluge_host PRIVATE ${HOST_INCLUDE_DIRS})
target_link_libraries(deluge_host PRIVATE deluge_app deluge_scheduler)
if(RT_LIB)
    target_link_libraries(deluge_host PRIVATE ${RT_LIB})
endif()
