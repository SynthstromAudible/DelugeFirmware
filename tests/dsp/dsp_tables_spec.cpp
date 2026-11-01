#include "dsp-next/fixed/tables/decay_small.h"
#include "dsp-next/fixed/tables/sine_small.h"
#include "synth/sources/sample/time_stretch_adjust.h"

#include <cppspec.hpp>
#include <cstdint>

using namespace deluge::dsp::fixed;

namespace {
/// FNV-1a over a table's values, as little-endian 32-bit words: a record of every entry.
template <typename Array>
constexpr uint32_t fingerprint(const Array& table) {
	uint32_t h = 2166136261u;
	for (auto v : table) {
		const auto w = static_cast<uint32_t>(static_cast<int32_t>(v));
		for (int b = 0; b < 32; b += 8) {
			h = (h ^ ((w >> b) & 0xFFu)) * 16777619u;
		}
	}
	return h;
}

// Recorded from the app's tables these replace (sineWaveSmall, decayTableSmall4/8, timeStretchAdjustTable).
constexpr uint32_t kSineSmallFingerprint = 0x5b634410;
constexpr uint32_t kDecaySmall4Fingerprint = 0x67b60de0;
constexpr uint32_t kDecaySmall8Fingerprint = 0xfe5011a2;
constexpr uint32_t kTimeStretchAdjustFingerprint = 0x35988bd8;
} // namespace

static_assert(tables::kSineSmall[0] == 0 && tables::kSineSmall[64] == 32767); // a quarter turn is +1.0
static_assert(tables::kDecaySmall8[0] == 65535);                              // the decay starts at 1.0
static_assert(fingerprint(tables::kSineSmall) == kSineSmallFingerprint);

// clang-format off
describe dsp_tables("dsp-next's lookup tables", ${
	it("hold the values the synth was recorded with", _ {
		expect(fingerprint(tables::kSineSmall)).to_equal(kSineSmallFingerprint);
		expect(fingerprint(tables::kDecaySmall4)).to_equal(kDecaySmall4Fingerprint);
		expect(fingerprint(tables::kDecaySmall8)).to_equal(kDecaySmall8Fingerprint);
		expect(fingerprint(deluge::synth::kTimeStretchAdjust)).to_equal(kTimeStretchAdjustFingerprint);
	});
});
// clang-format on

CPPSPEC_MAIN(dsp_tables);
