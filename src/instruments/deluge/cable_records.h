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
#pragma once

#include "synth/modulation/cable.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace deluge::instrument {

using PatchSource = synth::PatchSource;
using synth::CablePolarity;
using synth::CableRecord;
using synth::CableTarget;

/// @brief One clip's cables, in the order the routing compile leaves them (the order a save walks): the instrument's
/// patching state.
class CableRecords {
public:
	[[nodiscard]] std::span<const CableRecord> records() const { return {records_.data(), size_}; }
	[[nodiscard]] size_t size() const { return size_; }
	[[nodiscard]] bool empty() const { return size_ == 0; }
	[[nodiscard]] const CableRecord& operator[](size_t i) const { return records_[i]; }

	/// @brief Where the cable from @p source to @p target is.
	/// @return Its index, or nullopt when there is none.
	[[nodiscard]] std::optional<size_t> find(PatchSource source, CableTarget target) const {
		for (size_t i = 0; i < size_; ++i) {
			if (records_[i].source == source && records_[i].target == target) {
				return i;
			}
		}
		return std::nullopt;
	}

	/// @brief Add @p record at the end.
	/// @return False when the records are full (synth::kMaxCables).
	bool append(const CableRecord& record) {
		if (size_ >= records_.size()) {
			return false;
		}
		records_[size_++] = record;
		return true;
	}

	void set_polarity(size_t i, CablePolarity polarity) { records_[i].polarity = polarity; }
	void set_target(size_t i, CableTarget target) { records_[i].target = target; }

	/// @brief Remove record @p i by moving the last record into its place.
	void erase_by_moving_last(size_t i) {
		records_[i] = records_[size_ - 1];
		--size_;
	}

	void truncate(size_t count) { size_ = std::min(size_, count); }

	/// @brief Reorder: position c takes the record previously at @p order[c].
	/// @param order A permutation of [0, size()).
	void permute(std::span<const uint8_t> order) {
		const std::array<CableRecord, synth::kMaxCables> previous = records_;
		for (size_t c = 0; c < order.size(); ++c) {
			records_[c] = previous[order[c]];
		}
	}

	[[nodiscard]] bool operator==(const CableRecords& other) const {
		return std::ranges::equal(records(), other.records());
	}

private:
	std::array<CableRecord, synth::kMaxCables> records_{};
	size_t size_{};
};

} // namespace deluge::instrument
