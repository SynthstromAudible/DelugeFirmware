# clang cross toolchain for 32-bit ARM Linux (Cortex-A9 with NEON, hard float), for the host builds that run under
# qemu-arm: the arm-linux host app (golden_vt_render's ARM build) and the ARM unit tree.
#
# clang, because the firmware ships built with it. The target is Linux, not the device's bare metal, so it takes the
# C library and libstdc++ from the GCC cross toolchain's sysroot: arm-linux-gnueabihf-gcc must be installed.

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)
set(CMAKE_ASM_COMPILER clang)
set(CMAKE_C_COMPILER_TARGET arm-linux-gnueabihf)
set(CMAKE_CXX_COMPILER_TARGET arm-linux-gnueabihf)
set(CMAKE_ASM_COMPILER_TARGET arm-linux-gnueabihf)
set(CMAKE_SYSROOT /usr/arm-linux-gnueabihf)

set(_armflags "-mcpu=cortex-a9 -mfpu=neon -mfloat-abi=hard --gcc-toolchain=/usr")
set(CMAKE_C_FLAGS_INIT "${_armflags}")
set(CMAKE_CXX_FLAGS_INIT "${_armflags}")
set(CMAKE_ASM_FLAGS_INIT "${_armflags}")

# Run test binaries through qemu.
set(CMAKE_CROSSCOMPILING_EMULATOR /usr/bin/qemu-arm -L /usr/arm-linux-gnueabihf -cpu cortex-a9)

# Find host tools (python codegen) on the host, libraries and headers in the target sysroot.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
