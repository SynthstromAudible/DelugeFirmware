/*
 * Host shim for <arm_neon.h>.
 *
 * On non-ARM hosts (x86-64) the ARM NEON intrinsics header does not exist, and on
 * ARM64 hosts we still want one consistent path. SIMDe (SIMD Everywhere) implements
 * the NEON intrinsics on x86-64 (via SSE/AVX) and passes through to native NEON on
 * ARM64. SIMDE_ENABLE_NATIVE_ALIASES exposes the bare NEON names (int32x4_t,
 * vld1q_s32, ...) the DSP code and `argon` are written against.
 *
 * Placed on the host build's include path ahead of any system arm_neon.h so that
 * `#include <arm_neon.h>` in the portable app resolves here.
 */
#ifndef LIBDELUGE_HOST_ARM_NEON_SHIM_H
#define LIBDELUGE_HOST_ARM_NEON_SHIM_H

#ifndef SIMDE_ENABLE_NATIVE_ALIASES
#define SIMDE_ENABLE_NATIVE_ALIASES
#endif
#include <simde/arm/neon.h>

#endif // LIBDELUGE_HOST_ARM_NEON_SHIM_H
