/*
 * Copyright © 2014-2023 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */
#include "voice_rig.h"

#include "synth/sources/dx7/dx_voice_port.h"
#include "synth/voice/unison.h"

#include <array>
#include <cppspec.hpp>
#include <cstdint>
#include <format>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "support/spec_main.h"

using namespace deluge::synth;
using namespace deluge::synth::test;

namespace {

/// A DX7 port that logs every call and plays a saw from the increment it is given, ending four blocks after its
/// key-up: whatever reaches it shows in its log or its sound.
class ToneDxPort final : public DxVoicePort {
public:
	std::vector<std::string> calls;

	bool note_on(void* patch, int midinote, int velocity) override {
		calls.push_back(std::format("on {} {} {}", patch != nullptr, midinote, velocity));
		live_ = true;
		blocks_since_keyup_ = -1;
		return true;
	}
	void keyup() override {
		calls.emplace_back("keyup");
		blocks_since_keyup_ = 0;
	}
	void unassign() override {
		calls.emplace_back("unassign");
		live_ = false;
	}
	std::span<const deluge::dsp::Q<24>> render(int32_t num_samples, uint32_t phase_increment, int32_t amp_mod_param,
	                                           bool patch_changed, int midinote) override {
		calls.push_back(
		    std::format("render {} {} {} {} {}", num_samples, phase_increment, amp_mod_param, patch_changed, midinote));
		if (!live_ || blocks_since_keyup_ > 4) {
			return {};
		}
		if (blocks_since_keyup_ >= 0) {
			blocks_since_keyup_++;
		}
		for (int32_t i = 0; i < num_samples; i++) {
			phase_ += phase_increment;
			// A saw in Q24: the phase's top 24 bits, centred
			buffer_[i] = deluge::dsp::Q<24>::from_raw(static_cast<int32_t>(phase_ >> 8) - (1 << 23));
		}
		return {buffer_.data(), static_cast<size_t>(num_samples)};
	}

private:
	std::array<deluge::dsp::Q<24>, kBlock * 2> buffer_{};
	uint32_t phase_{};
	bool live_{};
	int32_t blocks_since_keyup_{-1};
};

/// A rig whose every slot plays through its own logging DX7 port, declared first so it outlives the slots (their
/// destructors unassign through it).
struct DxRig {
	ToneDxPort tone;
	std::unique_ptr<Rig> rig = std::make_unique<Rig>();
	DxRig() {
		for (int32_t u = 0; u < kMaxNumVoicesUnison; u++) {
			for (int32_t s = 0; s < kNumSources; s++) {
				rig->voice.unison_parts[u].slots[s].bind(
				    {.sample = &rig->samples[u][s], .dx = &tone, .input = &rig->input});
			}
		}
	}
};

/// What one note left: each block's output, the port's log, and the noise stream's next word.
struct Played {
	std::vector<std::array<deluge::dsp::Q<31>, kBlock * 2>> blocks;
	std::vector<std::string> calls;
	uint32_t next_noise{};
};

int32_t dummy_patch = 0;

/// The DX7 on osc 1, osc 2 silent.
NoteSetup dx7_setup() {
	NoteSetup setup = note_setup(OscType::DX7);
	setup.sources[0].dx_patch = &dummy_patch;
	return setup;
}

/// Plays one note of @p setup in @p mode: the phase draws, the note-on, six blocks (the patch changed on the third
/// when @p patch_change), the release, then blocks until the voice ends; @p shape adjusts the config first.
Played play(SynthMode mode, const NoteSetup& setup, const std::function<void(RenderConfig&)>& shape,
            bool patch_change = false) {
	DxRig dx;
	Rig& rig = *dx.rig;
	RenderConfig cfg = config(mode, OscType::DX7);
	shape(cfg);
	rig.voice.randomize_osc_phases(cfg.sound, rig.noise);
	(void)rig.voice.note_on(setup, cfg.sound, note(60), rig.clock(), 1, rig.noise);
	Played played;
	// A voice that ends is not rendered again (the pool frees it)
	bool alive = true;
	for (int32_t b = 0; b < 6 && alive; b++) {
		RenderConfig block_cfg = cfg;
		block_cfg.values.dx_patch_changed[0] = patch_change && b == 2;
		alive = render_block(rig, setup, block_cfg);
		played.blocks.push_back(rig.out);
	}
	if (alive) {
		ReleaseInfo off = release(cfg);
		off.num_unison = cfg.sound.voicing.unison.count;
		rig.voice.note_off(off, true, rig.clock());
	}
	for (int32_t b = 0; b < 64 && alive; b++) {
		alive = render_block(rig, setup, cfg);
		played.blocks.push_back(rig.out);
	}
	played.calls = dx.tone.calls;
	played.next_noise = rig.noise.next();
	return played;
}

/// How @p dx7 differs from @p subtractive, or empty when it plays the same: every block, the port's log and the noise.
std::string difference(const Played& subtractive, const Played& dx7) {
	if (dx7.blocks.size() != subtractive.blocks.size()) {
		return std::format("{} blocks, not {}", dx7.blocks.size(), subtractive.blocks.size());
	}
	for (size_t b = 0; b < dx7.blocks.size(); b++) {
		if (dx7.blocks[b] != subtractive.blocks[b]) {
			return std::format("block {} differs", b);
		}
	}
	if (dx7.calls != subtractive.calls) {
		return std::format("the port's log differs ({} calls, not {})", dx7.calls.size(), subtractive.calls.size());
	}
	if (dx7.next_noise != subtractive.next_noise) {
		return "the noise stream differs";
	}
	if (dx7.calls.size() <= 8) {
		return "the DX7 barely played";
	}
	return {};
}

/// How the DX7 mode's note differs from the subtractive engine's with the DX7 on osc 1.
std::string dx7_mode_difference(const NoteSetup& setup, const std::function<void(RenderConfig&)>& shape,
                                bool patch_change = false) {
	return difference(play(SynthMode::SUBTRACTIVE, setup, shape, patch_change),
	                  play(SynthMode::DX7, setup, shape, patch_change));
}

void as_is(RenderConfig& /*cfg*/) {
}

} // namespace

// clang-format off
describe dx7_engine_spec_body("the DX7 engine", ${
	it("plays a free-running DX7 as the subtractive engine plays it on osc 1", _{
		expect(dx7_mode_difference(dx7_setup(), as_is)).to_equal(std::string{});
	});

	it("plays three spread unison parts, each its own DX7, the same", _{
		const auto spread = [](RenderConfig& cfg) {
			cfg.sound.voicing.unison = derive_unison(3, 20, 50);
			cfg.sound.voicing.unison_spread = 50;
		};
		expect(dx7_mode_difference(dx7_setup(), spread)).to_equal(std::string{});
	});

	it("plays a retriggered DX7 the same", _{
		expect(dx7_mode_difference(dx7_setup(), [](RenderConfig& cfg) { cfg.sound.engine.osc_retrigger_phase[0] = 0x40000000; }))
		    .to_equal(std::string{});
	});

	it("passes a patch change on to the DX7 the same", _{
		expect(dx7_mode_difference(dx7_setup(), as_is, /*patch_change=*/true)).to_equal(std::string{});
	});

	it("plays the noise source beside the DX7 the same", _{
		NoteSetup setup = dx7_setup();
		setup.initial_patching.preset_values[params::LOCAL_NOISE_VOLUME] = deluge::dsp::Q<31>::from_raw(0x20000000);
		expect(dx7_mode_difference(setup, [](RenderConfig& cfg) { cfg.values.noise_volume_contains_something = true; }))
		    .to_equal(std::string{});
	});

	it("never sounds osc 2", _{
		NoteSetup with_osc2 = dx7_setup();
		with_osc2.sources[1].ever_active = true;
		with_osc2.initial_patching.preset_values[params::LOCAL_OSC_B_VOLUME] = deluge::dsp::Q<31>::from_raw(0x40000000);
		const auto osc2_on = [](RenderConfig& cfg) {
			cfg.values.source_ever_active = {true, true};
			cfg.values.source_active_currently = {true, true};
		};
		const Played dx7 = play(SynthMode::DX7, with_osc2, osc2_on);
		const Played subtractive = play(SynthMode::SUBTRACTIVE, dx7_setup(), as_is);
		expect(dx7.blocks.size()).to_equal(subtractive.blocks.size());
		expect(dx7.blocks == subtractive.blocks).to_equal(true);
	});
});
// clang-format on

DELUGE_SPEC(dx7_engine_spec, dx7_engine_spec_body)
