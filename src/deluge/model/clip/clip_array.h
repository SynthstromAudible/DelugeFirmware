/*
 * Copyright © 2019-2023 Synthstrom Audible Limited
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

#include "definitions_cxx.hpp"
#include "util/containers.h"

class Clip;

/// The ordered list of Clips in a Song section. Legacy method names are kept from the old
/// ResizeablePointerArray-based implementation; modernise call sites incrementally.
class ClipArray final {
public:
	ClipArray() = default;

	[[nodiscard]] int32_t getNumElements() const { return static_cast<int32_t>(clips_.size()); }
	[[nodiscard]] Clip* getClipAtIndex(int32_t index) const { return clips_[index]; }
	Clip** getElementAddress(int32_t index) { return &clips_[index]; }
	void setPointerAtIndex(Clip* clip, int32_t index) { clips_[index] = clip; }

	Error insertClipAtIndex(Clip* clip, int32_t index) {
		try {
			clips_.insert(clips_.begin() + index, clip);
		} catch (deluge::exception&) {
			return Error::INSUFFICIENT_RAM;
		}
		return Error::NONE;
	}

	void deleteAtIndex(int32_t i, int32_t numToDelete = 1) {
		clips_.erase(clips_.begin() + i, clips_.begin() + i + numToDelete);
	}

	int32_t getIndexForClip(Clip* clip) const {
		for (int32_t c = 0; c < getNumElements(); c++) {
			if (clips_[c] == clip) {
				return c;
			}
		}
		return -1;
	}

	bool ensureEnoughSpaceAllocated(int32_t numAdditionalElementsNeeded) {
		try {
			clips_.reserve(clips_.size() + numAdditionalElementsNeeded);
			return true;
		} catch (deluge::exception&) {
			return false;
		}
	}

	void swapElements(int32_t i1, int32_t i2) { std::swap(clips_[i1], clips_[i2]); }

	void empty() { clips_.clear(); }

private:
	deluge::fast_vector<Clip*> clips_;
};
