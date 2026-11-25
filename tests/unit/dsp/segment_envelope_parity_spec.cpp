/*
 * Copyright © 2026 Synthstrom Audible Limited
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

// SegmentEnvelope against main's envelope (main_envelope_records.h) over every stimulus, and against next's Q31
// envelope (envelope_q31_oracle.h) over the classic ADSR ones.
//
// The allowances are the spec's. main and the Q31 envelope change stage only on a block's edge, dropping the rest of
// the block, and read a decay before advancing it; this envelope changes stage at the exact sample and reads every
// stage after advancing. So a block may lead or trail the other by one where a stage changes within a block of it, and
// by two in a decay, where the two add.

#include "dsp/fixed/tables/decay_small.h"
#include "synth/modulation/segment_envelope.h"

#include "envelope_q31_oracle.h"
#include "main_envelope_records.h"
#include "main_envelope_stimuli.h"

#include <cmath>
#include <cppspec.hpp>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "support/matchers/value_matchers.h"
#include "support/spec_main.h"

namespace me = deluge::test::main_envelope;
using deluge::dsp::Q;
using deluge::synth::EnvelopeFollow;
using deluge::synth::EnvelopeMode;
using deluge::synth::EnvelopeState;
using Oracle = deluge::test::EnvelopeQ31Oracle;

namespace {

constexpr uint8_t kDecayStage = 2;

/// One render: the level (0..1) and the stage.
struct Render {
	double level;
	uint8_t stage;
};

[[nodiscard]] double level_of(int32_t word) {
	return ((static_cast<double>(word) / 2147483648.0) + 1.0) / 2.0;
}

/// SegmentEnvelope through @p s, as the probe drives main's.
[[nodiscard]] std::vector<Render> run_segment(const me::Stimulus& s) {
	const EnvelopeState state{.mode = static_cast<EnvelopeMode>(s.mode), .follow = EnvelopeFollow::Z, .curve = s.curve};
	SegmentEnvelope env;
	(void)env.note_on(s.directly_to_decay, Q<31>::from_raw(me::params_at(s, 0).sustain), state);
	std::vector<Render> out;
	for (uint32_t b = 0; b < s.blocks; ++b) {
		const me::Params p = me::params_at(s, b);
		if (b == s.note_off) {
			env.note_off(s.has_sustain);
		}
		if (b == s.fast_release) {
			env.unconditional_release(SegmentEnvelope::Stage::FAST_RELEASE, kSoftCullIncrement);
		}
		if (b == s.retrigger) {
			const Q<31> old = env.output_level();
			(void)env.note_on(s.directly_to_decay, Q<31>::from_raw(p.sustain), state);
			env.resume_attack(old);
		}
		const Q<31> word = env.render(s.block, p.attack, p.decay, Q<31>::from_raw(p.sustain), p.release, state,
		                              Q<31>::from_raw(me::follow_at(s, b)));
		out.push_back({level_of(word.raw()), static_cast<uint8_t>(env.stage())});
	}
	return out;
}

/// The Q31 envelope through @p s (classic ADSR), as the voice drove it.
[[nodiscard]] std::vector<Render> run_oracle(const me::Stimulus& s) {
	Oracle env;
	(void)env.note_on(s.directly_to_decay, Q<31>::from_raw(me::params_at(s, 0).sustain));
	std::vector<Render> out;
	for (uint32_t b = 0; b < s.blocks; ++b) {
		const me::Params p = me::params_at(s, b);
		if (b == s.note_off) {
			env.note_off(s.has_sustain);
		}
		if (b == s.fast_release) {
			env.unconditional_release(Oracle::Stage::FAST_RELEASE, deluge::test::kOracleSoftCullIncrement);
		}
		if (b == s.retrigger) {
			const Q<31> old = env.output_level();
			(void)env.note_on(s.directly_to_decay, Q<31>::from_raw(p.sustain));
			env.resume_attack(old);
		}
		const Q<31> word = env.render(s.block, p.attack, p.decay, Q<31>::from_raw(p.sustain), p.release,
		                              deluge::dsp::fixed::tables::kDecaySmall8.data());
		out.push_back({level_of(word.raw()), static_cast<uint8_t>(env.stage())});
	}
	return out;
}

/// main's record @p i as renders.
[[nodiscard]] std::vector<Render> main_renders(size_t i) {
	const me::Record& r = me::kRecords[i];
	std::vector<Render> out;
	for (uint32_t b = 0; b < r.count; ++b) {
		out.push_back({level_of(r.words[b]), r.stages[b]});
	}
	return out;
}

/// Whether a stage changes within a block of block @p b in @p r.
[[nodiscard]] bool near_change(const std::vector<Render>& r, size_t b) {
	const bool before = b > 0 && r[b - 1].stage != r[b].stage;
	const bool after = b + 1 < r.size() && r[b + 1].stage != r[b].stage;
	return before || after;
}

constexpr uint8_t kAttackStage = 0;
constexpr uint8_t kReleaseStage = 4;
constexpr uint8_t kFastReleaseStage = 5;

[[nodiscard]] bool releasing(uint8_t stage) {
	return stage == kReleaseStage || stage == kFastReleaseStage;
}

/// Per block of @p r: the stage boundaries passed since the note began or was struck again, and, in a release, the
/// level the release started from (the block before it).
struct Course {
	std::vector<size_t> boundaries;
	std::vector<double> release_from;
};
[[nodiscard]] Course course(const std::vector<Render>& r, size_t struck_again) {
	Course c{std::vector<size_t>(r.size()), std::vector<double>(r.size())};
	size_t passed = 0;
	double from = 0.0;
	for (size_t b = 0; b < r.size(); ++b) {
		const bool changed = b > 0 && r[b].stage != r[b - 1].stage;
		if (changed && r[b].stage == kAttackStage) {
			// Struck again: the attack resumes where each finds the level on it, @p struck_again boundaries' worth
			passed = struck_again;
		}
		else if (changed) {
			++passed;
		}
		if (releasing(r[b].stage) && (b == 0 || r[b - 1].stage != r[b].stage)) {
			from = b > 0 ? r[b - 1].level : 0.0;
		}
		c.boundaries[b] = passed;
		c.release_from[b] = from;
	}
	return c;
}

/// The blocks of @p ours that no shift within the allowance matches, by more than @p tolerance; prints the first.
///
/// Before a release, a block may lead or trail by one block for each stage boundary passed since the note began (the
/// other changes stage on a block's edge, dropping the rest of the block or, where it advances before reading, running
/// it twice), plus one in a decay (the other's late read), at most three. A monotone stage shifted by a fraction of
/// that lies within the range the other takes over those blocks.
///
/// A release starts on the same block's edge in both, so it may lead or trail by one; it is compared by its shape,
/// rescaled from the level it started at to the other's, since a release begun in a decay starts from the decay's
/// level, which the allowance above already compared.
/// @p struck_again is the boundaries a note struck again counts as.
[[nodiscard]] size_t mismatches(const char* what, size_t index, const std::vector<Render>& ours,
                                const std::vector<Render>& theirs, double tolerance, size_t struck_again) {
	const Course mine = course(ours, struck_again);
	const Course other = course(theirs, struck_again);
	size_t count = 0;
	for (size_t b = 0; b < ours.size(); ++b) {
		double level = ours[b].level;
		size_t w = 1;
		if (releasing(ours[b].stage) && releasing(theirs[b].stage)) {
			if (mine.release_from[b] > 0.0) {
				level = level / mine.release_from[b] * other.release_from[b];
			}
		}
		else {
			const bool decay = ours[b].stage == kDecayStage || theirs[b].stage == kDecayStage;
			w = std::min<size_t>(std::max(mine.boundaries[b], other.boundaries[b]) + (decay ? 1 : 0), 3);
			if (w == 0 && (near_change(ours, b) || near_change(theirs, b))) {
				w = 1;
			}
		}
		double low = theirs[b].level;
		double high = low;
		for (size_t j = (b >= w ? b - w : 0); j <= std::min(b + w, theirs.size() - 1); ++j) {
			low = std::min(low, theirs[j].level);
			high = std::max(high, theirs[j].level);
		}
		// Within a window's reach of the record's end, the blocks it would look ahead to were never rendered: a level
		// past the last one may be what the next block would show
		const bool cut_short = b + w >= theirs.size();
		const bool past_end = cut_short && (level > high + tolerance) == (theirs.back().level >= theirs[b].level);
		if ((level < low - tolerance || level > high + tolerance) && !past_end) {
			if (count == 0) {
				std::printf("%s case %zu block %zu: ours %.7f (stage %u), theirs %.7f..%.7f (stage %u)\n", what, index,
				            b, level, ours[b].stage, low, high, theirs[b].stage);
			}
			++count;
		}
	}
	return count;
}

} // namespace

// clang-format off
describe segment_envelope_parity_spec_body("SegmentEnvelope against main's and the Q31 envelope", ${
	// A note struck again resumes its attack by the curve's inverse in both: one boundary.
	it("matches main's envelope within 2^-16, with the block allowances", _ {
		size_t failed = 0;
		for (size_t i = 0; i < me::kStimuli.size(); ++i) {
			failed += mismatches("main", i, run_segment(me::kStimuli[i]), main_renders(i), 0x1p-16, 1) > 0 ? 1 : 0;
		}
		expect(failed).to_equal(0U);
	});

	// The Q31 envelope interpolates its tables linearly between 257 points: on the k = 8 curve that errs by up to
	// h^2/8 * f'' = 6e-5 near the curve's start, above the tables' rounding (8.3e-6), so the tolerance is their sum. It
	// resumes a note struck again by its table's inverse, which places the attack about a block behind the curve's
	// (1.04 blocks in the retrigger case): two boundaries.
	it("matches the Q31 envelope on the classic ADSR within its tables' interpolation, with the block allowances", _ {
		size_t failed = 0;
		for (size_t i = 0; i < me::kStimuli.size(); ++i) {
			const me::Stimulus& s = me::kStimuli[i];
			if (s.mode != me::Mode::ADSR || s.curve != 32) {
				continue;
			}
			failed += mismatches("q31", i, run_segment(s), run_oracle(s), 7e-5, 2) > 0 ? 1 : 0;
		}
		expect(failed).to_equal(0U);
	});
});
// clang-format on

DELUGE_SPEC(segment_envelope_parity_spec, segment_envelope_parity_spec_body)
