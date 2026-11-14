// clang-format off
#include "instruments/deluge/pages.h"
#include "effects/setting_params.h"
#include "instruments/deluge/plugin.h"
#include <cppspec.hpp>

#include <array>
#include <utility>

using namespace deluge;
namespace ip = deluge::instrument::param;

namespace {

using Knobs = std::array<instrument::KnobPair, 8>;
using instrument::SynthMode;

/// @brief The device pages' knobs, the knobs a new sound starts with.
Knobs device_knobs() {
	Knobs knobs{};
	for (size_t button = 0; button < knobs.size(); button++) {
		knobs[button] = {.lower = *instrument::kDevicePages[button].slots[0].param,
		                 .upper = *instrument::kDevicePages[button].slots[1].param};
	}
	return knobs;
}

/// @brief @p base with mod button @p button's knobs @p lower and @p upper.
Knobs with_pair(Knobs base, size_t button, plugin::ParamId lower, plugin::ParamId upper) {
	base[button] = {.lower = lower, .upper = upper};
	return base;
}

/// The device knobs with the filter button lent to the modulators' volumes, as FM has them.
const Knobs kFm = with_pair(device_knobs(), 1, ip::kModulator2Volume, ip::kModulator1Volume);

Knobs into_fm(const Knobs& knobs) {
	return instrument::remap_for_mode(SynthMode::SUBTRACTIVE, SynthMode::FM, knobs);
}

Knobs out_of_fm(const Knobs& knobs) {
	return instrument::remap_for_mode(SynthMode::FM, SynthMode::SUBTRACTIVE, knobs);
}

} // namespace

describe instrument_pages("the Deluge instrument's pages", ${
	it("puts volume over pan on the first mod button", _{
		const auto& page = instrument::kDevicePages[0];
		expect(*page.slots[1].param == ip::kVolumePostFX).to_be_true();
		expect(*page.slots[0].param == ip::kPan).to_be_true();
	});
	it("steps the delay's ping-pong from the delay rate's press, or its sync type with the alternative delay knobs", _{
		expect(*instrument::slot_for(ip::kDelayRate, {}).press_param == effects::setting::kDelayPingPong).to_be_true();
		expect(*instrument::slot_for(ip::kDelayRate, {.alt_delay = true}).press_param
		       == effects::setting::kDelaySyncType).to_be_true();
	});
	it("steps the delay's type from the delay feedback's press, or its sync level with the alternative delay knobs", _{
		expect(*instrument::slot_for(ip::kDelayFeedback, {}).press_param == effects::setting::kDelayType).to_be_true();
		expect(*instrument::slot_for(ip::kDelayFeedback, {.alt_delay = true}).press_param
		       == effects::setting::kDelaySyncLevel).to_be_true();
	});
	it("steps a filter's mode from its resonance", _{
		expect(instrument::slot_for(ip::kLpfResonance, {}).press == plugin::PressAction::STEP_SETTING).to_be_true();
		expect(*instrument::slot_for(ip::kLpfResonance, {}).press_param == effects::setting::kLpfMode).to_be_true();
		expect(*instrument::slot_for(ip::kHpfResonance, {}).press_param == effects::setting::kHpfMode).to_be_true();
	});
	it("moves to the next reverb preset from the reverb amount", _{
		expect(instrument::slot_for(ip::kReverbAmount, {}).press == plugin::PressAction::REVERB_PRESET).to_be_true();
	});
	it("cycles the filter page from each filter's frequency knob and the treble, but not in FM", _{
		for (const plugin::ParamId id : {ip::kLpfFrequency, ip::kHpfFrequency, ip::kTreble}) {
			expect(instrument::slot_for(id, {}).press == plugin::PressAction::NEXT_PAGE).to_be_true();
			expect(instrument::slot_for(id, {.fm = true}).press == plugin::PressAction::NONE).to_be_true();
		}
	});
	it("holds the stutter from the stutter rate", _{
		expect(instrument::slot_for(ip::kStutterRate, {}).press == plugin::PressAction::HOLD_STUTTER).to_be_true();
	});
	it("kinds a cable's depth, a patched param and an unpatched one apart", _{
		const auto depth = instrument::cable_depth_id(instrument::PatchSource::SIDECHAIN, ip::kVolumePostReverbSend);
		expect(instrument::slot_for(*depth, {}).kind == plugin::ParamKind::PATCH_CABLE).to_be_true();
		expect(instrument::slot_for(*depth, {}).press == plugin::PressAction::STEP_SETTING).to_be_true();
		expect(instrument::slot_for(ip::kPan, {}).kind == plugin::ParamKind::PATCHED).to_be_true();
		expect(instrument::slot_for(ip::kBitcrushAmount, {}).kind == plugin::ParamKind::UNPATCHED_SOUND).to_be_true();
	});
	it("pins every default knob: the lower and upper param of each mod button", _{
		const auto sidechain = *instrument::cable_depth_id(instrument::PatchSource::SIDECHAIN, ip::kVolumePostReverbSend);
		const auto lfo_pitch = *instrument::cable_depth_id(instrument::PatchSource::LFO_GLOBAL_1, ip::kPitch);
		const plugin::ParamId expected[8][2] = {
			{ip::kPan, ip::kVolumePostFX},
			{ip::kLpfResonance, ip::kLpfFrequency},
			{ip::kEnv1Release, ip::kEnv1Attack},
			{ip::kDelayFeedback, ip::kDelayRate},
			{ip::kReverbAmount, sidechain},
			{lfo_pitch, ip::kLfo1Rate},
			{ip::kPortamento, ip::kStutterRate},
			{ip::kBitcrushAmount, ip::kSampleRateReduction},
		};
		for (uint32_t button = 0; button < 8; button++) {
			expect(instrument::kDevicePages[button].section).to_equal(button);
			for (uint32_t knob = 0; knob < 2; knob++) {
				expect(*instrument::kDevicePages[button].slots[knob].param == expected[button][knob]).to_be_true();
			}
		}
	});
	it("makes a sample sound's pages the device pages with mode 6's lower knob on pitch adjust, and no other change", _{
		int differing = 0;
		for (size_t button = 0; button < 8; button++) {
			for (size_t knob = 0; knob < 2; knob++) {
				if (!(instrument::kSamplePages[button].slots[knob] == instrument::kDevicePages[button].slots[knob])) {
					differing++;
					expect(button).to_equal(size_t{6});
					expect(knob).to_equal(size_t{0});
				}
			}
			expect(instrument::kSamplePages[button].section).to_equal(static_cast<uint32_t>(button));
		}
		expect(differing).to_equal(1);
		expect(*instrument::kSamplePages[6].slots[0].param == ip::kPitch).to_be_true();
		expect(*instrument::kSamplePages[6].slots[1].param == ip::kStutterRate).to_be_true();
	});
	it("gives oscillator 1's wavetable claim its wave index on the upper knob and the LFO's depth on it below", _{
		const auto lfo_depth = *instrument::cable_depth_id(instrument::PatchSource::LFO_LOCAL_1, ip::kOscAWavetablePosition);
		const Knobs got = instrument::wavetable_claimed(device_knobs(), 0);
		expect(got[7] == instrument::KnobPair{.lower = lfo_depth, .upper = ip::kOscAWavetablePosition}).to_be_true();
		for (size_t button = 0; button < 7; button++) {
			expect(got[button] == device_knobs()[button]).to_be_true();
		}
	});
	it("keeps oscillator 2's wave index on oscillator 1's lower knob", _{
		const Knobs held = with_pair(device_knobs(), 7, ip::kOscBWavetablePosition, ip::kSampleRateReduction);
		const Knobs got = instrument::wavetable_claimed(held, 0);
		expect(got[7] == instrument::KnobPair{.lower = ip::kOscBWavetablePosition, .upper = ip::kOscAWavetablePosition})
		    .to_be_true();
	});
	it("gives oscillator 2's wavetable claim its wave index on the lower knob and leaves the upper knob", _{
		const Knobs got = instrument::wavetable_claimed(device_knobs(), 1);
		expect(got[7] == instrument::KnobPair{.lower = ip::kOscBWavetablePosition, .upper = ip::kSampleRateReduction})
		    .to_be_true();
	});
	it("puts the bitcrusher and the sample rate reduction back when a sample replaces a wavetable", _{
		const Knobs claimed = instrument::wavetable_claimed(device_knobs(), 0);
		expect(instrument::wavetable_released(claimed, 0) == device_knobs()).to_be_true();
		const Knobs two = instrument::wavetable_claimed(device_knobs(), 1);
		expect(instrument::wavetable_released(two, 1) == device_knobs()).to_be_true();
	});
	it("releases a knob on a cable's depth to its oscillator's wave index, and no other oscillator's", _{
		const auto depth = *instrument::cable_depth_id(instrument::PatchSource::LFO_LOCAL_1, ip::kOscBWavetablePosition);
		const Knobs held = with_pair(device_knobs(), 7, depth, ip::kOscAWavetablePosition);
		const Knobs got = instrument::wavetable_released(held, 1);
		expect(got[7] == instrument::KnobPair{.lower = ip::kBitcrushAmount, .upper = ip::kOscAWavetablePosition})
		    .to_be_true();
		expect(instrument::wavetable_released(held, 0)[7] == instrument::KnobPair{.lower = depth,
		                                                                           .upper = ip::kSampleRateReduction})
		    .to_be_true();
	});
	it("releases an upper knob on a cable's depth to its oscillator's wave index, and not the lower knob beside it", _{
		const auto depth = *instrument::cable_depth_id(instrument::PatchSource::LFO_LOCAL_1, ip::kOscAWavetablePosition);
		const Knobs held = with_pair(device_knobs(), 7, ip::kOscBWavetablePosition, depth);
		expect(instrument::wavetable_released(held, 0)[7]
		       == instrument::KnobPair{.lower = ip::kOscBWavetablePosition, .upper = ip::kSampleRateReduction})
		    .to_be_true();
		expect(instrument::wavetable_released(held, 1)[7]
		       == instrument::KnobPair{.lower = ip::kBitcrushAmount, .upper = depth})
		    .to_be_true();
	});
	it("gives oscillator 2's claim its wave index over a lower knob already on oscillator 1's LFO depth", _{
		const auto depth = *instrument::cable_depth_id(instrument::PatchSource::LFO_LOCAL_1, ip::kOscAWavetablePosition);
		const Knobs held = with_pair(device_knobs(), 7, depth, ip::kOscAWavetablePosition);
		expect(instrument::wavetable_claimed(held, 1)[7]
		       == instrument::KnobPair{.lower = ip::kOscBWavetablePosition, .upper = ip::kOscAWavetablePosition})
		    .to_be_true();
	});
	it("cycles low-pass, high-pass, EQ on the first mod button", _{
		expect(*instrument::kFilterCycle[1].slots[1].param == ip::kHpfFrequency).to_be_true();
		expect(*instrument::kFilterCycle[2].slots[0].param == ip::kBass).to_be_true();
		expect(instrument::kFilterCycle[2].page_id).to_equal(2u);
	});
	it("publishes its pages as a record keyed by its plugin id", _{
		const auto& record = instrument::kRemoteControls;
		expect(record.plugin == plugin::kDelugeInstrument).to_be_true();
		const std::array<plugin::RemoteControls, 1> records{record};
		expect(plugin::remote_controls_for(records, plugin::kDelugeInstrument) == &records[0]).to_be_true();
		expect(plugin::remote_controls_for(records, plugin::kGlobalEffectable) == nullptr).to_be_true();
	});
	it("gets the device pages in order, and none past the end", _{
		const auto& record = instrument::kRemoteControls;
		const plugin::PageContext ctx;
		expect(record.count(ctx)).to_equal(uint32_t{8});
		plugin::RemotePage out;
		for (uint32_t i = 0; i < 8; i++) {
			expect(record.get(ctx, i, out)).to_be_true();
			expect(out == instrument::kDevicePages[i]).to_be_true();
		}
		expect(record.get(ctx, 8, out)).to_be_false();
	});
	it("finds the sound's whole filter cycle on any section: ids 0-2 in group 0, 3-5 in group 1 with the slots swapped", _{
		const auto& record = instrument::kRemoteControls;
		const plugin::PageContext ctx;
		plugin::RemotePage out;
		for (uint32_t section = 0; section < 8; section++) {
			for (uint32_t id = 0; id < 6; id++) {
				plugin::RemotePage want = instrument::kFilterCycle[id % 3];
				want.section = section;
				if (id >= 3) {
					want.group = 1;
					want.page_id = id;
					std::swap(want.slots[0], want.slots[1]);
				}
				expect(record.find(ctx, section, id, out)).to_be_true();
				expect(out == want).to_be_true();
				expect(out.section).to_equal(section);
				expect(out.group).to_equal(static_cast<uint8_t>(id >= 3 ? 1 : 0));
				expect(out.page_id).to_equal(id);
			}
			expect(record.find(ctx, section, 6, out)).to_be_false();
		}
		expect(record.find(ctx, 1, 3, out)).to_be_true();
		expect(*out.slots[1].param == ip::kLpfResonance).to_be_true();
		expect(*out.slots[0].param == ip::kLpfFrequency).to_be_true();
	});
	it("decorates a param as slot_for does, reading FM from the owner's state and alt delay from the context", _{
		const auto& record = instrument::kRemoteControls;
		instrument::SoundState fm_state;
		fm_state.synth_mode = instrument::SynthMode::FM;
		const auto fm = plugin::StateHandle::of<instrument::DelugeInstrument>(fm_state);
		const auto subtractive = plugin::StateHandle::of<instrument::DelugeInstrument>({});
		const auto lfo_pitch = *instrument::cable_depth_id(instrument::PatchSource::LFO_GLOBAL_1, ip::kPitch);
		const auto sidechain = *instrument::cable_depth_id(instrument::PatchSource::SIDECHAIN, ip::kVolumePostReverbSend);
		const plugin::ParamId params[] = {ip::kPan,           ip::kLpfFrequency,  ip::kHpfFrequency,
		                                  ip::kTreble,        ip::kLpfResonance,  ip::kHpfResonance,
		                                  ip::kDelayRate,     ip::kDelayFeedback, ip::kReverbAmount,
		                                  ip::kStutterRate,   ip::kBitcrushAmount, lfo_pitch, sidechain};
		for (const plugin::StateHandle* state : {static_cast<const plugin::StateHandle*>(nullptr), &fm, &subtractive}) {
			for (const bool alt : {false, true}) {
				const plugin::PageContext ctx{.state = state, .alt_delay = alt};
				const bool is_fm = state == &fm;
				for (const plugin::ParamId id : params) {
					expect(record.decorate(ctx, id) == instrument::slot_for(id, {.fm = is_fm, .alt_delay = alt}))
					    .to_be_true();
				}
			}
		}
	});
	context("remaps the knobs on a synth mode change, as the engine's FM swap does", _{
		it("lends a new synth's filter button to the modulators' volumes, and gives it back", _{
			expect(into_fm(device_knobs()) == kFm).to_be_true();
			expect(out_of_fm(kFm) == device_knobs()).to_be_true();
		});
		it("brings a high-pass, an EQ and a mixed pair on the filter button back as the low-pass pair", _{
			for (const auto& [lower, upper] : {std::pair{ip::kHpfResonance, ip::kHpfFrequency},
			                                   std::pair{ip::kBass, ip::kTreble},
			                                   std::pair{ip::kHpfResonance, ip::kLpfFrequency},
			                                   std::pair{ip::kLpfResonance, ip::kTreble},
			                                   std::pair{ip::kBass, ip::kHpfFrequency}}) {
				const Knobs fm = into_fm(with_pair(device_knobs(), 1, lower, upper));
				expect(fm == kFm).to_be_true();
				expect(out_of_fm(fm) == device_knobs()).to_be_true();
			}
		});
		it("leaves a pair with the cutoff on the lower knob, as the filter cycle's group 1 has it", _{
			const Knobs cutoff_lower = with_pair(device_knobs(), 1, ip::kLpfFrequency, ip::kLpfResonance);
			expect(into_fm(cutoff_lower) == cutoff_lower).to_be_true();
		});
		it("swaps a low-pass pair on any button, mod button 7 included", _{
			const Knobs on_7 = with_pair(device_knobs(), 7, ip::kLpfResonance, ip::kLpfFrequency);
			expect(into_fm(on_7) == with_pair(kFm, 7, ip::kModulator2Volume, ip::kModulator1Volume)).to_be_true();
		});
		it("passes over a button with a cable's depth on it, though the cable's param would match", _{
			const auto depth = *instrument::cable_depth_id(instrument::PatchSource::LFO_LOCAL_1, ip::kLpfResonance);
			const Knobs cable = with_pair(device_knobs(), 1, depth, ip::kLpfFrequency);
			expect(into_fm(cable) == cable).to_be_true();
		});
		it("turns a modulator-volume pair learned in FM into the low-pass pair on the way out", _{
			const Knobs learned = with_pair(kFm, 3, ip::kModulator2Volume, ip::kModulator1Volume);
			expect(out_of_fm(learned) == with_pair(device_knobs(), 3, ip::kLpfResonance, ip::kLpfFrequency))
			    .to_be_true();
		});
		it("changes nothing staying in FM, staying out of it, or between subtractive and ring modulation", _{
			const Knobs learned = with_pair(kFm, 3, ip::kModulator2Volume, ip::kModulator1Volume);
			for (const Knobs& knobs : {device_knobs(), learned}) {
				expect(instrument::remap_for_mode(SynthMode::FM, SynthMode::FM, knobs) == knobs).to_be_true();
				expect(instrument::remap_for_mode(SynthMode::SUBTRACTIVE, SynthMode::RINGMOD, knobs) == knobs)
				    .to_be_true();
				expect(instrument::remap_for_mode(SynthMode::RINGMOD, SynthMode::SUBTRACTIVE, knobs) == knobs)
				    .to_be_true();
			}
		});
		it("goes into FM from ring modulation and out to it as from and to subtractive", _{
			expect(instrument::remap_for_mode(SynthMode::RINGMOD, SynthMode::FM, device_knobs()) == kFm).to_be_true();
			expect(instrument::remap_for_mode(SynthMode::FM, SynthMode::RINGMOD, kFm) == device_knobs()).to_be_true();
		});
	});
});
// clang-format on

CPPSPEC_SPEC(instrument_pages)
