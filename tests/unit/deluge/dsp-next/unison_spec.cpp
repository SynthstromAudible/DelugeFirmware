#include "modulation/params/param.h"
#include "support/oracles/phase_increment_fine_tuner.h"
#include "synth/matrix/param_format.h"
#include "synth/voice/unison.h"

#include <array>
#include <cmath>
#include <cppspec.hpp>
#include <cstdint>

#include "support/spec_main.h"

using deluge::synth::derive_unison;
using deluge::synth::UnisonLayout;
using deluge::test::oracle::PhaseIncrementFineTuner;

namespace {

/// The layout as Sound computed it before the derivation moved to the synth library (computeUnisonDetuners,
/// setupUnisonStereoSpread): the oracle. Parts are filled only for a count other than one.
struct Recorded {
	std::array<PhaseIncrementFineTuner, kMaxNumVoicesUnison> detuners{};
	std::array<int32_t, kMaxNumVoicesUnison> pans{};
};

Recorded recorded(int32_t count, int32_t detune, int32_t spread) {
	Recorded r;
	if (count != 1) {
		const int32_t detune_scaled = detune * 42949672;
		const int32_t lowest = -(detune_scaled >> 1);
		const int32_t spacing = detune_scaled / (count - 1);
		for (int32_t u = 0; u < count; u++) {
			if ((count & 1) != 0 && u == ((count - 1) >> 1)) {
				r.detuners[u].setNoDetune();
			}
			else {
				r.detuners[u].setup(lowest + spacing * u);
			}
		}
		const int32_t spread_scaled = spread * 42949672;
		const int32_t lowest_pan = -(spread_scaled >> 1);
		const int32_t pan_spacing = spread_scaled / (count - 1);
		for (int32_t u = 0; u < count; u++) {
			const bool odd = (std::min(u, count - 1 - u) & 1) != 0;
			r.pans[u] = (odd ? -1 : 1) * (lowest_pan + pan_spacing * u);
		}
	}
	return r;
}

/// Whether @p layout matches the recorded arithmetic for parts below its count.
bool matches(const UnisonLayout& layout, int32_t count, int32_t detune, int32_t spread) {
	if (count == 1) {
		return true; // the parts are not read
	}
	const Recorded r = recorded(count, detune, spread);
	for (int32_t u = 0; u < count; u++) {
		for (const int32_t increment : {1 << 20, 1 << 28, 1 << 30}) {
			if (layout.detunes[u].apply(increment) != r.detuners[u].detune(increment)) {
				return false;
			}
		}
		if (layout.pans[u].raw() != r.pans[u]) {
			return false;
		}
	}
	return true;
}

} // namespace

// clang-format off
describe unison("a sound's unison layout", ${
	it("detunes and pans each part as the sound did", _ {
		for (int32_t count = 1; count <= kMaxNumVoicesUnison; count++) {
			for (int32_t detune = 0; detune <= 50; detune++) {
				for (int32_t spread = 0; spread <= 50; spread += 5) {
					const UnisonLayout layout = derive_unison(static_cast<uint8_t>(count), static_cast<int8_t>(detune),
					                                          static_cast<uint8_t>(spread));
					expect(matches(layout, count, detune, spread)).to_equal(true);
				}
			}
		}
	});

	it("scales each part to the volume's neutral over the root of the count", _ {
		constexpr float neutral =
		    deluge::dsp::to_float(deluge::synth::matrix::param_neutral<deluge::modulation::params::LOCAL_VOLUME>());
		for (int32_t count = 1; count <= kMaxNumVoicesUnison; count++) {
			const UnisonLayout layout = derive_unison(static_cast<uint8_t>(count), 8, 0);
			expect(layout.level.raw()).to_equal(deluge::dsp::from_float<31>(neutral / sqrtf(count)).raw());
		}
	});

	it("leaves a single part undetuned and unpanned", _ {
		const UnisonLayout layout = derive_unison(1, 30, 40);
		expect(layout.detunes[0] == deluge::dsp::fixed::FineTune{}).to_equal(true);
		expect(layout.pans[0].raw()).to_equal(0);
	});
});
// clang-format on

DELUGE_SPEC(unison_spec, unison)
