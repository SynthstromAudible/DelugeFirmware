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
#include "libdeluge/system.h"
#include "util/containers.h"
#include "util/structural_edit_probe.h"
#include <algorithm>
#include <expected>
#include <utility>

class Clip;

/// The ordered list of Clips in a Song section. Legacy method names are kept from the old
/// ResizeablePointerArray-based implementation; modernise call sites incrementally.
class ClipArray final {
public:
	ClipArray() = default;

	Clip** getElementAddress(int32_t index) { return &clips_[index]; }

	std::expected<void, Error> insertClipAt(Clip* clip, int32_t index) {
		AudioExcludedSection section; // the render walks this container: see noteEdit()
		noteEdit();
		try {
			clips_.insert(clips_.begin() + index, clip);
		} catch (deluge::exception&) {
			return std::unexpected(Error::INSUFFICIENT_RAM);
		}
		return {};
	}

	std::expected<void, Error> reserveExtra(int32_t numAdditionalElementsNeeded) {
		AudioExcludedSection section; // the render walks this container: see noteEdit()
		noteEdit();
		try {
			clips_.reserve(clips_.size() + numAdditionalElementsNeeded);
		} catch (deluge::exception&) {
			return std::unexpected(Error::INSUFFICIENT_RAM);
		}
		return {};
	}

	auto erase(deluge::fast_vector<Clip*>::iterator first, deluge::fast_vector<Clip*>::iterator last) {
		AudioExcludedSection section; // the render walks this container: see noteEdit()
		noteEdit();
		return clips_.erase(first, last);
	}
	auto erase(deluge::fast_vector<Clip*>::iterator at) {
		AudioExcludedSection section; // the render walks this container: see noteEdit()
		noteEdit();
		return clips_.erase(at);
	}

	int32_t getIndexForClip(Clip* clip) const {
		auto it = std::ranges::find(clips_, clip);
		return (it != clips_.end()) ? static_cast<int32_t>(it - clips_.begin()) : -1;
	}

	// Standard container surface
	[[nodiscard]] size_t size() const { return clips_.size(); }
	[[nodiscard]] bool empty() const { return clips_.empty(); }
	/// Swap two clips in place (a session-view clip drag).
	void swapClips(int32_t a, int32_t b) {
		AudioExcludedSection section; // the render walks this container: see noteEdit()
		noteEdit();
		std::swap(clips_[a], clips_[b]);
	}

	void clear() {
		AudioExcludedSection section; // the render walks this container: see noteEdit()
		noteEdit();
		clips_.clear();
	}
	Clip*& operator[](size_t i) { return clips_[i]; }
	Clip* const& operator[](size_t i) const { return clips_[i]; }
	auto begin() { return clips_.begin(); }
	auto end() { return clips_.end(); }
	auto begin() const { return clips_.begin(); }
	auto end() const { return clips_.end(); }

private:
	/// Every structural edit runs inside an AudioExcludedSection (libdeluge/system.h): the render walks a song's clips
	/// and, on the device, preempts thread mode at any instruction. Each edit also reports to the probe
	/// (util/structural_edit_probe.h), which counts any made outside a section.
	static void noteEdit() { deluge::noteStructuralEdit(deluge::StructuralContainer::CLIP_ARRAY); }

	deluge::fast_vector<Clip*> clips_{};
};
