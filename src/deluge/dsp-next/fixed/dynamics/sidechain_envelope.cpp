/*
 * Copyright © 2014-2026 Synthstrom Audible Limited
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

#include "dsp-next/fixed/dynamics/sidechain_envelope.h"

#include "dsp-next/fixed/interpolation/table_interpolation.h"

namespace deluge::dsp::fixed {

void SidechainEnvelope::registerHit(int32_t strength) {
	pending_hit_strength_ = combine_hit_strengths(pending_hit_strength_, strength);
}

void SidechainEnvelope::registerHitRetrospectively(int32_t strength, uint32_t samples_ago, SidechainRates rates) {
	pending_hit_strength_ = 0;
	offset_ = ONE_Q31 - strength;
	// The duck starts from no duck at all: the level the hit fell from is long gone.
	height_ = ONE_Q31 - offset_;

	const auto attack = static_cast<uint32_t>(rates.attack);
	const uint32_t attack_length = kStageLength / attack;
	if (samples_ago < attack_length) {
		pos_ = samples_ago * attack;
		stage_ = Stage::ATTACK;
		return;
	}

	const uint32_t since_release = samples_ago - attack_length;
	const auto release = static_cast<uint32_t>(rates.release);
	const uint32_t release_length = kStageLength / release;
	if (since_release < release_length) {
		pos_ = since_release * release;
		stage_ = Stage::RELEASE;
		return;
	}

	stage_ = Stage::OFF;
}

void SidechainEnvelope::transition(Stage next) {
	stage_ = next;
	switch (next) {
	case Stage::ATTACK:
		height_ = last_value_ - offset_;
		pos_ = 0;
		break;
	case Stage::RELEASE:
		pos_ = 0;
		height_ = ONE_Q31 - offset_;
		break;
	case Stage::OFF:
		break;
	}
}

int32_t SidechainEnvelope::attackLevel() const {
	// Falls slowly at first, then quickly: the duck squeezes in rather than snapping.
	return (multiply_32x32_rshift32(height_, ONE_Q31 - getDecay4(kStageLength - pos_, 23)) << 1) + offset_;
}

int32_t SidechainEnvelope::releaseLevel(int32_t shape) const {
	const uint32_t positive_shape = static_cast<uint32_t>(shape) + 2147483648u;

	// The curve straightens as the release goes on, sooner the lower the shape; a straight line once it runs out.
	int32_t curvedness16 = static_cast<int32_t>((positive_shape >> 15) - (pos_ >> 7));
	int32_t pre_value = 0;
	if (curvedness16 < 0) {
		pre_value = static_cast<int32_t>(pos_ << 8);
	}
	else {
		curvedness16 = std::min(curvedness16, 65536);
		const int32_t straightness = 65536 - curvedness16;
		pre_value =
		    straightness * static_cast<int32_t>(pos_ >> 8) + (getDecay8(kStageLength - pos_, 23) >> 16) * curvedness16;
	}

	return ONE_Q31 - height_ + (multiply_32x32_rshift32(pre_value, height_) << 1);
}

int32_t SidechainEnvelope::render(uint32_t num_samples, int32_t shape, SidechainRates rates) {
	if (pending_hit_strength_ != 0) {
		const int32_t new_offset = ONE_Q31 - pending_hit_strength_;
		pending_hit_strength_ = 0;

		// A hit dipping less than the duck already under way changes nothing.
		if (new_offset < last_value_) {
			offset_ = new_offset;
			transition(rates.instant_attack ? Stage::RELEASE : Stage::ATTACK);
		}
	}

	if (stage_ == Stage::ATTACK) {
		pos_ += num_samples * static_cast<uint32_t>(rates.attack);
		if (pos_ < kStageLength) {
			last_value_ = attackLevel();
			return last_value_ - ONE_Q31;
		}
		transition(Stage::RELEASE);
	}

	if (stage_ == Stage::RELEASE) {
		pos_ += num_samples * static_cast<uint32_t>(rates.release);
		if (pos_ < kStageLength) {
			last_value_ = releaseLevel(shape);
			return last_value_ - ONE_Q31;
		}
		transition(Stage::OFF);
	}

	last_value_ = ONE_Q31;
	return last_value_ - ONE_Q31;
}

} // namespace deluge::dsp::fixed
