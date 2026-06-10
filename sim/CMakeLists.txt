# Host-sim build for the portable Deluge application.
#
# A NATIVE build (x86-64 / ARM64) — deliberately separate from the arm-none-eabi
# firmware build (whose top-level CMakeLists hard-codes the cross toolchain). It
# compiles src/deluge against the host BSP (src/bsp/host), which implements the
# <libdeluge/...> boundary. Two payoffs: it is the definitive proof the app has no
# transitive HAL dependency (no RZA1/ on the include path), and it is the substrate
# for host CI + the audio WAV-diff harness.
#
# Configure/run:
#   cmake -B build-sim -S sim -G Ninja
#   cmake --build build-sim
#   ./build-sim/deluge_host
#
# STAGE: this currently builds the BSP-boundary smoke target (deluge_host_boundary)
# — proving the native toolchain + the <libdeluge/...> contract compile/link/run
# off-target. The full-app target is wired up incrementally on top of this.

cmake_minimum_required(VERSION 3.24)
project(DelugeHostSim C CXX)

set(CMAKE_C_STANDARD 23)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(REPO_ROOT ${CMAKE_CURRENT_LIST_DIR}/..)

# ---------------------------------------------------------------------------
# Include paths shared by every host target. Order matters: sim/compat first so
# the app's `#include <arm_neon.h>` resolves to the SIMDe shim, not a system one.
# ---------------------------------------------------------------------------
set(HOST_INCLUDE_DIRS
    ${CMAKE_CURRENT_LIST_DIR}/compat   # arm_neon.h -> SIMDe shim
    ${REPO_ROOT}/include               # <libdeluge/...> boundary
    ${REPO_ROOT}/src                   # SHARED_INCLUDE (RZA1-free app + foundation)
)

# Stage 1: the boundary smoke target — host BSP + a main that exercises it. No app
# sources yet; proves the native build and the C-ABI contract are sound off-target.
add_executable(deluge_host_boundary
    ${REPO_ROOT}/src/bsp/host/host_bsp.c
    ${REPO_ROOT}/src/bsp/host/host_main.c
)
target_include_directories(deluge_host_boundary PRIVATE ${HOST_INCLUDE_DIRS})

# ---------------------------------------------------------------------------
# NE10 (the ARM NEON int32 FFT the DSP uses), built native. Its compute is all
# NEON *intrinsics* (the .neonintrinsic files) + plain C — no hand-written
# assembly — so the intrinsics route through the SIMDe shim exactly like argon.
# The only non-portable NE10 sources are common/*.s (assembler-constant + version
# boilerplate, nothing the C links against); they are simply omitted on host.
# ---------------------------------------------------------------------------
add_library(deluge_ne10 STATIC
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
target_include_directories(deluge_ne10 PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/compat # arm_neon.h -> SIMDe shim
    ${REPO_ROOT}/src/NE10/inc
    ${REPO_ROOT}/src/NE10/common
    ${REPO_ROOT}/src/NE10
)
# NE10_fft_int32.c was patched by the Deluge to call routineWithClusterLoading()
# (the long FFT yields to audio/SD), so it needs deluge/deluge.h to compile; the
# symbol resolves at link with the app.
target_include_directories(deluge_ne10 PRIVATE
    ${REPO_ROOT}/src      # deluge/deluge.h + SHARED_INCLUDE
    ${REPO_ROOT}/include  # <libdeluge/...>
)

# clock_gettime lives in libc on modern glibc; link rt for older toolchains.
find_library(RT_LIB rt)
if(RT_LIB)
    target_link_libraries(deluge_host_boundary PRIVATE ${RT_LIB})
endif()
