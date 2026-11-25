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

#include "synth/modulation/envelope.h"

#include "dsp/fixed/osc/sine_table.h"

#include <algorithm>

using deluge::dsp::Q;
using deluge::dsp::fixed::carried;
using deluge::dsp::fixed::EnvelopeCurve;
using deluge::synth::eighths_of;
using deluge::synth::EnvelopeEighths;
using deluge::synth::EnvelopeMode;

namespace {

constexpr Q<31> kFullScale = Q<31>::max();
constexpr Q<31> kHalfScale = deluge::dsp::q<31>(0.5); ///< The output's zero-centre, and the sine's mean.
constexpr Q<31> kSilent = Q<31>::min();               ///< The centred output of a silent envelope.
/// The least level an attack or a held AR or MPE note gives: a voice is freed at exactly 0.
constexpr Q<31> kFloor = deluge::dsp::q<31>(0x1p-31);

/// A level centred about 0, as the patch source reads it: less a half, doubled.
[[nodiscard]] Q<31> centred(Q<31> level) {
	return scale_shl_wrap<1>(sub_wrap(level, kHalfScale));
}
/// The product of two levels, rounded: at least one is not negative, so it cannot saturate.
[[gnu::always_inline, nodiscard]] inline Q<31> product(Q<31> a, Q<31> b) {
	return Q<31>::from_raw(static_cast<int32_t>(((int64_t{a.raw()} * b.raw()) + (1 << 30)) >> 31)); // Raw: Q62 to Q31
}

} // namespace

[[gnu::always_inline]] inline void Envelope::smooth_sustain(uint32_t num_samples, Q<31> sustain_level) {
	// Smoothed toward its target: modulating sustain would otherwise jump downstream, since params don't smooth the
	// envelope and envelopes render before the other params, their own sustain included
	// Raw: 1/512 of the gap a sample, rounded, and a word at least so it arrives
	const int32_t gap = sustain_level.raw() - smoothed_sustain_.raw();
	auto step = static_cast<int32_t>(((int64_t{gap} * std::min<uint32_t>(num_samples, 512)) + 256) >> 9);
	if (step == 0) [[unlikely]] {
		step = static_cast<int32_t>(gap > 0) - static_cast<int32_t>(gap < 0);
	}
	smoothed_sustain_ = Q<31>::from_raw(smoothed_sustain_.raw() + step);
}

[[gnu::always_inline]] inline Q<31> Envelope::attack_level(int8_t eighths) {
	level_ = std::max(sub_wrap(kFullScale, EnvelopeCurve{eighths}.falling(position_.position)), kFloor);
	return centred(level_);
}

[[gnu::always_inline]] inline Q<31> Envelope::decay_level(uint32_t num_samples, Q<31> sustain_level, int8_t eighths) {
	smooth_sustain(num_samples, sustain_level);
	level_ = add_wrap(smoothed_sustain_, product(EnvelopeCurve{eighths}.falling(position_.position),
	                                             sub_wrap(kFullScale, smoothed_sustain_)));
	return centred(level_);
}

[[gnu::always_inline]] inline Q<31> Envelope::sustain(uint32_t num_samples, Q<31> sustain_level) {
	smooth_sustain(num_samples, sustain_level);
	level_ = smoothed_sustain_;
	if (sustain_level == Q<31>{}) [[unlikely]] { // nothing to hold: as good as off
		set_state(Stage::OFF);
	}
	else if (ignored_note_off_) [[unlikely]] {
		unconditional_release();
	}
	return centred(level_);
}

[[gnu::always_inline]] inline Q<31> Envelope::release_level(int8_t eighths) {
	level_ = product(EnvelopeCurve{eighths}.falling(position_.position), from_);
	return centred(level_);
}

// A voice renders this every block for each envelope it uses. Each stage is one case: its rate, whether it ends in the
// block, its level. What an end takes is out of line.
[[gnu::always_inline]] inline Q<31> Envelope::run(uint32_t num_samples, const Rates& rates, Q<31> sustain_level,
                                                  EnvelopeEighths eighths) {
	switch (stage_) {
	case Stage::ATTACK:
		if (moves(rates.attack, num_samples)) [[likely]] {
			return attack_level(eighths.attack);
		}
		break;
	case Stage::HOLD:
		if (moves(rates.hold, num_samples)) [[likely]] {
			level_ = kFullScale;
			return centred(level_);
		}
		break;
	case Stage::DECAY:
		if (moves(rates.decay, num_samples)) [[likely]] {
			return decay_level(num_samples, sustain_level, eighths.decay);
		}
		break;
	case Stage::SUSTAIN:
		return sustain(num_samples, sustain_level);
	case Stage::RELEASE:
		if (moves(rates.release, num_samples)) [[likely]] {
			return release_level(eighths.release);
		}
		break;
	case Stage::FAST_RELEASE:
		break;
	default:
		level_ = Q<31>{};
		return kSilent;
	}
	return run_through(num_samples, rates, sustain_level, eighths);
}

Q<31> Envelope::render(uint32_t num_samples, uint32_t attack_rate, uint32_t decay_rate, Q<31> sustain_level,
                       uint32_t release_rate, const deluge::synth::EnvelopeState& state, Q<31> follow_input) {
	const Rates rates{.attack = attack_rate, .hold = 0, .decay = decay_rate, .release = release_rate};
	const EnvelopeEighths eighths = eighths_of(state.curve);
	if (mode_ != EnvelopeMode::ADSR) [[unlikely]] {
		return run_mode(num_samples, rates, sustain_level, eighths, follow_input);
	}
	return run(num_samples, rates, sustain_level, eighths);
}

Q<31> Envelope::render_audio_clip(uint32_t num_samples, uint32_t attack_rate, uint32_t decay_rate, Q<31> sustain_level,
                                  uint32_t release_rate) {
	return run_out_of_line(num_samples,
	                       {.attack = attack_rate, .hold = 0, .decay = decay_rate, .release = release_rate},
	                       sustain_level, deluge::synth::kAudioClipEighths);
}

[[gnu::noinline]] Q<31> Envelope::run_mode(uint32_t num_samples, Rates rates, Q<31> sustain_level,
                                           EnvelopeEighths eighths, Q<31> follow_input) {
	if (mode_ == EnvelopeMode::AHD) {
		// The ADSR params at other jobs: the hold from decay, the decay from release, no sustain
		rates.hold = rates.decay >= kStageEnd / kAhdHoldSpeedup ? kStageEnd : rates.decay * kAhdHoldSpeedup;
		rates.decay = rates.release;
		return run_out_of_line(num_samples, rates, Q<31>{}, eighths);
	}
	if (stage_ < Stage::RELEASE) {
		return follow(num_samples, rates, sustain_level, eighths.decay,
		              mode_ == EnvelopeMode::MPE ? follow_input : Q<31>{});
	}
	return run_out_of_line(num_samples, rates, sustain_level, eighths);
}

uint32_t Envelope::rate_of(Stage stage, const Rates& rates) const {
	switch (stage) {
	case Stage::ATTACK:
		return rates.attack;
	case Stage::HOLD:
		return rates.hold;
	case Stage::DECAY:
		return rates.decay;
	case Stage::RELEASE:
		return rates.release;
	case Stage::FAST_RELEASE:
		return std::max(fast_release_rate_, 2 * rates.release);
	default:
		return 0;
	}
}

[[gnu::noinline]] Q<31> Envelope::run_out_of_line(uint32_t num_samples, const Rates& rates, Q<31> sustain_level,
                                                  EnvelopeEighths eighths) {
	return run(num_samples, rates, sustain_level, eighths);
}

[[gnu::noinline]] Q<31> Envelope::run_through(uint32_t num_samples, const Rates& rates, Q<31> sustain_level,
                                              EnvelopeEighths eighths) {
	if (stage_ == Stage::FAST_RELEASE) {
		fast_release_rate_ = std::max(fast_release_rate_, 2 * rates.release);
	}
	const uint32_t rate = rate_of(stage_, rates);
	if (const std::optional<uint64_t> over = position_.advance(rate, num_samples)) {
		go_on(*over, rate, rates);
	}
	switch (stage_) {
	case Stage::ATTACK:
		return attack_level(eighths.attack);
	case Stage::HOLD:
		level_ = kFullScale;
		return centred(level_);
	case Stage::DECAY:
		return decay_level(num_samples, sustain_level, eighths.decay);
	case Stage::SUSTAIN:
		return sustain(num_samples, sustain_level);
	case Stage::RELEASE:
		return release_level(eighths.release);
	case Stage::FAST_RELEASE: {
		// The Q31 sine from its peak, made unipolar: less high-frequency content than a line
		const Q<31> sine = Q<31>::from_raw(
		    deluge::dsp::fixed::kSineTable.value(position_.position + (kStageEnd >> 1), 24)); // Raw: a Q31 word
		level_ = product(add_wrap(halve_floor(sine), kHalfScale), from_);
		return centred(level_);
	}
	default:
		level_ = Q<31>{};
		return kSilent;
	}
}

[[gnu::noinline]] void Envelope::go_on(uint64_t over, uint32_t rate, const Rates& rates) {
	for (;;) {
		Stage next = Stage::OFF;
		switch (stage_) {
		case Stage::ATTACK:
			next = mode_ == EnvelopeMode::AHD ? Stage::HOLD : Stage::DECAY;
			break;
		case Stage::HOLD:
			next = Stage::DECAY;
			break;
		case Stage::DECAY:
			next = mode_ == EnvelopeMode::AHD ? Stage::OFF : Stage::SUSTAIN;
			break;
		default: // a release
			break;
		}
		set_state(next);
		position_.position = 0;
		if (next == Stage::SUSTAIN || next == Stage::OFF) {
			return;
		}
		const uint32_t next_rate = std::min(rate_of(next, rates), kStageEnd);
		const uint64_t at = carried(over, rate, next_rate);
		if (at < kStageEnd) {
			position_.position = static_cast<uint32_t>(at);
			return;
		}
		over = at - kStageEnd;
		rate = next_rate;
	}
}

Q<31> Envelope::follow(uint32_t num_samples, const Rates& rates, Q<31> sustain_level, int8_t eighths,
                       Q<31> follow_input) {
	// While the note is held it sits in SUSTAIN, whatever the level does, so culling treats it as a held note
	set_state(Stage::SUSTAIN);
	// The input as stored: 0 to 1 for pressure and the mod wheel, -1 to 1 for per-note MPE Y, which below centre pulls
	// the target under the level
	const Q<31> target =
	    std::max(add_sat(sustain_level, product(sub_wrap(kFullScale, sustain_level), follow_input)), Q<31>{});
	// Raw: the gap and the steps, whose products are 64 bits
	const int32_t gap = target.raw() - level_.raw();
	const uint64_t moved = uint64_t{gap > 0 ? rates.attack : rates.release} * num_samples;
	if (eighths > 0) {
		// Each stage-time leaves 2^-k of the gap: so many Q26 octaves this block, a whole 31 leaving none
		const uint64_t octaves = std::min<uint64_t>(moved * static_cast<uint32_t>(eighths), 31ULL << 26);
		const int64_t loss = deluge::dsp::fixed::exp2_loss_q62(static_cast<uint32_t>(octaves));
		// The gap times a Q62 word, as Q62: its high 31 bits' product and its low 31 bits' share
		constexpr int64_t kLow = 0x7FFF'FFFF;
		const int64_t step = (int64_t{gap} * (loss >> 31)) + ((int64_t{gap} * (loss & kLow)) >> 31);
		const int64_t level = (int64_t{level_.raw()} << 31) + level_rest_ + step;
		level_ = Q<31>::from_raw(static_cast<int32_t>(level >> 31));
		level_rest_ = static_cast<int32_t>(level & kLow);
	}
	else { // full scale per stage-time
		const auto step = static_cast<int32_t>(std::min<uint64_t>(moved << 8, kFullScale.raw()));
		level_ = Q<31>::from_raw(level_.raw() + std::clamp(gap, -step, step));
		level_rest_ = 0;
	}
	level_ = std::max(level_, kFloor);
	return centred(level_);
}

Q<31> Envelope::note_on(bool directly_to_decay) {
	return note_on(directly_to_decay, Q<31>{}, {});
}

Q<31> Envelope::note_on(bool directly_to_decay, Q<31> initial_sustain) {
	return note_on(directly_to_decay, initial_sustain, {});
}

Q<31> Envelope::note_on(bool directly_to_decay, Q<31> initial_sustain, const deluge::synth::EnvelopeState& state) {
	mode_ = state.mode;
	curve_ = state.curve;
	const bool ahd = mode_ == EnvelopeMode::AHD;
	smoothed_sustain_ = ahd ? Q<31>{} : initial_sustain;
	ignored_note_off_ = false;
	position_.position = 0;
	level_rest_ = 0;
	// The followers reach their target on the first render even without an attack, whose rate is then huge
	if (!directly_to_decay || mode_ == EnvelopeMode::AR || mode_ == EnvelopeMode::MPE) {
		level_ = Q<31>{};
		set_state(Stage::ATTACK);
	}
	else {
		level_ = kFullScale;
		set_state(ahd ? Stage::HOLD : Stage::DECAY);
	}
	return centred(level_);
}

void Envelope::note_off(bool has_sustain) {
	if (mode_ == EnvelopeMode::AHD) {
		has_sustain = false; // a one-shot
	}
	else if (mode_ != EnvelopeMode::ADSR) {
		has_sustain = true; // held while the note is
	}
	if (!has_sustain) {
		ignored_note_off_ = true;
	}
	else if (stage_ < Stage::RELEASE) {
		unconditional_release();
	}
}

void Envelope::set_state(Stage new_stage) {
	if (stage_ != new_stage) {
		stage_ = new_stage;
		stage_changed_ = true;
	}
}

void Envelope::unconditional_off() {
	set_state(Stage::OFF);
}

void Envelope::unconditional_release(Stage release_stage, uint32_t fast_release_rate) {
	fast_release_rate_ = fast_release_rate;
	if (stage_ != release_stage) {
		set_state(release_stage);
		position_.position = 0;
		from_ = level_;
	}
}

void Envelope::resume_attack(Q<31> resume_level) {
	if (stage_ != Stage::ATTACK) {
		return;
	}
	level_ = resume_level; // the followers carry on from it
	if (mode_ == EnvelopeMode::ADSR || mode_ == EnvelopeMode::AHD) {
		// Short of the end, so that the attack is what resumes
		position_.position =
		    std::min(EnvelopeCurve{eighths_of(curve_).attack}.position_of(resume_level), kStageEnd - 1);
	}
}
