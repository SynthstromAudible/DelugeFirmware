// The plate's LFO pair (dsp-next/float/reverb/plate_lfo.h) against the reference's FxEngine, and what it runs at.
#include "dsp-next/float/reverb/plate_lfo.h"
#include "reference_reverb.h"
#include <cppspec.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace {
using deluge::dsp::fp::PlateLfo;
namespace ref = deluge::dsp::test::reverb_ref;

/// The plates' ring, whose write index paces the LFO.
constexpr int32_t kRing = 32768;
/// The frequencies every plate builds its engine with.
constexpr std::array<float, 2> kFrequencies{0.5f / 44100.f, 0.3f / 44100.f};
/// Seconds of trace the frequency checks measure over.
constexpr double kTraceSeconds = 40.0;

/// @brief The kernel driven as a plate drives its engine: the write index steps down and wraps every frame, and
/// the LFO steps when the index's low five bits are zero.
struct KernelDriver {
	PlateLfo::Coefficients c = PlateLfo::coefficients({.frequency_1 = kFrequencies[0], .frequency_2 = kFrequencies[1]});
	PlateLfo::State s{};
	int32_t write = 0;
	KernelDriver() { PlateLfo::start(c, s); }
	/// @return Both lanes after this frame's advance.
	std::array<float, 2> frame() {
		if (--write < 0) {
			write += kRing;
		}
		if ((write & 31) == 0) {
			PlateLfo::advance(c, s, PlateLfo::kStride);
		}
		std::array<float, 2> lanes{};
		PlateLfo::value(s).StoreTo(lanes.data());
		return lanes;
	}
};

struct LfoTrace {
	double hz_1;
	double hz_2;
	float divergence; ///< Largest |LFO_1 − LFO_2| over the trace.
	bool finite;      ///< Every lane finite on every frame.
};

/// @return The kernel's trace over kTraceSeconds: upward midpoint crossings per second of each lane, how far the
/// lanes stray from each other, and whether every value is finite.
LfoTrace trace_kernel() {
	KernelDriver driver;
	LfoTrace trace{.hz_1 = 0.0, .hz_2 = 0.0, .divergence = 0.0f, .finite = true};
	std::size_t crossings_1 = 0;
	std::size_t crossings_2 = 0;
	std::array<float, 2> previous{};
	const auto frames = static_cast<std::size_t>(kTraceSeconds * 44100.0);
	for (std::size_t frame = 0; frame < frames; ++frame) {
		const std::array<float, 2> lfo = driver.frame();
		trace.finite = trace.finite && std::isfinite(lfo[0]) && std::isfinite(lfo[1]);
		trace.divergence = std::max(trace.divergence, std::abs(lfo[0] - lfo[1]));
		if (frame > 0) {
			crossings_1 += (previous[0] <= 0.5f && lfo[0] > 0.5f) ? 1 : 0;
			crossings_2 += (previous[1] <= 0.5f && lfo[1] > 0.5f) ? 1 : 0;
		}
		previous = lfo;
	}
	trace.hz_1 = static_cast<double>(crossings_1) / kTraceSeconds;
	trace.hz_2 = static_cast<double>(crossings_2) / kTraceSeconds;
	return trace;
}
} // namespace

// clang-format off
describe plate_lfo("the plate's LFO pair", ${
	it("starts and steps as the reference's FxEngine does", _ {
		std::vector<float> buffer(kRing);
		ref::FxEngine engine{buffer, kFrequencies};
		KernelDriver driver;
		std::size_t mismatches = 0;
		for (int32_t frame = 0; frame < 200000; ++frame) {
			engine.Advance();
			const std::array<float, 2> lanes = driver.frame();
			mismatches += std::bit_cast<uint32_t>(lanes[0]) != std::bit_cast<uint32_t>(engine.LFO(ref::LFO_1)) ? 1 : 0;
			mismatches += std::bit_cast<uint32_t>(lanes[1]) != std::bit_cast<uint32_t>(engine.LFO(ref::LFO_2)) ? 1 : 0;
		}
		expect(mismatches).to_equal(std::size_t{0});
	});

	context("over a 40 s trace", _ {
		const LfoTrace trace = trace_kernel();

		it("runs LFO_1 at about 0.5 Hz and LFO_2 at about 0.3 Hz", _ {
			expect(trace.hz_1).to_be_between(0.40, 0.55);
			expect(trace.hz_2).to_be_between(0.24, 0.34);
		});

		it("keeps the two modulators independent", _ {
			// The approximate cosine's coefficient is 2 − 32f²; unstrided, f is small enough that it rounds to 2 in
			// both lanes and the two LFOs fuse into one.
			expect(trace.divergence).to_be_greater_than(0.1f);
		});

		it("keeps both lanes finite from the first step", _ {
			expect(trace.finite).to_be_true();
		});
	});
});
// clang-format on

CPPSPEC_MAIN(plate_lfo);
