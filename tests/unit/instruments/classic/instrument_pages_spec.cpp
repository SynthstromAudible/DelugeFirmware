// clang-format off
#include "instruments/classic/pages.h"
#include "effects/setting_params.h"
#include "instruments/classic/deluge_synth.h"
#include "support/printers/knob_pair_printer.h"
#include "support/printers/param_id_printer.h"
#include "support/printers/plugin_id_printer.h"
#include "support/printers/remote_controls_printer.h"
#include "instruments/classic/state_rules.h"
#include <cppspec.hpp>

#include <array>
#include <utility>

using namespace deluge;
namespace ip = deluge::instruments::classic::param;

namespace {

using Knobs = std::array<instruments::classic::KnobPair, 8>;
using instruments::classic::SynthMode;

/// @brief The device pages' knobs, the knobs a new sound starts with.
Knobs device_knobs() {
	Knobs knobs{};
	for (size_t button = 0; button < knobs.size(); button++) {
		knobs[button] = {.lower = *instruments::classic::kDevicePages[button].slots[0].param,
		                 .upper = *instruments::classic::kDevicePages[button].slots[1].param};
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
	return instruments::classic::remap_for_mode(SynthMode::SUBTRACTIVE, SynthMode::FM, knobs);
}

Knobs out_of_fm(const Knobs& knobs) {
	return instruments::classic::remap_for_mode(SynthMode::FM, SynthMode::SUBTRACTIVE, knobs);
}

} // namespace

describe instrument_pages("the Deluge instrument's pages", ${
	it("puts volume over pan on the first mod button", _{
		const auto& page = instruments::classic::kDevicePages[0];
		expect(*page.slots[1].param).to_equal(ip::kVolumePostFX);
		expect(*page.slots[0].param).to_equal(ip::kPan);
	});
	it("steps the delay's ping-pong from the delay rate's press, or its sync type with the alternative delay knobs", _{
		expect(*instruments::classic::slot_for(ip::kDelayRate, {}).press_param).to_equal(effects::setting::kDelayPingPong);
		expect(*instruments::classic::slot_for(ip::kDelayRate, {.alt_delay = true}).press_param)
		    .to_equal(effects::setting::kDelaySyncType);
	});
	it("steps the delay's type from the delay feedback's press, or its sync level with the alternative delay knobs", _{
		expect(*instruments::classic::slot_for(ip::kDelayFeedback, {}).press_param).to_equal(effects::setting::kDelayType);
		expect(*instruments::classic::slot_for(ip::kDelayFeedback, {.alt_delay = true}).press_param)
		    .to_equal(effects::setting::kDelaySyncLevel);
	});
	it("steps a filter's mode from its resonance", _{
		expect(instruments::classic::slot_for(ip::kLpfResonance, {}).press).to_equal(plugin::PressAction::STEP_SETTING);
		expect(*instruments::classic::slot_for(ip::kLpfResonance, {}).press_param).to_equal(effects::setting::kLpfMode);
		expect(*instruments::classic::slot_for(ip::kHpfResonance, {}).press_param).to_equal(effects::setting::kHpfMode);
	});
	it("moves to the next reverb preset from the reverb amount", _{
		expect(instruments::classic::slot_for(ip::kReverbAmount, {}).press).to_equal(plugin::PressAction::REVERB_PRESET);
	});
	it("cycles the filter page from each filter's frequency knob and the treble, but not in FM", _{
		for (const plugin::ParamId id : {ip::kLpfFrequency, ip::kHpfFrequency, ip::kTreble}) {
			expect(instruments::classic::slot_for(id, {}).press).to_equal(plugin::PressAction::NEXT_PAGE);
			expect(instruments::classic::slot_for(id, {.fm = true}).press).to_equal(plugin::PressAction::NONE);
		}
	});
	it("holds the stutter from the stutter rate", _{
		expect(instruments::classic::slot_for(ip::kStutterRate, {}).press).to_equal(plugin::PressAction::HOLD_STUTTER);
	});
	it("names a cable's depth, a patched param and an unpatched one by their ids", _{
		const auto depth = instruments::classic::cable_depth_id(instruments::classic::PatchSource::SIDECHAIN, ip::kVolumePostReverbSend);
		expect(*instruments::classic::slot_for(*depth, {}).param).to_equal(*depth);
		expect(instruments::classic::slot_for(*depth, {}).press).to_equal(plugin::PressAction::STEP_SETTING);
		expect(*instruments::classic::slot_for(ip::kPan, {}).param).to_equal(ip::kPan);
		expect(*instruments::classic::slot_for(ip::kBitcrushAmount, {}).param).to_equal(ip::kBitcrushAmount);
	});
	it("pins every default knob: the lower and upper param of each mod button", _{
		const auto sidechain = *instruments::classic::cable_depth_id(instruments::classic::PatchSource::SIDECHAIN, ip::kVolumePostReverbSend);
		const auto lfo_pitch = *instruments::classic::cable_depth_id(instruments::classic::PatchSource::LFO_GLOBAL_1, ip::kPitch);
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
			expect(instruments::classic::kDevicePages[button].section).to_equal(button);
			for (uint32_t knob = 0; knob < 2; knob++) {
				expect(*instruments::classic::kDevicePages[button].slots[knob].param).to_equal(expected[button][knob]);
			}
		}
	});
	it("makes a sample sound's pages the device pages with mode 6's lower knob on pitch adjust, and no other change", _{
		int differing = 0;
		for (size_t button = 0; button < 8; button++) {
			for (size_t knob = 0; knob < 2; knob++) {
				if (!(instruments::classic::kSamplePages[button].slots[knob] == instruments::classic::kDevicePages[button].slots[knob])) {
					differing++;
					expect(button).to_equal(size_t{6});
					expect(knob).to_equal(size_t{0});
				}
			}
			expect(instruments::classic::kSamplePages[button].section).to_equal(static_cast<uint32_t>(button));
		}
		expect(differing).to_equal(1);
		expect(*instruments::classic::kSamplePages[6].slots[0].param).to_equal(ip::kPitch);
		expect(*instruments::classic::kSamplePages[6].slots[1].param).to_equal(ip::kStutterRate);
	});
	it("gives oscillator 1's wavetable claim its wave index on the upper knob and the LFO's depth on it below", _{
		const auto lfo_depth = *instruments::classic::cable_depth_id(instruments::classic::PatchSource::LFO_LOCAL_1, ip::kOscAWavetablePosition);
		const Knobs got = instruments::classic::wavetable_claimed(device_knobs(), 0);
		expect(got[7]).to_equal(instruments::classic::KnobPair{.lower = lfo_depth, .upper = ip::kOscAWavetablePosition});
		for (size_t button = 0; button < 7; button++) {
			expect(got[button]).to_equal(device_knobs()[button]);
		}
	});
	it("keeps oscillator 2's wave index on oscillator 1's lower knob", _{
		const Knobs held = with_pair(device_knobs(), 7, ip::kOscBWavetablePosition, ip::kSampleRateReduction);
		const Knobs got = instruments::classic::wavetable_claimed(held, 0);
		expect(got[7])
		    .to_equal(instruments::classic::KnobPair{.lower = ip::kOscBWavetablePosition, .upper = ip::kOscAWavetablePosition});
	});
	it("gives oscillator 2's wavetable claim its wave index on the lower knob and leaves the upper knob", _{
		const Knobs got = instruments::classic::wavetable_claimed(device_knobs(), 1);
		expect(got[7])
		    .to_equal(instruments::classic::KnobPair{.lower = ip::kOscBWavetablePosition, .upper = ip::kSampleRateReduction});
	});
	it("puts the bitcrusher and the sample rate reduction back when a sample replaces a wavetable", _{
		const Knobs claimed = instruments::classic::wavetable_claimed(device_knobs(), 0);
		expect(instruments::classic::wavetable_released(claimed, 0)).to_equal(device_knobs());
		const Knobs two = instruments::classic::wavetable_claimed(device_knobs(), 1);
		expect(instruments::classic::wavetable_released(two, 1)).to_equal(device_knobs());
	});
	it("releases a knob on a cable's depth to its oscillator's wave index, and no other oscillator's", _{
		const auto depth = *instruments::classic::cable_depth_id(instruments::classic::PatchSource::LFO_LOCAL_1, ip::kOscBWavetablePosition);
		const Knobs held = with_pair(device_knobs(), 7, depth, ip::kOscAWavetablePosition);
		const Knobs got = instruments::classic::wavetable_released(held, 1);
		expect(got[7])
		    .to_equal(instruments::classic::KnobPair{.lower = ip::kBitcrushAmount, .upper = ip::kOscAWavetablePosition});
		expect(instruments::classic::wavetable_released(held, 0)[7])
		    .to_equal(instruments::classic::KnobPair{.lower = depth, .upper = ip::kSampleRateReduction});
	});
	it("releases an upper knob on a cable's depth to its oscillator's wave index, and not the lower knob beside it", _{
		const auto depth = *instruments::classic::cable_depth_id(instruments::classic::PatchSource::LFO_LOCAL_1, ip::kOscAWavetablePosition);
		const Knobs held = with_pair(device_knobs(), 7, ip::kOscBWavetablePosition, depth);
		expect(instruments::classic::wavetable_released(held, 0)[7])
		    .to_equal(instruments::classic::KnobPair{.lower = ip::kOscBWavetablePosition, .upper = ip::kSampleRateReduction});
		expect(instruments::classic::wavetable_released(held, 1)[7])
		    .to_equal(instruments::classic::KnobPair{.lower = ip::kBitcrushAmount, .upper = depth});
	});
	it("gives oscillator 2's claim its wave index over a lower knob already on oscillator 1's LFO depth", _{
		const auto depth = *instruments::classic::cable_depth_id(instruments::classic::PatchSource::LFO_LOCAL_1, ip::kOscAWavetablePosition);
		const Knobs held = with_pair(device_knobs(), 7, depth, ip::kOscAWavetablePosition);
		expect(instruments::classic::wavetable_claimed(held, 1)[7])
		    .to_equal(instruments::classic::KnobPair{.lower = ip::kOscBWavetablePosition, .upper = ip::kOscAWavetablePosition});
	});
	it("cycles low-pass, high-pass, EQ on the first mod button", _{
		expect(*instruments::classic::kFilterCycle[1].slots[1].param).to_equal(ip::kHpfFrequency);
		expect(*instruments::classic::kFilterCycle[2].slots[0].param).to_equal(ip::kBass);
		expect(instruments::classic::kFilterCycle[2].page_id).to_equal(2u);
	});
	it("publishes its pages as a record keyed by its plugin id", _{
		const auto& record = instruments::classic::kRemoteControls;
		expect(record.plugin).to_equal(plugin::kDelugeInstrument);
		const std::array<plugin::RemoteControls, 1> records{record};
		expect(plugin::remote_controls_for(records, plugin::kDelugeInstrument)).to_equal(&records[0]);
		expect(plugin::remote_controls_for(records, plugin::kGlobalEffectable)).to_be_null();
	});
	it("gets the device pages in order, and none past the end", _{
		const auto& record = instruments::classic::kRemoteControls;
		const plugin::PageContext ctx;
		expect(record.count(ctx)).to_equal(uint32_t{8});
		plugin::RemotePage out;
		for (uint32_t i = 0; i < 8; i++) {
			expect(record.get(ctx, i, out)).to_be_true();
			expect(out).to_equal(instruments::classic::kDevicePages[i]);
		}
		expect(record.get(ctx, 8, out)).to_be_false();
	});
	it("finds the sound's whole filter cycle on any section: ids 0-2 in group 0, 3-5 in group 1 with the slots swapped", _{
		const auto& record = instruments::classic::kRemoteControls;
		const plugin::PageContext ctx;
		plugin::RemotePage out;
		for (uint32_t section = 0; section < 8; section++) {
			for (uint32_t id = 0; id < 6; id++) {
				plugin::RemotePage want = instruments::classic::kFilterCycle[id % 3];
				want.section = section;
				if (id >= 3) {
					want.group = 1;
					want.page_id = id;
					std::swap(want.slots[0], want.slots[1]);
				}
				expect(record.find(ctx, section, id, out)).to_be_true();
				expect(out).to_equal(want);
				expect(out.section).to_equal(section);
				expect(out.group).to_equal(static_cast<uint8_t>(id >= 3 ? 1 : 0));
				expect(out.page_id).to_equal(id);
			}
			expect(record.find(ctx, section, 6, out)).to_be_false();
		}
		expect(record.find(ctx, 1, 3, out)).to_be_true();
		expect(*out.slots[1].param).to_equal(ip::kLpfResonance);
		expect(*out.slots[0].param).to_equal(ip::kLpfFrequency);
	});
	it("decorates a param as slot_for does, reading FM from the owner's state and alt delay from the context", _{
		const auto& record = instruments::classic::kRemoteControls;
		const auto fm = plugin::StateHandle::of<instruments::classic::DelugeSynth>(
		    instruments::classic::switch_engine(instruments::classic::SoundState{}, instruments::classic::SynthMode::FM));
		const auto subtractive = plugin::StateHandle::of<instruments::classic::DelugeSynth>({});
		const auto lfo_pitch = *instruments::classic::cable_depth_id(instruments::classic::PatchSource::LFO_GLOBAL_1, ip::kPitch);
		const auto sidechain = *instruments::classic::cable_depth_id(instruments::classic::PatchSource::SIDECHAIN, ip::kVolumePostReverbSend);
		const plugin::ParamId params[] = {ip::kPan,           ip::kLpfFrequency,  ip::kHpfFrequency,
		                                  ip::kTreble,        ip::kLpfResonance,  ip::kHpfResonance,
		                                  ip::kDelayRate,     ip::kDelayFeedback, ip::kReverbAmount,
		                                  ip::kStutterRate,   ip::kBitcrushAmount, lfo_pitch, sidechain};
		for (const plugin::StateHandle* state : {static_cast<const plugin::StateHandle*>(nullptr), &fm, &subtractive}) {
			for (const bool alt : {false, true}) {
				const plugin::PageContext ctx{.state = state, .alt_delay = alt};
				const bool is_fm = state == &fm;
				for (const plugin::ParamId id : params) {
					expect(record.decorate(ctx, id))
					    .to_equal(instruments::classic::slot_for(id, {.fm = is_fm, .alt_delay = alt}));
				}
			}
		}
	});
	context("remaps the knobs on a synth mode change, as the engine's FM swap does", _{
		it("lends a new synth's filter button to the modulators' volumes, and gives it back", _{
			expect(into_fm(device_knobs())).to_equal(kFm);
			expect(out_of_fm(kFm)).to_equal(device_knobs());
		});
		it("brings a high-pass, an EQ and a mixed pair on the filter button back as the low-pass pair", _{
			for (const auto& [lower, upper] : {std::pair{ip::kHpfResonance, ip::kHpfFrequency},
			                                   std::pair{ip::kBass, ip::kTreble},
			                                   std::pair{ip::kHpfResonance, ip::kLpfFrequency},
			                                   std::pair{ip::kLpfResonance, ip::kTreble},
			                                   std::pair{ip::kBass, ip::kHpfFrequency}}) {
				const Knobs fm = into_fm(with_pair(device_knobs(), 1, lower, upper));
				expect(fm).to_equal(kFm);
				expect(out_of_fm(fm)).to_equal(device_knobs());
			}
		});
		it("leaves a pair with the cutoff on the lower knob, as the filter cycle's group 1 has it", _{
			const Knobs cutoff_lower = with_pair(device_knobs(), 1, ip::kLpfFrequency, ip::kLpfResonance);
			expect(into_fm(cutoff_lower)).to_equal(cutoff_lower);
		});
		it("swaps a low-pass pair on any button, mod button 7 included", _{
			const Knobs on_7 = with_pair(device_knobs(), 7, ip::kLpfResonance, ip::kLpfFrequency);
			expect(into_fm(on_7)).to_equal(with_pair(kFm, 7, ip::kModulator2Volume, ip::kModulator1Volume));
		});
		it("passes over a button with a cable's depth on it, though the cable's param would match", _{
			const auto depth = *instruments::classic::cable_depth_id(instruments::classic::PatchSource::LFO_LOCAL_1, ip::kLpfResonance);
			const Knobs cable = with_pair(device_knobs(), 1, depth, ip::kLpfFrequency);
			expect(into_fm(cable)).to_equal(cable);
		});
		it("turns a modulator-volume pair learned in FM into the low-pass pair on the way out", _{
			const Knobs learned = with_pair(kFm, 3, ip::kModulator2Volume, ip::kModulator1Volume);
			expect(out_of_fm(learned)).to_equal(with_pair(device_knobs(), 3, ip::kLpfResonance, ip::kLpfFrequency));
		});
		it("changes nothing staying in FM, staying out of it, or between subtractive and ring modulation", _{
			const Knobs learned = with_pair(kFm, 3, ip::kModulator2Volume, ip::kModulator1Volume);
			for (const Knobs& knobs : {device_knobs(), learned}) {
				expect(instruments::classic::remap_for_mode(SynthMode::FM, SynthMode::FM, knobs)).to_equal(knobs);
				expect(instruments::classic::remap_for_mode(SynthMode::SUBTRACTIVE, SynthMode::RINGMOD, knobs)).to_equal(knobs);
				expect(instruments::classic::remap_for_mode(SynthMode::RINGMOD, SynthMode::SUBTRACTIVE, knobs)).to_equal(knobs);
			}
		});
		it("moves oscillator A's volume, pulse width and pitch knobs onto the DX7's own into DX7 mode, and back", _{
			const auto depth = *instruments::classic::cable_depth_id(instruments::classic::PatchSource::LFO_GLOBAL_1, ip::kOscAVolume);
			const Knobs osc = with_pair(with_pair(device_knobs(), 2, ip::kOscAVolume, ip::kOscAPhaseWidth), 3, ip::kOscAPitch, depth);
			const Knobs dx7 = with_pair(with_pair(device_knobs(), 2, ip::kDx7Level, ip::kDx7AmpMod), 3, ip::kDx7Pitch,
			                            *instruments::classic::cable_depth_id(instruments::classic::PatchSource::LFO_GLOBAL_1, ip::kDx7Level));
			expect(instruments::classic::remap_for_mode(SynthMode::SUBTRACTIVE, SynthMode::DX7, osc)).to_equal(dx7);
			expect(instruments::classic::remap_for_mode(SynthMode::DX7, SynthMode::SUBTRACTIVE, dx7)).to_equal(osc);
			// A knob on a parked param with no DX7 counterpart stays (it turns nothing while parked)
			const Knobs osc_b = with_pair(device_knobs(), 2, ip::kOscBVolume, ip::kOscAVolume);
			expect(instruments::classic::remap_for_mode(SynthMode::SUBTRACTIVE, SynthMode::DX7, osc_b))
			    .to_equal(with_pair(device_knobs(), 2, ip::kOscBVolume, ip::kDx7Level));
			expect(instruments::classic::remap_for_mode(SynthMode::SUBTRACTIVE, SynthMode::DX7, device_knobs())).to_equal(device_knobs());
		});
		it("goes into FM from ring modulation and out to it as from and to subtractive", _{
			expect(instruments::classic::remap_for_mode(SynthMode::RINGMOD, SynthMode::FM, device_knobs())).to_equal(kFm);
			expect(instruments::classic::remap_for_mode(SynthMode::FM, SynthMode::RINGMOD, kFm)).to_equal(device_knobs());
		});
	});
});
// clang-format on

CPPSPEC_SPEC(instrument_pages)
