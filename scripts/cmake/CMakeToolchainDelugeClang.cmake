# Clang variant of CMakeToolchainDeluge.cmake.
#
# Compiles the portable C++ application with clang for armv7a-none-eabihf while
# taking the entire C/C++ library stack (newlib, libstdc++, libsupc++, libgcc)
# from the bundled arm-none-eabi-gcc sysroot. Clang is only ever the front end;
# no clang runtime, no libc++, no picolibc.
#
# Why the sysroot rather than a self-contained clang: the GCC tree already ships
# a prebuilt NEON hard-float multilib (thumb/v7-a+simd/hard) matching this
# firmware's flags exactly, so clang-built objects link against the very same
# libraries GCC-built ones do.
#
# See dbt-toolchain docs/superpowers/specs/2026-08-11-bare-metal-llvm-clang-design.md

set(CMAKE_SYSTEM_NAME               Generic)
set(CMAKE_SYSTEM_PROCESSOR          arm)

if(DEFINED ENV{DBT_TOOLCHAIN_PATH})
  set(TOOLCHAIN_ROOT $ENV{DBT_TOOLCHAIN_PATH})
else()
  set(TOOLCHAIN_ROOT ".")
endif()

if(DEFINED ENV{DELUGE_FW_ROOT})
  set(FIRMWARE_ROOT $ENV{DELUGE_FW_ROOT})
else()
  set(FIRMWARE_ROOT ${CMAKE_SOURCE_DIR})
endif()

file(READ ${FIRMWARE_ROOT}/toolchain/REQUIRED_VERSION TOOLCHAIN_VERSION)
string(STRIP ${TOOLCHAIN_VERSION} TOOLCHAIN_VERSION)

set(TOOLCHAIN_TRIPLE ${CMAKE_HOST_SYSTEM_NAME})
string(TOLOWER ${TOOLCHAIN_TRIPLE} TOOLCHAIN_TRIPLE)
set(TOOLCHAIN_TRIPLE "${TOOLCHAIN_TRIPLE}-${CMAKE_HOST_SYSTEM_PROCESSOR}")

cmake_path(SET ARM_TOOLCHAIN_ROOT ${TOOLCHAIN_ROOT}/toolchain/v${TOOLCHAIN_VERSION}/${TOOLCHAIN_TRIPLE}/arm-none-eabi-gcc/)
cmake_path(ABSOLUTE_PATH ARM_TOOLCHAIN_ROOT)

set(DELUGE_SYSROOT ${ARM_TOOLCHAIN_ROOT}/arm-none-eabi)
set(ARM_TOOLCHAIN_BIN_PATH ${ARM_TOOLCHAIN_ROOT}/bin)

# The multilib these flags select. Clang does NOT implement GCC's multilib
# selection for Arm (it warns -Wmultilib-not-found and otherwise reaches for the
# soft-float base multilib), so every path below is named explicitly.
set(DELUGE_MULTILIB "thumb/v7-a+simd/hard")

# libstdc++ headers are per-multilib: bits/c++config.h exists once per variant
# AND as a soft-float base copy. Without the explicit include below, clang either
# errors ('bits/c++config.h' file not found) or silently takes the soft-float
# copy. Discover the version dir rather than hardcoding 15.2.1.
file(GLOB DELUGE_CXX_INCLUDE_DIRS ${DELUGE_SYSROOT}/include/c++/*)
list(GET DELUGE_CXX_INCLUDE_DIRS 0 DELUGE_CXX_INCLUDE)
file(GLOB DELUGE_GCC_LIB_DIRS ${ARM_TOOLCHAIN_ROOT}/lib/gcc/arm-none-eabi/*)
list(GET DELUGE_GCC_LIB_DIRS 0 DELUGE_GCC_LIB)

# Host clang; ATfE's clang once the toolchain ships one.
find_program(DELUGE_CLANG   NAMES clang   REQUIRED)
find_program(DELUGE_CLANGXX NAMES clang++ REQUIRED)

set(CMAKE_C_COMPILER   ${DELUGE_CLANG}   CACHE FILEPATH "Path to C Compiler.")
set(CMAKE_CXX_COMPILER ${DELUGE_CLANGXX} CACHE FILEPATH "Path to C++ Compiler.")

# Assembly stays with GCC. The .S sources use GNU as directives clang's
# integrated assembler does not implement (.func/.endfunc, in chainload.S and
# the RZA1 asm) — debug-info aids with no effect on codegen. Handing them to
# arm-none-eabi-gcc keeps those sources working unchanged for both toolchains.
# (-fno-integrated-as is NOT the fix: clang then reaches for the host
# /usr/bin/as, which promptly rejects -EL.)
set(CMAKE_ASM_COMPILER ${ARM_TOOLCHAIN_BIN_PATH}/arm-none-eabi-gcc CACHE FILEPATH "Path to ASM compiler.")

# Binutils stay GCC's: they understand this sysroot's archives and the ARM
# attributes, and nothing here needs llvm-* equivalents.
set(CMAKE_AR      ${ARM_TOOLCHAIN_BIN_PATH}/arm-none-eabi-ar      CACHE FILEPATH "Path to archiver.")
set(CMAKE_LINKER  ${ARM_TOOLCHAIN_BIN_PATH}/arm-none-eabi-ld      CACHE FILEPATH "Path to linker.")
set(CMAKE_OBJCOPY ${ARM_TOOLCHAIN_BIN_PATH}/arm-none-eabi-objcopy CACHE FILEPATH "Path to objcopy.")
set(CMAKE_RANLIB  ${ARM_TOOLCHAIN_BIN_PATH}/arm-none-eabi-ranlib  CACHE FILEPATH "Path to ranlib.")
set(CMAKE_SIZE    ${ARM_TOOLCHAIN_BIN_PATH}/arm-none-eabi-size    CACHE FILEPATH "Path to size.")
set(CMAKE_STRIP   ${ARM_TOOLCHAIN_BIN_PATH}/arm-none-eabi-strip   CACHE FILEPATH "Path to strip.")
set(CMAKE_NM      ${ARM_TOOLCHAIN_BIN_PATH}/arm-none-eabi-nm      CACHE FILEPATH "Path to list symbols.")
set(CMAKE_OBJDUMP ${ARM_TOOLCHAIN_BIN_PATH}/arm-none-eabi-objdump CACHE FILEPATH "Path to dump objects.")

# Static library: clang cannot drive a bare-metal link without the explicit
# library paths, and deluge_app is an OBJECT library anyway.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(CMAKE_ASM_FLAGS_RELEASE "-DNDEBUG" CACHE STRING "" FORCE)
set(CMAKE_C_FLAGS_RELEASE   "-DNDEBUG" CACHE STRING "" FORCE)
set(CMAKE_CXX_FLAGS_RELEASE "-DNDEBUG" CACHE STRING "" FORCE)

# Architecture. Mirrors CMakeToolchainDeluge.cmake with one deliberate change:
# -mfpu=neon-fp16 rather than -mfpu=neon.
#
# LLVM's own cortex-a9 model enables +fp16 by default, so rustc has it on while
# clang's -mfpu=neon explicitly turns it OFF. That single difference makes the
# Rust callee's feature set a non-subset of the C++ caller's, and LLVM then
# refuses every cross-language inline — the whole point of the lld/LTO work.
# neon-fp16 also replaces a `bl __aeabi_h2f` call with one vcvtb.f32.f16, and
# selects the same thumb/v7-a+simd/hard multilib.
#
# Half-precision is a property of the Cortex-A9 core, not of Renesas' integration:
# the NEON MPE this part demonstrably has (existing builds run with -mfpu=neon)
# brings VFPv3 with the half-precision extension. Both toolchains agree
# independently — LLVM's cortex-a9 model enables +fp16 by default, and GCC's
# -mfpu=auto for -mcpu=cortex-a9 reports __ARM_FP=14 (half+single+double) where
# the explicit -mfpu=neon reports 12. So -mfpu=neon is subtracting a capability
# the CPU has, and neon-fp16 restores it.
# -mno-unaligned-access: REQUIRED, and it must stay in lockstep with the Rust side.
#
# Without it the optimiser merges byte accesses into one wide load/store — e.g. a `char*`
# memcpy of 4 bytes becomes a single `ldr`. That is only valid where unaligned access is
# permitted, and on this part it is not: an unaligned `ldr` faults when SCTLR.A is set, and an
# unaligned `ldrd`/`ldm` faults regardless of SCTLR.A. The observed symptom was a boot-time DABT
# with DFSR=0x1 (alignment fault) inside `core::fmt::write`, reached from the RTT writer, on a
# perfectly legal `&str` that happened to sit at an odd address — string data is align-1, so
# there was no bug at the call site at all, only codegen assuming a capability the hardware does
# not offer here.
#
# Keep this identical to `src/bsp/rust/armv7a-deluge-eabihf.json`'s `+strict-align`. The custom
# Rust target exists to make the two feature sets MATCH (LLVM only inlines across languages when
# the callee's features are a subset of the caller's), so adding strict-align to one side alone
# silently switches cross-language inlining back off — which is the whole reason that target
# exists. Change both or neither.
set(ARCH_FLAGS
  --target=armv7a-none-eabihf
  -mcpu=cortex-a9
  -mfpu=neon-fp16
  -mfloat-abi=hard
  -mthumb
  -mlittle-endian
  -mno-unaligned-access
)

# The GCC sysroot, named explicitly because clang will not infer any of it.
set(SYSROOT_FLAGS
  --sysroot=${DELUGE_SYSROOT}
  --gcc-toolchain=${ARM_TOOLCHAIN_ROOT}
  -stdlib=libstdc++
)

# GCC spelling of the same architecture, for the assembler. No --target, no
# --sysroot/--gcc-toolchain/-stdlib: those are clang driver options and
# arm-none-eabi-gcc rejects them.
# No -mno-unaligned-access here: hand-written assembly chooses its own access widths, so the
# flag has nothing to constrain, and arm-none-eabi-gcc is only being used as the assembler.
set(ASM_ARCH_FLAGS
  -mcpu=cortex-a9
  -mfpu=neon-fp16
  -mfloat-abi=hard
  -mthumb
  -mlittle-endian
)

# Per-flag genexes rather than one wrapping the whole list: a list inside a
# generator expression keeps its semicolons and arrives as a single argument.
foreach(flag IN LISTS ARCH_FLAGS SYSROOT_FLAGS)
  add_compile_options("$<$<COMPILE_LANGUAGE:C,CXX>:${flag}>")
endforeach()
foreach(flag IN LISTS ASM_ARCH_FLAGS)
  add_compile_options("$<$<COMPILE_LANGUAGE:ASM>:${flag}>")
endforeach()

add_link_options(${ARCH_FLAGS} ${SYSROOT_FLAGS})

# The shared warning set names GCC-only options (-Warray-bounds=1,
# -Wstack-usage=). They are harmless but produce a warning per translation unit.
add_compile_options($<$<COMPILE_LANGUAGE:C,CXX>:-Wno-unknown-warning-option>)

# Multilib-correct libstdc++ configuration header (C++ only).
add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-isystem${DELUGE_CXX_INCLUDE}/arm-none-eabi/${DELUGE_MULTILIB}>)

add_compile_options(
  -fmessage-length=0
  -funsafe-math-optimizations # required to use NEON instead of VFPv3 for floating point
)

# validateParams()'s static_assert in modulation/params/param.cpp walks far
# enough to exceed clang's default constexpr budget (GCC's is higher). This is
# an evaluation-limit difference, not a defect in the assertion.
add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-fconstexpr-steps=100000000>)

# -mthumb-interwork has no clang equivalent and is a no-op on v7-A, so it is
# dropped rather than translated.

set(CMAKE_FIND_ROOT_PATH ${DELUGE_SYSROOT})
set(CMAKE_SYSROOT ${DELUGE_SYSROOT})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
