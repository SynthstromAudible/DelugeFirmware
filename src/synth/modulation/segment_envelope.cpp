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

#include "synth/modulation/segment_envelope.h"

#include <algorithm>
#include <cmath>
#include <limits>

using deluge::dsp::Q;
using deluge::dsp::fp::CurveShape;
using deluge::synth::EnvelopeMode;
using E = deluge::dsp::fp::Envelope;

namespace {

constexpr Q<31> kCentre = deluge::dsp::q<31>(0.5); ///< The output's zero-centre.
constexpr Q<31> kSilent = Q<31>::min();            ///< The centred output of a silent envelope.
constexpr float kFloor = 0x1p-31f; ///< The least level an attack or a held AR or MPE note gives: not silent
/// The longest stage, in samples: a rate of 0 never ends within a song, and the count stays exact in float.
constexpr float kLongest = 16777216.f;

/// A level's Q31 word, 0 to full scale, truncated as main's conversion truncates.
[[nodiscard]] Q<31> to_q31(float level) {
	if (level >= 1.f) {
		return Q<31>::max();
	}
	// Raw: the level scaled to the word, truncated
	return Q<31>::from_raw(static_cast<int32_t>(std::max(level, 0.f) * 2147483648.f));
}
/// A Q31 word as a level: its value.
[[nodiscard]] float to_float(Q<31> word) {
	// Raw: the word over 2^31
	return static_cast<float>(word.raw()) * 0x1p-31f;
}
/// A level, 0 to 1, centred about 0 as the patch source reads it: less a half, doubled.
[[nodiscard]] Q<31> centred(float level) {
	return scale_shl_wrap<1>(sub_wrap(to_q31(level), kCentre));
}
/// The samples a stage lasts at @p rate position steps a sample.
[[nodiscard]] float length_of(uint32_t rate) {
	if (rate == 0) {
		return kLongest;
	}
	return std::min(static_cast<float>(SegmentEnvelope::kStageEnd) / static_cast<float>(rate), kLongest);
}
[[nodiscard]] bool follows(EnvelopeMode mode) {
	return mode == EnvelopeMode::AR || mode == EnvelopeMode::MPE;
}

} // namespace

Q<31> SegmentEnvelope::render(uint32_t num_samples, uint32_t attack_rate, uint32_t decay_rate, Q<31> sustain_level,
                              uint32_t release_rate, const deluge::synth::EnvelopeState& state, Q<31> follow_input) {
	return render_shaped(num_samples, attack_rate, decay_rate, sustain_level, release_rate,
	                     CurveShape::from_curve(state.curve), follow_input);
}

Q<31> SegmentEnvelope::render_audio_clip(uint32_t num_samples, uint32_t attack_rate, uint32_t decay_rate,
                                         Q<31> sustain_level, uint32_t release_rate) {
	return render_shaped(num_samples, attack_rate, decay_rate, sustain_level, release_rate,
	                     deluge::dsp::fp::kAudioClipShape, Q<31>{});
}

Q<31> SegmentEnvelope::render_shaped(uint32_t num_samples, uint32_t attack_rate, uint32_t decay_rate,
                                     Q<31> sustain_level, uint32_t release_rate, const CurveShape& shape,
                                     Q<31> follow_input) {
	shape_ = shape;
	// AHD reuses the ADSR params: the hold from decay, the decay from release, no sustain
	uint32_t hold_rate = kStageEnd; // a hold of one sample, unused outside AHD
	if (mode_ == EnvelopeMode::AHD) {
		hold_rate = (decay_rate >= kStageEnd / kAhdHoldSpeedup) ? kStageEnd : decay_rate * kAhdHoldSpeedup;
		decay_rate = release_rate;
		sustain_level = Q<31>{};
	}
	else if (follows(mode_) && stage_ < Stage::RELEASE) {
		return follow(num_samples, attack_rate, sustain_level, release_rate, shape,
		              mode_ == EnvelopeMode::MPE ? follow_input : Q<31>{});
	}
	if (stage_ == Stage::OFF) {
		return kSilent;
	}
	if (stage_ == Stage::FAST_RELEASE && fast_release_rate_ < 2 * release_rate) {
		fast_release_rate_ = 2 * release_rate;
	}

	// Every stage's length and curve from this block's params, so a stage entered within the block runs at them
	auto set = [&](Segment s, uint32_t rate, float k) {
		slot(s).length = length_of(rate);
		slot(s).curve = k;
	};
	set(Segment::ATTACK, attack_rate, shape.attack.k);
	set(Segment::HOLD, hold_rate, 0.f);
	set(Segment::DECAY, decay_rate, shape.decay.k);
	set(Segment::RELEASE, release_rate, shape.release.k);
	set(Segment::FAST_RELEASE, fast_release_rate_, 0.f);
	// and the stage under way carried on at them from where it is
	if (segment_.index >= 0.f) {
		const auto& g = program_.segments[static_cast<size_t>(segment_.index)];
		E::retime(segment_, g.length);
		segment_.curve = g.curve;
	}

	if (stage_ != Stage::SUSTAIN) {
		(void)E::advance_block(program_, segment_, num_samples);
	}

	Stage reached = Stage::OFF;
	if (segment_.index >= 0.f) {
		reached = static_cast<Stage>(static_cast<uint8_t>(segment_.index));
	}
	if (reached == Stage::DECAY || reached == Stage::SUSTAIN) {
		// Smoothed toward its target: modulating sustain would otherwise jump downstream, since params don't smooth the
		// envelope and envelopes render before the other params, their own sustain included
		const float amount = std::min(static_cast<float>(num_samples) * (1.f / 512.f), 1.f);
		smoothed_sustain_ += (to_float(sustain_level) - smoothed_sustain_) * amount;
	}

	switch (reached) {
	case Stage::ATTACK:
		level_ = std::max(segment_.value, kFloor);
		break;
	case Stage::HOLD:
		level_ = 1.f;
		break;
	case Stage::DECAY:
		level_ = smoothed_sustain_ + (segment_.value * (1.f - smoothed_sustain_));
		break;
	case Stage::SUSTAIN:
		level_ = smoothed_sustain_;
		set_state(Stage::SUSTAIN);
		if (sustain_level == Q<31>{}) { // nothing to hold: as good as off
			set_state(Stage::OFF);
		}
		else if (ignored_note_off_) {
			unconditional_release();
		}
		return centred(level_);
	case Stage::RELEASE:
	case Stage::FAST_RELEASE:
		level_ = segment_.value;
		break;
	default: // a release, or AHD's decay, has ended
		level_ = 0.f;
		set_state(Stage::OFF);
		return kSilent;
	}
	set_state(reached);
	return centred(level_);
}

Q<31> SegmentEnvelope::follow(uint32_t num_samples, uint32_t attack_rate, Q<31> sustain_level, uint32_t release_rate,
                              const CurveShape& shape, Q<31> follow_input) {
	// While the note is held it sits in SUSTAIN, whatever the level does, so culling treats it as a held note
	set_state(Stage::SUSTAIN);
	// The input as stored: 0 to 1 for pressure and the mod wheel, -1 to 1 for per-note MPE Y, which below centre pulls
	// the target under the level
	const float level = to_float(sustain_level);
	const float target = std::clamp(level + ((1.f - level) * to_float(follow_input)), 0.f, 1.f);
	// How far through a full-scale stage this block goes, at the rate for the direction it goes
	const uint32_t rate = target > level_ ? attack_rate : release_rate;
	const float distance = static_cast<float>(rate) * static_cast<float>(num_samples) / static_cast<float>(kStageEnd);
	const float k = shape.decay.k;
	if (k > 0.f) {
		level_ = target + ((level_ - target) * std::exp2(-k * distance)); // each stage-time leaves 2^-k of the gap
	}
	else {
		level_ += std::clamp(target - level_, -distance, distance); // full scale per stage-time
	}
	level_ = std::max(level_, kFloor); // a voice is freed at exactly 0, which a held level must not mean
	segment_.value = level_;
	return centred(level_);
}

Q<31> SegmentEnvelope::note_on(bool directly_to_decay) {
	return note_on(directly_to_decay, Q<31>{}, {});
}

Q<31> SegmentEnvelope::note_on(bool directly_to_decay, Q<31> initial_sustain) {
	return note_on(directly_to_decay, initial_sustain, {});
}

Q<31> SegmentEnvelope::note_on(bool directly_to_decay, Q<31> initial_sustain,
                               const deluge::synth::EnvelopeState& state) {
	mode_ = state.mode;
	shape_ = CurveShape::from_curve(state.curve);
	smoothed_sustain_ = mode_ == EnvelopeMode::AHD ? 0.f : to_float(initial_sustain);
	ignored_note_off_ = false;
	level_ = 0.f;
	segment_ = E::State{};
	build_program();
	// The followers reach their target on the first render even without an attack, whose rate is then huge
	if (!directly_to_decay || follows(mode_)) {
		enter(Segment::ATTACK);
		set_state(Stage::ATTACK);
	}
	else {
		level_ = 1.f;
		segment_.value = 1.f;
		const bool ahd = mode_ == EnvelopeMode::AHD;
		enter(ahd ? Segment::HOLD : Segment::DECAY);
		set_state(ahd ? Stage::HOLD : Stage::DECAY);
	}
	return centred(level_);
}

void SegmentEnvelope::build_program() {
	using deluge::dsp::fp::kOff;
	namespace seg = deluge::dsp::fp::detail;
	const bool ahd = mode_ == EnvelopeMode::AHD;
	// Lengths and curves are set every render; these give each segment its kind, target and successor
	program_ = {};
	auto at = [](Segment s) { return static_cast<float>(s); };
	slot(Segment::ATTACK) = seg::curve_segment(1.f, 1.f, 0.f, at(ahd ? Segment::HOLD : Segment::DECAY));
	slot(Segment::HOLD) = seg::curve_segment(1.f, 1.f, 0.f, at(Segment::DECAY));
	slot(Segment::DECAY) = seg::curve_segment(0.f, 1.f, 0.f, ahd ? kOff : at(Segment::SUSTAIN));
	slot(Segment::SUSTAIN) = seg::hold_segment(0.f);
	slot(Segment::RELEASE) = seg::curve_segment(0.f, 1.f, 0.f, kOff);
	slot(Segment::FAST_RELEASE) = seg::cosine_segment(0.f, 1.f, kOff);
	program_.release = kOff;
}

void SegmentEnvelope::enter(Segment s) {
	E::enter(program_, segment_, static_cast<float>(s), false);
}

void SegmentEnvelope::note_off(bool has_sustain) {
	if (mode_ == EnvelopeMode::AHD) {
		has_sustain = false; // a one-shot
	}
	else if (follows(mode_)) {
		has_sustain = true; // held while the note is
	}
	if (!has_sustain) {
		ignored_note_off_ = true;
	}
	else if (stage_ < Stage::RELEASE) {
		unconditional_release();
	}
}

void SegmentEnvelope::set_state(Stage new_stage) {
	if (stage_ != new_stage) {
		stage_ = new_stage;
		stage_changed_ = true;
	}
}

void SegmentEnvelope::unconditional_off() {
	set_state(Stage::OFF);
}

void SegmentEnvelope::unconditional_release(Stage release_stage, uint32_t fast_release_rate) {
	fast_release_rate_ = fast_release_rate;
	if (stage_ != release_stage) {
		set_state(release_stage);
		segment_.value = level_; // the release runs from the level, which the decay's unit curve is not
		enter(release_stage == Stage::FAST_RELEASE ? Segment::FAST_RELEASE : Segment::RELEASE);
	}
}

void SegmentEnvelope::resume_attack(Q<31> resume_level) {
	if (stage_ != Stage::ATTACK) {
		return;
	}
	const float v = to_float(resume_level);
	if (!follows(mode_)) {
		// Where the attack's curve reaches the level: the fraction elapsed, which the next render's retime keeps
		segment_.elapsed = shape_.attack.rising_inverse(v) * segment_.length;
	}
	level_ = v; // the followers carry on from it
	segment_.value = v;
}

Q<31> SegmentEnvelope::output_level() const {
	return to_q31(level_);
}

void SegmentEnvelope::set_output_level(Q<31> level) {
	level_ = to_float(level);
	segment_.value = level_;
}
