/*
 * Copyright © 2026 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 * Licensed under the GNU General Public License v3 or later; see LICENSE.
 */

// The L1 engines on the host: each engine family through a voice, a source and an engine written against the
// contracts alone, and a voice whose sound changes mode while it sounds.

#include "voice_rig.h"

#include "synth/engines/slot_engine.h"

#include <array>
#include <cppspec.hpp>
#include <cstdint>
#include <memory>

using namespace deluge::synth;
using namespace deluge::synth::test;

namespace {

/// A source no voice knows: it adds a constant level.
class DroneSource final : public Source {
public:
	explicit DroneSource(int32_t level) : level_{level} {}
	SourceRenderResult render(const SourceBlock& block) override {
		for (int32_t i = 0; i < block.num_samples; i++) {
			block.osc_buffer[i] += level_;
		}
		renders++;
		return SourceRenderResult::ACTIVE;
	}
	int32_t renders = 0;

private:
	int32_t level_;
};

/// An engine no voice knows, over the voice's slots: each active slot of each part plays its drone.
class DroneEngine final : public SlotEngine {
public:
	EngineRenderResult render(const EngineBlock& block) override {
		for (int32_t u = 0; u < block.cfg->num_unison; u++) {
			for (int32_t s = 0; s < kNumSources; s++) {
				if (block.parts[u].slots[s].active) {
					drones[s].render({.osc_buffer = block.osc_buffer, .num_samples = block.num_samples});
				}
			}
		}
		return {};
	}
	std::array<DroneSource, kNumSources> drones{DroneSource{1000}, DroneSource{20}};
};

/// The noise volume and osc 2's level for a two-source sound with noise.
NoteSetup two_sources_and_noise() {
	NoteSetup setup = note_setup(OscType::SAW);
	setup.sources[1] = {.osc_type = OscType::SQUARE, .ever_active = true};
	setup.initial_patching.preset_values[params::LOCAL_OSC_B_VOLUME] = 0x40000000;
	setup.initial_patching.preset_values[params::LOCAL_NOISE_VOLUME] = 0x20000000;
	return setup;
}

VoiceConfig two_source_config(SynthMode mode) {
	VoiceConfig cfg = config(mode, OscType::SAW);
	cfg.osc_type = {OscType::SAW, OscType::SQUARE};
	cfg.source_ever_active = {true, true};
	cfg.source_active_currently = {true, true};
	cfg.noise_volume_contains_something = true;
	return cfg;
}

/// Plays @p blocks blocks of a two-source note; @return the last block.
std::array<int32_t, kBlock * 2> play(SynthMode mode, bool osc_sync, int32_t blocks) {
	auto rig = std::make_unique<Rig>();
	const NoteSetup setup = two_sources_and_noise();
	VoiceConfig cfg = two_source_config(mode);
	cfg.doing_osc_sync = osc_sync;
	rig->voice.note_on(setup, snapshot(mode), note(60), rig->clock(), 1, rig->noise);
	for (int32_t i = 0; i < blocks; i++) {
		render_block(*rig, setup, cfg);
	}
	return rig->out;
}

bool any_sound(const std::array<int32_t, kBlock * 2>& out) {
	for (int32_t i = 0; i < kBlock; i++) {
		if (out[i] != 0) {
			return true;
		}
	}
	return false;
}

} // namespace

// clang-format off
describe synth_engine_spec("synth engines", ${
	context("the contracts", _{
		it("start the voice's slots for an engine and a source the voice does not know, and render them", _{
			auto rig = std::make_unique<Rig>();
			DroneEngine engine;
			const NoteSetup setup = note_setup(OscType::SAW);
			const SoundSnapshot snap = snapshot(SynthMode::SUBTRACTIVE);
			const VoiceConfig cfg = config(SynthMode::SUBTRACTIVE, OscType::SAW);
			expect(engine.note_on({.parts = rig->voice.unison_parts, .setup = &setup, .snapshot = &snap,
			                       .note_code = 60, .velocity = 100, .reset = true, .noise = &rig->noise}))
			    .to_be_true();
			expect(rig->voice.unison_parts[0].slots[0].active).to_be_true();
			expect(rig->voice.unison_parts[0].slots[1].active).to_be_false();

			std::array<int32_t, kBlock> out{};
			engine.render({.osc_buffer = out.data(), .num_samples = kBlock, .parts = rig->voice.unison_parts, .cfg = &cfg});
			expect(engine.drones[0].renders).to_equal(1);
			expect(engine.drones[1].renders).to_equal(0);
			expect(out[0]).to_equal(1000);
		});

		it("declare the hooks later engines need, and no engine here uses them", _{
			DroneEngine engine;
			DroneSource source{0};
			expect(engine.owns_envelope()).to_be_false();
			expect(engine.sound_stage() == nullptr).to_be_true();
			expect(source.takes_input()).to_be_false();
		});
	});

	context("subtractive", _{
		it("mixes both sources and noise, the same each time", _{
			const auto first = play(SynthMode::SUBTRACTIVE, false, 4);
			const auto second = play(SynthMode::SUBTRACTIVE, false, 4);
			expect(any_sound(first)).to_be_true();
			expect(first == second).to_be_true();
		});

		it("hard-syncs osc 2 to osc 1", _{
			expect(play(SynthMode::SUBTRACTIVE, true, 4) == play(SynthMode::SUBTRACTIVE, false, 4)).to_be_false();
		});
	});

	context("ring-mod", _{
		it("multiplies the sources rather than mixing them", _{
			const auto ring_mod = play(SynthMode::RINGMOD, false, 4);
			expect(any_sound(ring_mod)).to_be_true();
			expect(ring_mod == play(SynthMode::SUBTRACTIVE, false, 4)).to_be_false();
		});
	});

	context("FM", _{
		it("modulates its carriers with an audible modulator", _{
			auto modulated = std::make_unique<Rig>();
			auto plain = std::make_unique<Rig>();
			const NoteSetup with_modulator = note_setup(OscType::SINE);
			NoteSetup without = note_setup(OscType::SINE);
			without.initial_patching.preset_values[params::LOCAL_MODULATOR_0_VOLUME] = kMin;
			without.modulator_audible = {false, false};
			const VoiceConfig cfg = config(SynthMode::FM, OscType::SINE);
			modulated->voice.note_on(with_modulator, snapshot(SynthMode::FM), note(60), modulated->clock(), 1, modulated->noise);
			plain->voice.note_on(without, snapshot(SynthMode::FM), note(60), plain->clock(), 1, plain->noise);
			for (int32_t i = 0; i < 4; i++) {
				render_block(*modulated, with_modulator, cfg);
				render_block(*plain, without, cfg);
			}
			expect(any_sound(plain->out)).to_be_true();
			expect(modulated->out == plain->out).to_be_false();
		});
	});

	context("a voice whose sound changes mode while it sounds", _{
		it("keeps sounding from subtractive to FM and back", _{
			auto rig = std::make_unique<Rig>();
			const NoteSetup setup = note_setup(OscType::SINE);
			rig->voice.note_on(setup, snapshot(SynthMode::SUBTRACTIVE), note(60), rig->clock(), 1, rig->noise);
			int64_t peak = 0;
			render_block(*rig, setup, config(SynthMode::SUBTRACTIVE, OscType::SINE));
			render_block(*rig, setup, config(SynthMode::FM, OscType::SINE), &peak);
			expect(peak).to_be_greater_than(int64_t{0});
			render_block(*rig, setup, config(SynthMode::SUBTRACTIVE, OscType::SINE), &peak);
			expect(peak).to_be_greater_than(int64_t{1} << 16);
		});

		it("renders a held FM voice turned subtractive without an oscillator, as silence", _{
			auto rig = std::make_unique<Rig>();
			const NoteSetup setup = note_setup(OscType::SAW);
			rig->voice.note_on(setup, snapshot(SynthMode::FM), note(60), rig->clock(), 1, rig->noise);
			render_block(*rig, setup, config(SynthMode::FM, OscType::SAW));
			int64_t peak = -1;
			expect(render_block(*rig, setup, config(SynthMode::SUBTRACTIVE, OscType::SAW), &peak)).to_be_true();
			expect(peak).to_equal(int64_t{0});
		});
	});
});
// clang-format on

CPPSPEC_MAIN(synth_engine_spec);
