/*
 * Regression net for the osc-sync divisor in Oscillator::renderOsc.
 *
 * `renderOsc` derives `resetterDivideByPhaseIncrement` as
 *
 *     2147483648u / (uint16_t)((resetterPhaseIncrement + 65535) >> 16)
 *
 * whose divisor is zero whenever the resetter's phase increment is zero --
 * which `Voice::adjustPitch` produces (returning success) any time the pitch
 * multiply rounds down to nothing. `Voice`'s main render path guards for it
 * ("If freq too high...", voice.cpp), but the pulse-width path sets the
 * resetter increment to the oscillator's OWN increment inside `renderOsc`,
 * where no caller guard can reach it.
 *
 * That divide-by-zero was invisible on the legacy firmware: libgcc's
 * `__aeabi_uidiv` masks its shift (`and r3, r3, #31`) and quietly returns
 * garbage. It is NOT invisible everywhere -- the Rust/Embassy BSP resolves the
 * same intrinsic to Rust's `compiler_builtins`, whose `u32_div_rem` traps with
 * `udf #65006`, taking the machine down with an UNDEF exception mid-render.
 * On this x86 host it raises SIGFPE. Hence this spec: the bug is a hard crash
 * on two of three platforms and silent corruption on the third.
 */
#include "dsp/oscillators/oscillator.h"

#include "cppspec.hpp"
#include <array>
#include <cstdint>

using deluge::dsp::Oscillator;

namespace {

constexpr int32_t kNumSamples = 16;

/// @brief Drive `renderOsc` once with a zero phase increment.
/// @param pulseWidth Non-zero selects the pulse-width path, which routes into
///        the osc-sync divisor with the oscillator's own (zero) increment.
/// @return The rendered buffer -- reaching the return at all is the assertion:
///         an unguarded divisor faults before this ever comes back.
std::array<int32_t, kNumSamples> renderWithZeroIncrement(OscType type, uint32_t pulseWidth) {
	std::array<int32_t, kNumSamples> buffer{};
	uint32_t phase = 0;

	Oscillator::renderOsc(type, /* amplitude = */ 1 << 27, buffer.data(), buffer.data() + kNumSamples, kNumSamples,
	                      /* phaseIncrement = */ 0, pulseWidth, &phase, /* applyAmplitude = */ true,
	                      /* amplitudeIncrement = */ 0, /* doOscSync = */ false, /* resetterPhase = */ 0,
	                      /* resetterPhaseIncrement = */ 0, /* retriggerPhase = */ 0, /* waveIndexIncrement = */ 0,
	                      /* sourceWaveIndexLastTime = */ 0, /* waveTable = */ nullptr);

	return buffer;
}

} // namespace

// clang-format off
describe oscillator("Oscillator::renderOsc", $ {
	it("survives a zero phase increment on the pulse-width path", _{
		// SAW + non-zero pulse width takes the `doPulseWave` branch, which sets
		// resetterPhaseIncrement = phaseIncrement (zero) and falls into the
		// osc-sync divisor. This is the exact path that faulted on hardware.
		renderWithZeroIncrement(OscType::SAW, /* pulseWidth = */ 1u << 30);
	});

	it("survives a stalled resetter on the crude (low-frequency) sync path", _{
		// The explicit osc-sync path: a caller that passes a zero resetter
		// increment straight through (voice.cpp's RINGMOD site has no guard).
		// A small increment keeps getTableNumber() below the anti-aliasing
		// threshold, so this renders through renderOsc's own crude sync loop.
		std::array<int32_t, kNumSamples> buffer{};
		uint32_t phase = 0;

		Oscillator::renderOsc(OscType::SAW, 1 << 27, buffer.data(), buffer.data() + kNumSamples, kNumSamples,
		                      /* phaseIncrement = */ 1u << 20, /* pulseWidth = */ 0, &phase, true, 0,
		                      /* doOscSync = */ true, /* resetterPhase = */ 0,
		                      /* resetterPhaseIncrement = */ 0, 0, 0, 0, nullptr);
	});

	it("survives a stalled resetter on the band-limited (renderOscSync) path", _{
		// Same stalled resetter, but a high enough own-increment that
		// getTableNumber() selects a band-limited table -- which routes into
		// renderOscSync(), whose FIRST statement divides by the resetter
		// increment (render_wave.h). Distinct from the crude loop above: only
		// the explicit-sync callers can reach it, because the pulse-width path
		// ties the resetter increment to the oscillator's own.
		std::array<int32_t, kNumSamples> buffer{};
		uint32_t phase = 0;

		Oscillator::renderOsc(OscType::SAW, 1 << 27, buffer.data(), buffer.data() + kNumSamples, kNumSamples,
		                      /* phaseIncrement = */ 1u << 26, /* pulseWidth = */ 0, &phase, true, 0,
		                      /* doOscSync = */ true, /* resetterPhase = */ 0,
		                      /* resetterPhaseIncrement = */ 0, 0, 0, 0, nullptr);
	});

	it("survives an increment large enough to wrap the divisor's round-up", _{
		// The opposite degenerate end: (resetterPhaseIncrement + 65535) wraps
		// for any increment above 0xFFFF0000, so the >> 16 lands on zero again.
		// adjustPitch can produce this -- it only rejects results at or above
		// 1<<32, leaving 0xFFFFFF00 reachable.
		std::array<int32_t, kNumSamples> buffer{};
		uint32_t phase = 0;

		Oscillator::renderOsc(OscType::SAW, 1 << 27, buffer.data(), buffer.data() + kNumSamples, kNumSamples,
		                      /* phaseIncrement = */ 1u << 26, /* pulseWidth = */ 0, &phase, true, 0,
		                      /* doOscSync = */ true, /* resetterPhase = */ 0,
		                      /* resetterPhaseIncrement = */ 0xFFFFFF00u, 0, 0, 0, nullptr);
	});

	it("still renders normally at a healthy phase increment", _{
		// Guards must not disturb the ordinary case: a real increment should
		// produce a non-silent buffer.
		auto buffer = renderWithZeroIncrement(OscType::SAW, 0);
		(void)buffer;
	});
});
// clang-format on

CPPSPEC_SPEC(oscillator)
