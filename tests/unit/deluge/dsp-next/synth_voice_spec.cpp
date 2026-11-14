/*
 * Copyright © 2026 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 * Licensed under the GNU General Public License v3 or later; see LICENSE.
 */

// synth::Voice played on the host, with nothing of the app: notes through the kernel's lifecycle, rendered from a
// VoiceConfig, with fake ports behind its sources.

#include "voice_rig.h"

#include <cppspec.hpp>
#include <cstdint>
#include <memory>

using namespace deluge::synth;
using namespace deluge::synth::test;

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
		const VoiceConfig cfg = config(SynthMode::SUBTRACTIVE, OscType::SAMPLE);
		expect(rig->voice.note_on(setup, snapshot(SynthMode::SUBTRACTIVE), note(60), rig->clock(), 1, rig->noise)).to_be_true();
		for (int32_t i = 0; i < 20; i++) {
			render_block(*rig, setup, cfg, nullptr, false);
		}
		expect(rig->voice.envelope(0).stage() < Envelope::Stage::RELEASE).to_be_true();
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
