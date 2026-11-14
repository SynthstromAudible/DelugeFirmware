// clang-format off
#include "instruments/deluge/pages.h"
#include "effects/setting_params.h"
#include "instruments/deluge/plugin.h"
#include <cppspec.hpp>

using namespace deluge;
namespace ip = deluge::instrument::param;

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
});
// clang-format on

CPPSPEC_SPEC(instrument_pages)
