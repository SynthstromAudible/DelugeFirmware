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
#include <string_view>
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

/// A rig whose every DX7 engine part plays through one logging DX7 port, declared first so it outlives the engine (its
/// destructor unassigns through it).
struct DxRig {
	ToneDxPort tone;
	std::unique_ptr<Rig> rig = std::make_unique<Rig>();
	DxRig() {
		for (int32_t u = 0; u < kMaxNumVoicesUnison; u++) {
			rig->voice.bind_dx7(u, &tone);
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

/// The DX7 engine's setup.
NoteSetup dx7_setup() {
	NoteSetup setup = note_setup(OscType::DX7);
	setup.dx7 = {.patch = &dummy_patch, .ever_active = true};
	return setup;
}

/// Plays one note of @p setup in @p mode: the phase draws, the note-on, six blocks (the patch changed on the third
/// when @p patch_change; the level halved from the fourth on when @p level_change), the release, then blocks until the
/// voice ends; @p shape adjusts the config first.
Played play(SynthMode mode, const NoteSetup& setup, const std::function<void(RenderConfig&)>& shape,
            bool patch_change = false, bool level_change = false) {
	NoteSetup quieter = setup;
	quieter.initial_patching.preset_values[params::LOCAL_OSC_A_VOLUME] = deluge::dsp::Q<31>::from_raw(0x40000000);
	DxRig dx;
	Rig& rig = *dx.rig;
	RenderConfig cfg = config(mode, OscType::DX7);
	shape(cfg);
	cfg.values.dx7_active_currently = cfg.values.source_active_currently[0];
	rig.voice.randomize_osc_phases(cfg.sound, rig.noise);
	(void)rig.voice.note_on(setup, cfg.sound, note(60), rig.clock(), 1, rig.noise);
	Played played;
	// A voice that ends is not rendered again (the pool frees it)
	bool alive = true;
	for (int32_t b = 0; b < 6 && alive; b++) {
		RenderConfig block_cfg = cfg;
		block_cfg.values.dx7_patch_changed = patch_change && b == 2;
		alive = render_block(rig, level_change && b >= 3 ? quieter : setup, block_cfg);
		played.blocks.push_back(rig.out);
	}
	if (alive) {
		ReleaseInfo off = release(cfg);
		off.num_unison = cfg.sound.voicing.unison.count;
		rig.voice.note_off(off, true, rig.clock());
	}
	for (int32_t b = 0; b < 64 && alive; b++) {
		alive = render_block(rig, level_change ? quieter : setup, cfg);
		played.blocks.push_back(rig.out);
	}
	played.calls = dx.tone.calls;
	played.next_noise = rig.noise.next();
	return played;
}

/// A hash of everything @p played left (FNV-1a over each block's samples, the port's log and the noise word), as hex.
std::string fingerprint(const Played& played) {
	uint64_t hash = 0xcbf29ce484222325;
	const auto mix = [&hash](uint32_t word) {
		for (int32_t i = 0; i < 4; i++) {
			hash = (hash ^ ((word >> (8 * i)) & 0xff)) * 0x100000001b3;
		}
	};
	for (const auto& block : played.blocks) {
		for (const auto sample : block) {
			mix(static_cast<uint32_t>(sample.raw()));
		}
	}
	for (const auto& call : played.calls) {
		for (const char c : call) {
			mix(static_cast<uint8_t>(c));
		}
		mix('\n');
	}
	mix(played.next_noise);
	return std::format("{:016x}", hash);
}

/// How the DX7 mode's note differs from @p pin, the answer the subtractive slot path gave before it was retired, or
/// empty when it plays the same.
std::string pinned_difference(std::string_view pin, const NoteSetup& setup,
                              const std::function<void(RenderConfig&)>& shape, bool patch_change = false,
                              bool level_change = false) {
	const Played dx7 = play(SynthMode::DX7, setup, shape, patch_change, level_change);
	if (dx7.calls.size() <= 8) {
		return "the DX7 barely played";
	}
	if (fingerprint(dx7) != pin) {
		return std::format("plays {}, not the pinned {}", fingerprint(dx7), pin);
	}
	return {};
}

void as_is(RenderConfig& /*cfg*/) {
}

// The subtractive slot path's answers, recorded before the slot DX7 was retired (a retrigger reaches neither path's
// DX7, so the retriggered note plays as the free-running one).
constexpr std::string_view kFreeRunning = "c6f3b030c9df7c01";
constexpr std::string_view kSpreadUnison = "17ffef5b9a66d347";
constexpr std::string_view kRetriggered = "c6f3b030c9df7c01";
constexpr std::string_view kFirstBlockRamp = "e74f0772b9e50cb3";
constexpr std::string_view kPatchChange = "12d9f0478b0ad262";

} // namespace

// clang-format off
describe dx7_engine_spec_body("the DX7 engine", ${
	it("plays a free-running DX7 as the subtractive engine plays it on osc 1", _{
		expect(pinned_difference(kFreeRunning, dx7_setup(), as_is)).to_equal(std::string{});
	});

	it("plays three spread unison parts, each its own DX7, the same", _{
		const auto spread = [](RenderConfig& cfg) {
			cfg.sound.voicing.unison = derive_unison(3, 20, 50);
			cfg.sound.voicing.unison_spread = 50;
		};
		expect(pinned_difference(kSpreadUnison, dx7_setup(), spread)).to_equal(std::string{});
	});

	it("plays a retriggered DX7 the same", _{
		expect(pinned_difference(kRetriggered, dx7_setup(), [](RenderConfig& cfg) { cfg.sound.engine.osc_retrigger_phase[0] = 0x40000000; }))
		    .to_equal(std::string{});
	});

	it("ramps the first block's level from silence the same", _{
		NoteSetup ramped = dx7_setup();
		ramped.initial_patching.preset_values[params::LOCAL_ENV_0_ATTACK] = deluge::dsp::Q<31>::from_raw(kMax);
		expect(pinned_difference(kFirstBlockRamp, ramped, as_is)).to_equal(std::string{});
	});

	it("passes a patch change on to the DX7 the same", _{
		expect(pinned_difference(kPatchChange, dx7_setup(), as_is, /*patch_change=*/true)).to_equal(std::string{});
	});

	it("plays no noise source: a DX7 with the noise up plays as the subtractive DX7 without it", _{
		NoteSetup noisy = dx7_setup();
		noisy.initial_patching.preset_values[params::LOCAL_NOISE_VOLUME] = deluge::dsp::Q<31>::from_raw(0x20000000);
		const Played dx7 = play(SynthMode::DX7, noisy,
		                        [](RenderConfig& cfg) { cfg.values.noise_volume_contains_something = true; });
		expect(fingerprint(dx7)).to_equal(std::string{kFreeRunning});
	});

	it("reads its pitch from its own setup, not osc 1's", _{
		NoteSetup up = dx7_setup();
		up.dx7.transpose = 7;
		const Played transposed = play(SynthMode::DX7, up, as_is);
		const Played plain = play(SynthMode::DX7, dx7_setup(), as_is);
		expect(transposed.calls == plain.calls).to_equal(false);
	});

	it("never sounds osc 2, whatever its setup", _{
		NoteSetup with_osc2 = dx7_setup();
		with_osc2.sources[1].ever_active = true;
		with_osc2.initial_patching.preset_values[params::LOCAL_OSC_B_VOLUME] = deluge::dsp::Q<31>::from_raw(0x40000000);
		const auto osc2_on = [](RenderConfig& cfg) {
			cfg.values.source_ever_active = {true, true};
			cfg.values.source_active_currently = {true, true};
		};
		const Played dx7 = play(SynthMode::DX7, with_osc2, osc2_on);
		expect(fingerprint(dx7)).to_equal(std::string{kFreeRunning});
	});
});
// clang-format on

DELUGE_SPEC(dx7_engine_spec, dx7_engine_spec_body)
