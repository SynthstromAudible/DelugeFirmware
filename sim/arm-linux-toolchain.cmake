# Cross-toolchain for the arm-linux/NEON reference build of the host-sim.
#
# Builds the SAME portable app + host BSP as the x86 host-sim, but targeting 32-bit
# ARM with real NEON (Cortex-A9), run under qemu-arm. This exercises the firmware's
# native code paths — the `#if defined(__arm__)` fixedpoint asm, argon's native NEON
# backend — giving a NEON reference to diff the x86/SIMDe host-sim against (the audio
# WAV-diff harness). It is NOT the firmware build (that is bare-metal arm-none-eabi);
# it is a Linux/qemu stand-in just for differential DSP testing.
#
# Compiled with clang, as the firmware is: the reference must take the code generation that ships. clang uses the GCC
# cross toolchain's sysroot for the C library and libstdc++, so arm-linux-gnueabihf-gcc must still be installed.
#
#   cmake -B build/host-app-arm -S sim -G Ninja \
#         -DCMAKE_TOOLCHAIN_FILE=sim/arm-linux-toolchain.cmake
#   ninja -C build/host-app-arm <targets>

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)
set(CMAKE_C_COMPILER_TARGET arm-linux-gnueabihf)
set(CMAKE_CXX_COMPILER_TARGET arm-linux-gnueabihf)
set(CMAKE_SYSROOT /usr/arm-linux-gnueabihf)

set(_armflags "-mcpu=cortex-a9 -mfpu=neon -mfloat-abi=hard --gcc-toolchain=/usr")
set(CMAKE_C_FLAGS_INIT "${_armflags}")
set(CMAKE_CXX_FLAGS_INIT "${_armflags}")

# Run test/host binaries through qemu.
set(CMAKE_CROSSCOMPILING_EMULATOR /usr/bin/qemu-arm -L /usr/arm-linux-gnueabihf -cpu cortex-a9)

# Find host tools (python codegen) on the host, libraries/headers in the target sysroot.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

# Signal to sim/CMakeLists.txt to take the native-NEON path (no SIMDe, no -m32, real arm_neon.h).
set(DELUGE_SIM_ARM ON)
