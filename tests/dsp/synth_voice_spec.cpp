/*
 * Copyright © 2026 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 * Licensed under the GNU General Public License v3 or later; see LICENSE.
 */

// synth::Voice played on the host, with nothing of the app: notes through the kernel's lifecycle, rendered from a
// VoiceConfig, with fake ports behind its sources.

#include "fake_voice_ports.h"
#include "synth/voice/voice.h"

#include "dsp-next/fixed/noise/cong.h"
#include "modulation/params/param.h"

#include <algorithm>
#include <array>
#include <cppspec.hpp>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <memory>

using namespace deluge::synth;
using deluge::synth::test::FakeDxPort;
using deluge::synth::test::FakeInputPort;
using deluge::synth::test::FakeSamplePort;
namespace params = deluge::modulation::params;

void functionsInit(); // util/param_functions.cpp: fills the param scaling tables

// The allocator seam the live input's pitch shifter reaches (memory/memory_allocator_interface.h), from the host heap.
void* allocMaxSpeed(uint32_t requiredSize) {
	return std::malloc(requiredSize);
}
extern "C" void delugeDealloc(void* address) {
	std::free(address);
}

namespace {

constexpr int32_t kBlock = SSI_TX_BUFFER_NUM_SAMPLES;
constexpr int32_t kMin = std::numeric_limits<int32_t>::min();
constexpr int32_t kMax = std::numeric_limits<int32_t>::max();
/// A short release, a few thousand samples: the voice has a release stage.
constexpr int32_t kShortRelease = static_cast<int32_t>(0x84000000);

/// One voice with everything it plays through: its ports, noise stream, clock and the scratch a render takes.
struct Rig {
	std::array<std::array<FakeSamplePort, kNumSources>, kMaxNumVoicesUnison> samples{};
	FakeDxPort dx;
	FakeInputPort input;
	deluge::dsp::fixed::Cong noise{1};
	uint32_t clock_next{1};
	Voice voice;
	std::array<std::array<int32_t, kBlock * 2>, 4> spare{};
	std::array<deluge::dsp::q31, kBlock * 2> fade{};
	std::array<deluge::dsp::q31, kBlock * 2> parallel{};
	std::array<int32_t, kBlock + 4> sync{};
	std::array<int32_t, kBlock * 2> out{};

	Rig() {
		functionsInit(); // the param scaling tables the patcher reads
		for (int32_t u = 0; u < kMaxNumVoicesUnison; u++) {
			for (int32_t s = 0; s < kNumSources; s++) {
				voice.unison_parts[u].slots[s].bind({.sample = &samples[u][s], .dx = &dx, .input = &input});
			}
		}
	}

	VoiceClock clock() { return VoiceClock{clock_next}; }
};

/// A sound's presets: one oscillator at full level through no filter, the amplitude envelope instant with full sustain
/// and @p release.
ControlBlock presets(int32_t release) {
	ControlBlock block{};
	block.preset_values.fill(0);
	block.preset_values[params::LOCAL_OSC_A_VOLUME] = kMax;
	block.preset_values[params::LOCAL_OSC_B_VOLUME] = kMin;
	block.preset_values[params::LOCAL_NOISE_VOLUME] = kMin;
	block.preset_values[params::LOCAL_VOLUME] = 0x4CCCCCA8;
	block.preset_values[params::LOCAL_ENV_0_ATTACK] = kMin;
	block.preset_values[params::LOCAL_ENV_0_SUSTAIN] = kMax;
	block.preset_values[params::LOCAL_ENV_0_RELEASE] = release;
	block.preset_values[params::LOCAL_LPF_FREQ] = kMax;
	block.preset_values[params::LOCAL_HPF_FREQ] = kMin;
	block.preset_values[params::LOCAL_MODULATOR_0_VOLUME] = 0x40000000;
	block.preset_values[params::LOCAL_MODULATOR_1_VOLUME] = kMin;
	return block;
}

NoteSetup note_setup(OscType osc, int32_t release = kShortRelease) {
	NoteSetup setup{};
	setup.sources[0] = {.osc_type = osc, .ever_active = true};
	setup.sources[1] = {.osc_type = OscType::SQUARE, .ever_active = false};
	setup.attack_is_zero[0] = true;
	setup.modulator_audible = {true, false};
	setup.initial_patching = presets(release);
	return setup;
}

SoundSnapshot snapshot(SynthMode mode) {
	SoundSnapshot snap{};
	snap.synth_mode = mode;
	snap.osc_retrigger_phase = {0, 0};
	snap.modulator_retrigger_phase = {0, 0};
	return snap;
}

VoiceConfig config(SynthMode mode, OscType osc) {
	VoiceConfig cfg{};
	cfg.volume_neutral_value_for_unison = 134217728;
	cfg.voice_priority = VoicePriority::MEDIUM;
	cfg.synth_mode = mode;
	cfg.num_unison = 1;
	cfg.osc_retrigger_phase = {0, 0};
	cfg.osc_type = {osc, OscType::SQUARE};
	cfg.repeat_mode = {SampleRepeatMode::ONCE, SampleRepeatMode::ONCE};
	cfg.note_tails_allowed = true;
	cfg.source_ever_active = {true, false};
	cfg.source_active_currently = {true, false};
	cfg.portamento = kMin;
	cfg.bend_ranges = {2, 48};
	return cfg;
}

constexpr std::array<int16_t, kNumExpressionDimensions> kNoMpe{};

NoteOnEvent note(int32_t code) {
	return {.note_before_arp = code,
	        .note_after_arp = code,
	        .velocity = 100,
	        .reset_envelopes = true,
	        .midi_channel = 0,
	        .mpe = kNoMpe};
}

/// The note-off for a sound whose amplitude envelope sustains (its presets' sustain is full).
ReleaseInfo release(const VoiceConfig& cfg) {
	return {.envelope_has_sustain = {true, false, false, false},
	        .note_tails_allowed = true,
	        .osc_types = cfg.osc_type,
	        .synth_mode = cfg.synth_mode,
	        .num_unison = 1};
}

/// Renders one block. @return whether the voice still sounds; @p peak takes the block's loudest sample.
bool render_block(Rig& rig, const NoteSetup& setup, const VoiceConfig& cfg, int64_t* peak = nullptr,
                  bool sample_can_auto_release = false) {
	rig.out.fill(0);
	VoiceBlock block{.num_active_voices = 1, .clock = rig.clock()};
	block.sample_can_auto_release[0] = sample_can_auto_release;
	const bool alive = rig.voice.render(
	    setup.initial_patching, cfg, block,
	    {
	        .buffer = rig.out.data(),
	        .num_samples = kBlock,
	        .pitch_adjust = kMaxSampleValue,
	        .spare_buffers = {rig.spare[0].data(), rig.spare[1].data(), rig.spare[2].data(), rig.spare[3].data()},
	        .filter_scratch = {.fade = rig.fade, .parallel = rig.parallel},
	        .sync_scratch = rig.sync,
	        .noise = &rig.noise,
	    });
	if (peak != nullptr) {
		*peak = 0;
		for (int32_t i = 0; i < kBlock; i++) {
			*peak = std::max<int64_t>(*peak, std::abs(static_cast<int64_t>(rig.out[i])));
		}
	}
	return alive;
}

int32_t blocks_until_silent(Rig& rig, const NoteSetup& setup, const VoiceConfig& cfg, int32_t limit) {
	int32_t blocks = 0;
	while (blocks < limit && render_block(rig, setup, cfg)) {
		blocks++;
	}
	return blocks;
}

} // namespace

// clang-format off
describe synth_voice_spec("synth::Voice", ${
	it("plays a virtual-analog note and releases it to silence", _{
		auto rig = std::make_unique<Rig>();
		const NoteSetup setup = note_setup(OscType::SAW);
		const VoiceConfig cfg = config(SynthMode::SUBTRACTIVE, OscType::SAW);
		expect(rig->voice.note_on(setup, snapshot(SynthMode::SUBTRACTIVE), note(60), rig->clock(), 1, rig->noise)).to_be_true();
		int64_t peak = 0;
		for (int32_t i = 0; i < 4; i++) {
			render_block(*rig, setup, cfg, &peak);
		}
		expect(peak).to_be_greater_than(int64_t{1} << 16);
		rig->voice.note_off(release(cfg), true, rig->clock());
		expect(blocks_until_silent(*rig, setup, cfg, 400)).to_be_less_than(400);
	});

	it("plays an FM note", _{
		auto rig = std::make_unique<Rig>();
		const NoteSetup setup = note_setup(OscType::SINE);
		const VoiceConfig cfg = config(SynthMode::FM, OscType::SINE);
		expect(rig->voice.note_on(setup, snapshot(SynthMode::FM), note(60), rig->clock(), 1, rig->noise)).to_be_true();
		int64_t peak = 0;
		for (int32_t i = 0; i < 4; i++) {
			render_block(*rig, setup, cfg, &peak);
		}
		expect(peak).to_be_greater_than(int64_t{1} << 16);
	});

	it("plays a sample through its port and releases a play-once sample before its end", _{
		auto rig = std::make_unique<Rig>();
		rig->samples[0][0] = FakeSamplePort{4000};
		const NoteSetup setup = note_setup(OscType::SAMPLE);
		const VoiceConfig cfg = config(SynthMode::SUBTRACTIVE, OscType::SAMPLE);
		expect(rig->voice.note_on(setup, snapshot(SynthMode::SUBTRACTIVE), note(60), rig->clock(), 1, rig->noise)).to_be_true();
		int64_t peak = 0;
		int64_t loudest = 0;
		bool released_before_end = false;
		for (int32_t i = 0; i < 64 && rig->samples[0][0].bytes_left_to_end() > 0; i++) {
			if (!render_block(*rig, setup, cfg, &peak, true)) {
				break;
			}
			loudest = std::max(loudest, peak);
			released_before_end = released_before_end
			    || (rig->voice.envelopes[0].stage() >= Envelope::Stage::RELEASE
			        && rig->samples[0][0].bytes_left_to_end() > 0);
		}
		expect(loudest).to_be_greater_than(int64_t{1} << 16);
		expect(released_before_end).to_be_true();
	});

	it("does not auto-release a sample whose loop end it still obeys", _{
		auto rig = std::make_unique<Rig>();
		rig->samples[0][0] = FakeSamplePort{4000};
		const NoteSetup setup = note_setup(OscType::SAMPLE);
		const VoiceConfig cfg = config(SynthMode::SUBTRACTIVE, OscType::SAMPLE);
		expect(rig->voice.note_on(setup, snapshot(SynthMode::SUBTRACTIVE), note(60), rig->clock(), 1, rig->noise)).to_be_true();
		for (int32_t i = 0; i < 20; i++) {
			render_block(*rig, setup, cfg, nullptr, false);
		}
		expect(rig->voice.envelopes[0].stage() < Envelope::Stage::RELEASE).to_be_true();
	});

	it("fast-releases to silence", _{
		auto rig = std::make_unique<Rig>();
		const NoteSetup setup = note_setup(OscType::SAW);
		const VoiceConfig cfg = config(SynthMode::SUBTRACTIVE, OscType::SAW);
		expect(rig->voice.note_on(setup, snapshot(SynthMode::SUBTRACTIVE), note(60), rig->clock(), 1, rig->noise)).to_be_true();
		render_block(*rig, setup, cfg);
		render_block(*rig, setup, cfg);
		expect(rig->voice.do_fast_release(kSoftCullIncrement, rig->clock())).to_be_true();
		expect(blocks_until_silent(*rig, setup, cfg, 64)).to_be_less_than(64);
	});

	it("fails a ring-mod note whose second source cannot play", _{
		auto rig = std::make_unique<Rig>();
		NoteSetup setup = note_setup(OscType::SAW);
		setup.sources[1] = {.osc_type = OscType::WAVETABLE, .ever_active = true, .playable = false};
		expect(rig->voice.note_on(setup, snapshot(SynthMode::RINGMOD), note(60), rig->clock(), 1, rig->noise)).to_be_false();
	});

	it("ranks a voice of a sound with more voices sooner in the cull order", _{
		auto rig = std::make_unique<Rig>();
		const NoteSetup setup = note_setup(OscType::SAW);
		expect(rig->voice.note_on(setup, snapshot(SynthMode::SUBTRACTIVE), note(60), rig->clock(), 1, rig->noise)).to_be_true();
		expect(rig->voice.priority_rating(VoicePriority::MEDIUM, 3))
		    .to_be_greater_than(rig->voice.priority_rating(VoicePriority::MEDIUM, 1));
		expect(rig->voice.priority_rating(VoicePriority::LOW, 1))
		    .to_be_greater_than(rig->voice.priority_rating(VoicePriority::HIGH, 7));
	});
});
// clang-format on

CPPSPEC_MAIN(synth_voice_spec);
