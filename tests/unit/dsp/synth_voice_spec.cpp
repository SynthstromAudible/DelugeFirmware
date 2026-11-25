/*
 * Copyright © 2026 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 * Licensed under the GNU General Public License v3 or later; see LICENSE.
 */

// synth::Voice played on the host, with nothing of the app: notes through the kernel's lifecycle, rendered from a
// RenderConfig, with fake ports behind its sources.

#include "synth/matrix/compiler.h"
#include "synth/modulation/patch_routing.h"

#include "voice_rig.h"

#include <cppspec.hpp>
#include <cstdint>
#include <memory>
#include <vector>

#include "support/spec_main.h"

using namespace deluge::synth;
using namespace deluge::synth::test;

// clang-format off
describe synth_voice_spec_body("synth::Voice", ${
	it("plays a virtual-analog note and releases it to silence", _{
		auto rig = std::make_unique<Rig>();
		const NoteSetup setup = note_setup(OscType::SAW);
		const RenderConfig cfg = config(SynthMode::SUBTRACTIVE, OscType::SAW);
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
		const RenderConfig cfg = config(SynthMode::FM, OscType::SINE);
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
		const RenderConfig cfg = config(SynthMode::SUBTRACTIVE, OscType::SAMPLE);
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
			    || (rig->voice.envelope(0).stage() >= Envelope::Stage::RELEASE
			        && rig->samples[0][0].bytes_left_to_end() > 0);
		}
		expect(loudest).to_be_greater_than(int64_t{1} << 16);
		expect(released_before_end).to_be_true();
	});

	it("does not auto-release a sample whose loop end it still obeys", _{
		auto rig = std::make_unique<Rig>();
		rig->samples[0][0] = FakeSamplePort{4000};
		const NoteSetup setup = note_setup(OscType::SAMPLE);
		const RenderConfig cfg = config(SynthMode::SUBTRACTIVE, OscType::SAMPLE);
		expect(rig->voice.note_on(setup, snapshot(SynthMode::SUBTRACTIVE), note(60), rig->clock(), 1, rig->noise)).to_be_true();
		for (int32_t i = 0; i < 20; i++) {
			render_block(*rig, setup, cfg, nullptr, false);
		}
		expect(rig->voice.envelope(0).stage()).to_be_less_than(Envelope::Stage::RELEASE);
	});

	it("fast-releases to silence", _{
		auto rig = std::make_unique<Rig>();
		const NoteSetup setup = note_setup(OscType::SAW);
		const RenderConfig cfg = config(SynthMode::SUBTRACTIVE, OscType::SAW);
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

	// The voice patches its params at note-on before its envelopes give their note-on levels, so its first render must
	// patch from them, even where an envelope's level does not move in that render (an instant attack held at full)
	it("patches from the envelopes' note-on levels on its first render, though they do not move", _{
		auto rig = std::make_unique<Rig>();
		NoteSetup setup = note_setup(OscType::SAW);
		const std::array records{CableRecord{.source = PatchSource::ENVELOPE_1,
		                                     .target = CableTarget::param(params::LOCAL_OSC_A_PITCH_ADJUST),
		                                     .polarity = CablePolarity::BIPOLAR}};
		RoutingBuild build;
		compile_routing(records, std::bitset<kMaxCables>{1}, build);
		std::vector<std::byte> storage(matrix::program_size(build) + alignof(matrix::MatrixProgram));
		std::byte* at = storage.data();
		while (reinterpret_cast<std::uintptr_t>(at) % alignof(matrix::MatrixProgram) != 0) {
			++at;
		}
		setup.initial_patching.program = matrix::compile_program(build, {at, matrix::program_size(build)});
		setup.initial_patching.depths[0] = deluge::dsp::Q<30>::from_raw(-(1 << 28));
		setup.initial_patching.preset_values[params::LOCAL_ENV_1_ATTACK] = deluge::dsp::Q<31>::from_raw(kMin);
		setup.initial_patching.preset_values[params::LOCAL_ENV_1_SUSTAIN] = deluge::dsp::Q<31>::from_raw(kMax);
		setup.attack_is_zero[1] = true;
		RenderConfig cfg = config(SynthMode::SUBTRACTIVE, OscType::SAW);
		cfg.values.local_sources_patched_to_anything = 1U << util::to_underlying(PatchSource::ENVELOPE_1);
		expect(rig->voice.note_on(setup, snapshot(SynthMode::SUBTRACTIVE), note(60), rig->clock(), 1, rig->noise)).to_be_true();
		const int32_t at_note_on = rig->voice.param_final_values()[params::LOCAL_OSC_A_PITCH_ADJUST];
		render_block(*rig, setup, cfg);
		expect(rig->voice.param_final_values()[params::LOCAL_OSC_A_PITCH_ADJUST]).to_be_less_than(at_note_on);
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

DELUGE_SPEC(synth_voice_spec, synth_voice_spec_body)
