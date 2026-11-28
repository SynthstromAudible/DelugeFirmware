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

/// The ids each engine owns, pinned apart from the code's lists: the oscillator engines' slots' volume, pitch, pulse
/// width and wavetable position, the noise, the carriers' feedback and the modulators; the DX7's own three.
constexpr std::array<uint32_t, 17> kPinnedOscillatorIds{0, 1, 3, 4, 5, 7, 8, 9, 10, 19, 20, 21, 22, 26, 27, 28, 29};
constexpr std::array<uint32_t, 3> kPinnedDx7Ids{55, 56, 57};

bool owned(ParamId id) {
	return std::ranges::find(kPinnedOscillatorIds, id.value) != kPinnedOscillatorIds.end()
	       || std::ranges::find(kPinnedDx7Ids, id.value) != kPinnedDx7Ids.end();
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

	it("owns exactly the pinned params per engine", _ {
		for (const deluge::plugin::ParamInfo& row : kParams) {
			const bool oscillator = std::ranges::find(kPinnedOscillatorIds, row.id.value) != kPinnedOscillatorIds.end();
			const bool dx7 = std::ranges::find(kPinnedDx7Ids, row.id.value) != kPinnedDx7Ids.end();
			expect(std::string{row.file_name} + (param_is_live(SynthMode::FM, row.id) ? " fm" : " -")
			       + (param_is_live(SynthMode::DX7, row.id) ? " dx7" : " -"))
			    .to_equal(std::string{row.file_name} + (dx7 ? " -" : " fm") + (oscillator ? " -" : " dx7"));
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

	it("names a parked oscillator A volume, pulse width or pitch by the DX7's own in DX7 mode, and back", _ {
		expect(playing_param(SynthMode::DX7, param::kOscAVolume)->value).to_equal(param::kDx7Level.value);
		expect(playing_param(SynthMode::DX7, param::kOscAPhaseWidth)->value).to_equal(param::kDx7AmpMod.value);
		expect(playing_param(SynthMode::DX7, param::kOscAPitch)->value).to_equal(param::kDx7Pitch.value);
		expect(playing_param(SynthMode::DX7, param::kOscBVolume).has_value()).to_equal(false);
		expect(playing_param(SynthMode::SUBTRACTIVE, param::kDx7Level)->value).to_equal(param::kOscAVolume.value);
		expect(playing_param(SynthMode::SUBTRACTIVE, param::kOscAVolume)->value).to_equal(param::kOscAVolume.value);
		const ParamId on_osc = *cable_depth_id(PatchSource::LFO_GLOBAL_1, param::kOscAVolume);
		expect(playing_target(SynthMode::DX7, on_osc)->value)
		    .to_equal(cable_depth_id(PatchSource::LFO_GLOBAL_1, param::kDx7Level)->value);
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
