#include "instruments/classic/deluge_synth.h"

#include <cppspec.hpp>
#include <cstdint>
#include <limits>

using namespace deluge::instruments::classic;
using deluge::effects::FilterMode;
using LowPass = deluge::dsp::fixed::LowPassSlot;
using HighPass = deluge::dsp::fixed::HighPassSlot;

namespace {

constexpr int32_t kMin = std::numeric_limits<int32_t>::min();

/// Facts whose filters are set but could not be heard: the low-pass open, the high-pass closed, neither morphed.
SoundBlockFacts quiet() {
	return {.has_filters = true,
	        .lpf_mode = FilterMode::TRANSISTOR_24DB,
	        .hpf_mode = FilterMode::HPLADDER,
	        .lpf_morph = kMin,
	        .lpf_freq = 0x7FFFFFFF,
	        .hpf_morph = kMin,
	        .hpf_freq = kMin};
}

bool lowRuns(const SoundBlockFacts& facts) {
	return DelugeSynth::begin_block({}, facts).low_pass != LowPass{};
}

bool highRuns(const SoundBlockFacts& facts) {
	return DelugeSynth::begin_block({}, facts).high_pass != HighPass{};
}

/// quiet() with @p change applied.
template <class Change>
SoundBlockFacts with(Change change) {
	SoundBlockFacts facts = quiet();
	change(facts);
	return facts;
}

} // namespace

// clang-format off
describe block_values("a block's values", ${
	it("runs neither filter when neither could be heard", _{
		expect(lowRuns(quiet())).to_equal(false);
		expect(highRuns(quiet())).to_equal(false);
	});

	it("runs the low-pass when driven, modulated, below its top or morphed", _{
		SoundBlockFacts driven = quiet();
		driven.lpf_mode = FilterMode::TRANSISTOR_24DB_DRIVE;
		expect(lowRuns(driven)).to_equal(true);
		expect(lowRuns(with([](SoundBlockFacts& f) { f.lpf_modulated = true; }))).to_equal(true);
		SoundBlockFacts lower = quiet();
		lower.lpf_freq = 0x7FFFFFD1;
		expect(lowRuns(lower)).to_equal(true);
		SoundBlockFacts morphed = quiet();
		morphed.lpf_morph = kMin + 1;
		expect(lowRuns(morphed)).to_equal(true);
	});

	it("keeps the low-pass off at its top and the high-pass off at its bottom", _{
		SoundBlockFacts top = quiet();
		top.lpf_freq = 0x7FFFFFD2;
		expect(lowRuns(top)).to_equal(false);
		SoundBlockFacts bottom = quiet();
		bottom.hpf_freq = kMin;
		expect(highRuns(bottom)).to_equal(false);
	});

	it("runs the high-pass when modulated, above its bottom or morphed", _{
		expect(highRuns(with([](SoundBlockFacts& f) { f.hpf_modulated = true; }))).to_equal(true);
		SoundBlockFacts higher = quiet();
		higher.hpf_freq = kMin + 1;
		expect(highRuns(higher)).to_equal(true);
		SoundBlockFacts morphed = quiet();
		morphed.hpf_morph = kMin + 1;
		expect(highRuns(morphed)).to_equal(true);
	});

	it("runs no filter without filters, whatever else holds", _{
		SoundBlockFacts none = quiet();
		none.has_filters = false;
		none.lpf_mode = FilterMode::TRANSISTOR_24DB_DRIVE;
		none.lpf_modulated = true;
		none.hpf_modulated = true;
		expect(lowRuns(none)).to_equal(false);
		expect(highRuns(none)).to_equal(false);
	});

	it("passes the host's and the sound's facts through", _{
		SoundBlockFacts facts = quiet();
		facts.clipping_amount = 3;
		facts.doing_osc_sync = true;
		facts.note_tails_allowed = true;
		facts.source_ever_active = {true, false};
		facts.source_active_currently = {false, true};
		facts.dx7_active_currently = true;
		facts.dx7_patch_changed = true;
		facts.noise_can_sound = true;
		facts.local_wired = 0b101;
		facts.bend_ranges = {2, 48};
		facts.portamento = 77;
		facts.filter_route = deluge::effects::FilterRoute::PARALLEL;
		facts.lfo2_synced_phase_increment = deluge::dsp::UQ<32>::from_raw(11);
		facts.lfo4_synced_phase_increment = deluge::dsp::UQ<32>::from_raw(13);
		facts.expression_sources_changed.set(1);
		facts.monophonic_expression_values = {5, 6, 7};
		facts.global_source_values[2] = 99;
		const deluge::synth::BlockValues values =
		    DelugeSynth::begin_block({.cpu_direness = 4, .engine_in_stereo = true}, facts);
		expect(values.cpu_direness).to_equal(4);
		expect(values.engine_rendering_in_stereo).to_equal(true);
		expect(values.has_filters).to_equal(true);
		expect(int{values.clipping_amount}).to_equal(3);
		expect(values.doing_osc_sync).to_equal(true);
		expect(values.note_tails_allowed).to_equal(true);
		expect(values.source_ever_active).to_equal(std::array<bool, 2>{true, false});
		expect(values.source_active_currently).to_equal(std::array<bool, 2>{false, true});
		expect(values.dx7_active_currently).to_equal(true);
		expect(values.dx7_patch_changed).to_equal(true);
		expect(values.noise_volume_contains_something).to_equal(true);
		expect(values.local_sources_patched_to_anything).to_equal(uint32_t{0b101});
		expect(values.bend_ranges).to_equal(std::array<uint8_t, 2>{2, 48});
		expect(values.portamento).to_equal(77);
		expect(values.filter_route).to_equal(deluge::effects::filter_route(deluge::effects::FilterRoute::PARALLEL));
		expect(values.lfo2_synced_phase_increment.raw()).to_equal(11U);
		expect(values.lfo4_synced_phase_increment.raw()).to_equal(13U);
		expect(values.expression_sources_changed_at_synth_level.test(1)).to_equal(true);
		expect(values.monophonic_expression_values).to_equal(std::array<int32_t, 3>{5, 6, 7});
		expect(values.global_source_values[2]).to_equal(99);
	});
});
// clang-format on

CPPSPEC_SPEC(block_values)
