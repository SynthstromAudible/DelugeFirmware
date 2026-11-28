#include "instruments/classic/engine_params.h"
#include "instruments/classic/params.h"

#include <algorithm>
#include <array>
#include <cppspec.hpp>
#include <string>

using namespace deluge::instruments::classic;
using deluge::plugin::ParamId;

namespace {

constexpr std::array kModes{SynthMode::SUBTRACTIVE, SynthMode::RINGMOD, SynthMode::FM, SynthMode::DX7};

/// What the voice's DX7 controls read (synth/voice/voice.cpp `dx7_controls`), and the source-activity rules read in
/// DX7 mode: the DX7's own params and the first envelope's attack.
constexpr std::array kDx7Reads{param::kDx7Level, param::kDx7Pitch, param::kDx7AmpMod, param::kEnv1Attack};

/// What the oscillator engines' controls read (`dual_source_controls`, `fm_controls`), with the noise and both slots'
/// volumes the source-activity rules read.
constexpr std::array kOscillatorReads{param::kOscAVolume,
                                      param::kOscBVolume,
                                      param::kOscAPitch,
                                      param::kOscBPitch,
                                      param::kOscAPhaseWidth,
                                      param::kOscBPhaseWidth,
                                      param::kOscAWavetablePosition,
                                      param::kOscBWavetablePosition,
                                      param::kNoiseVolume,
                                      param::kCarrier1Feedback,
                                      param::kCarrier2Feedback,
                                      param::kModulator1Volume,
                                      param::kModulator2Volume,
                                      param::kModulator1Pitch,
                                      param::kModulator2Pitch,
                                      param::kModulator1Feedback,
                                      param::kModulator2Feedback,
                                      param::kEnv1Attack};

bool owned(ParamId id) {
	return std::ranges::find(kOscillatorEngineParams, id) != kOscillatorEngineParams.end()
	       || std::ranges::find(kDx7EngineParams, id) != kDx7EngineParams.end();
}

} // namespace

// clang-format off
describe engine_params("which engine owns each of the Deluge instrument's params", ${
	it("keeps every param the DX7 reads live in DX7 mode, and none of the oscillator engines'", _ {
		for (const ParamId id : kDx7Reads) {
			expect(param_is_live(SynthMode::DX7, id)).to_equal(true);
		}
		for (const ParamId id : kOscillatorEngineParams) {
			expect(param_is_live(SynthMode::DX7, id)).to_equal(false);
		}
	});

	it("keeps every param the oscillator engines read live in their modes, and none of the DX7's", _ {
		for (const SynthMode mode : {SynthMode::SUBTRACTIVE, SynthMode::RINGMOD, SynthMode::FM}) {
			for (const ParamId id : kOscillatorReads) {
				expect(param_is_live(mode, id)).to_equal(true);
			}
			for (const ParamId id : kDx7EngineParams) {
				expect(param_is_live(mode, id)).to_equal(false);
			}
		}
	});

	it("gives every param no engine owns, the sound's, to every mode", _ {
		for (const deluge::plugin::ParamInfo& row : kParams) {
			for (const SynthMode mode : kModes) {
				if (!owned(row.id)) {
					expect(std::string{row.file_name} + (param_is_live(mode, row.id) ? " live" : " parked"))
					    .to_equal(std::string{row.file_name} + " live");
				}
			}
		}
	});

	it("keeps a cable's depth live while the param it roots at is", _ {
		const ParamId on_osc = *cable_depth_id(PatchSource::LFO_GLOBAL_1, param::kOscAVolume);
		const ParamId on_dx7 = *cable_depth_id(PatchSource::ENVELOPE_1, *cable_depth_id(PatchSource::LFO_GLOBAL_1,
		                                                                                param::kDx7Level));
		expect(target_is_live(SynthMode::DX7, on_osc)).to_equal(false);
		expect(target_is_live(SynthMode::DX7, on_dx7)).to_equal(true);
		expect(target_is_live(SynthMode::FM, on_osc)).to_equal(true);
		expect(target_is_live(SynthMode::FM, on_dx7)).to_equal(false);
	});
});

CPPSPEC_SPEC(engine_params)
