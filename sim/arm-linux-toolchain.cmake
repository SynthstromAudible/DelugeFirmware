# Cross-toolchain for the arm-linux/NEON reference build of the host-sim.
#
# Builds the SAME portable app + host BSP as the x86 host-sim, but targeting 32-bit ARM with real NEON (Cortex-A9),
# run under qemu-arm. This exercises the firmware's native code paths -- the `#if defined(__arm__)` fixedpoint asm,
# argon's native NEON backend -- giving a NEON reference to diff the x86/SIMDe host-sim against. It is NOT the
# firmware build (that is bare-metal arm-none-eabi); it is a Linux/qemu stand-in for differential testing.
#
#   cmake -B build/host-app-arm -S sim -G Ninja -DCMAKE_TOOLCHAIN_FILE=sim/arm-linux-toolchain.cmake
#   ninja -C build/host-app-arm <targets>

include(${CMAKE_CURRENT_LIST_DIR}/../cmake/arm-linux-clang.cmake)

# Signal to sim/CMakeLists.txt to take the native-NEON path (no SIMDe, no -m32, real arm_neon.h).
set(DELUGE_SIM_ARM ON)
