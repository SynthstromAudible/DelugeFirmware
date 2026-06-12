/*
 * Copyright © 2018-2023 Synthstrom Audible Limited
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

#include "storage/multi_range/multi_wave_table_range.h"
#include "storage/multi_range/multisample_range.h"
#include "util/containers.h"
#include <algorithm>
#include <variant>

#ifndef GREATER_OR_EQUAL
#define GREATER_OR_EQUAL 0
#define LESS (-1)
#endif

/// A Source's key ranges, sorted ascending by topNote. At any one time all elements are the same alternative
/// (sample or wavetable) - changeType() converts the lot, which mirrors the old implementation where the whole
/// array's element size was switched.
class MultiRangeArray final {
public:
	using RangeVariant = std::variant<MultisampleRange, MultiWaveTableRange>;

	static MultiRange* variantToRange(RangeVariant& variant) {
		return std::visit([](auto& range) -> MultiRange* { return &range; }, variant);
	}

	[[nodiscard]] int32_t getNumElements() const { return static_cast<int32_t>(ranges_.size()); }
	MultiRange* getElement(int32_t i) { return variantToRange(ranges_[i]); }

	/// The size of the elements currently stored, as a stand-in for a type tag (mirrors the old elementSize field)
	[[nodiscard]] int32_t currentElementSize() const {
		return holdsWaveTableRanges_ ? sizeof(MultiWaveTableRange) : sizeof(MultisampleRange);
	}

	[[nodiscard]] int32_t search(int32_t key, int32_t comparison, int32_t rangeBegin = 0) const {
		auto it = std::lower_bound(
		    ranges_.begin() + rangeBegin, ranges_.end(), key, [](RangeVariant const& variant, int32_t topNote) {
			    return std::visit([](auto const& r) { return (int32_t)r.topNote; }, variant) < topNote;
		    });
		return static_cast<int32_t>(it - ranges_.begin()) + comparison;
	}

	/// Inserts a new, empty range of the current type at index i. Returns nullptr on OOM.
	MultiRange* insertMultiRange(int32_t i) {
		try {
			if (holdsWaveTableRanges_) {
				ranges_.emplace(ranges_.begin() + i, std::in_place_type<MultiWaveTableRange>);
			}
			else {
				ranges_.emplace(ranges_.begin() + i, std::in_place_type<MultisampleRange>);
			}
		} catch (deluge::exception&) {
			return nullptr;
		}
		return getElement(i);
	}

	/// Inserts a pre-built range at index i. It must match the current type.
	Error insertVariant(int32_t i, RangeVariant&& variant) {
		try {
			ranges_.insert(ranges_.begin() + i, std::move(variant));
		} catch (deluge::exception&) {
			return Error::INSUFFICIENT_RAM;
		}
		return Error::NONE;
	}

	/// Converts all elements to the type indicated by newSize (a sizeof token, like the old API), preserving
	/// their topNotes only.
	Error changeType(int32_t newSize) {
		bool toWaveTable = (newSize == sizeof(MultiWaveTableRange));
		if (ranges_.empty()) {
			holdsWaveTableRanges_ = toWaveTable;
			return Error::NONE;
		}

		deluge::fast_vector<RangeVariant> newRanges;
		try {
			newRanges.reserve(ranges_.size());
			for (RangeVariant& old : ranges_) {
				int16_t topNote = variantToRange(old)->topNote;
				if (toWaveTable) {
					newRanges.emplace_back(std::in_place_type<MultiWaveTableRange>);
				}
				else {
					newRanges.emplace_back(std::in_place_type<MultisampleRange>);
				}
				variantToRange(newRanges.back())->topNote = topNote;
			}
		} catch (deluge::exception&) {
			return Error::INSUFFICIENT_RAM;
		}

		ranges_ = std::move(newRanges);
		holdsWaveTableRanges_ = toWaveTable;
		return Error::NONE;
	}

	void deleteAtIndex(int32_t i, int32_t numToDelete = 1) {
		ranges_.erase(ranges_.begin() + i, ranges_.begin() + i + numToDelete);
	}

	[[nodiscard]] size_t size() const { return ranges_.size(); }
	[[nodiscard]] bool empty() const { return ranges_.empty(); }
	void clear() { ranges_.clear(); }

	bool ensureEnoughSpaceAllocated(int32_t numAdditionalElementsNeeded) {
		try {
			ranges_.reserve(ranges_.size() + numAdditionalElementsNeeded);
			return true;
		} catch (deluge::exception&) {
			return false;
		}
	}

private:
	deluge::fast_vector<RangeVariant> ranges_;
	bool holdsWaveTableRanges_ = false;
};
