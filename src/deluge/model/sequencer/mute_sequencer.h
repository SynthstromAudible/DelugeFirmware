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

#pragma once

#include <algorithm>
#include <cstdint>

/// Mutes a repeating number of sequenced steps before they reach the arpeggiator.
///
/// A step is everything that starts at one position of the sequence, so a chord is muted or played as a whole. The
/// pattern is `steps` long: its first `mutes` steps are muted and the rest play. `offset` shifts the pattern to the
/// left, so with steps=5, mutes=2 and offset=1 the clip starts with 1 muted step, then 3 played, then 2 muted...
///
/// The pattern starts over whenever the clip is launched, and from then on runs endlessly, regardless of the clip
/// length.
class MuteSequencer {
public:
	static constexpr int32_t kMaxSteps = 50;

	[[nodiscard]] bool isEnabled() const { return steps_ > 0; }
	[[nodiscard]] int32_t getSteps() const { return steps_; }
	[[nodiscard]] int32_t getMutes() const { return mutes_; }
	[[nodiscard]] int32_t getOffset() const { return offset_; }
	[[nodiscard]] int32_t getMaxOffset() const { return std::max<int32_t>(steps_ - 1, 0); }

	/// 0 switches the sequencer off. Mutes and offset get clamped to fit the new length, but are kept while off.
	void setSteps(int32_t steps) {
		steps_ = std::clamp<int32_t>(steps, 0, kMaxSteps);
		if (steps_ > 0) {
			mutes_ = std::min<int32_t>(mutes_, steps_);
			offset_ = std::min<int32_t>(offset_, getMaxOffset());
		}
	}
	void setMutes(int32_t mutes) { mutes_ = std::clamp<int32_t>(mutes, 0, steps_); }
	void setOffset(int32_t offset) { offset_ = std::clamp<int32_t>(offset, 0, getMaxOffset()); }

	void copySettingsFrom(MuteSequencer const& other) {
		steps_ = other.steps_;
		mutes_ = other.mutes_;
		offset_ = other.offset_;
	}

	/// Go back to the first step of the pattern
	void restart() { position_ = 0; }

	/// Carry on the pattern from wherever another sequencer is up to
	void continueFrom(MuteSequencer const& other) { position_ = other.position_; }

	/// Consume the next step of the pattern, returning whether it is muted
	bool advance() {
		if (!isEnabled()) {
			return false;
		}
		bool muted = isStepMuted(position_);
		position_ = (position_ + 1) % steps_;
		return muted;
	}

	/// Whether the step at this index (counted from the clip launch) is muted
	[[nodiscard]] bool isStepMuted(uint32_t step) const {
		if (!isEnabled()) {
			return false;
		}
		return (step + offset_) % steps_ < static_cast<uint32_t>(mutes_);
	}

private:
	uint8_t steps_{0};
	uint8_t mutes_{0};
	uint8_t offset_{0};
	uint32_t position_{0};
};
