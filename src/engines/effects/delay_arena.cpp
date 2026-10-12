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

#include "engines/effects/delay_arena.h"

#include <algorithm>

namespace deluge::effects {

DelayArena::DelayArena(DelayArenaMemory memory, std::size_t region_bytes)
    : memory_{memory}, region_bytes_{region_bytes} {
}

DelayArena::~DelayArena() {
	for (std::size_t i = 0; i < count_; i++) {
		memory_.unlend(regions_[i].base);
	}
}

dsp::StereoSample* DelayArena::take(std::size_t frames) {
	const std::size_t bytes = frames * sizeof(dsp::StereoSample);
	for (std::size_t i = 0; i < count_; i++) {
		Region& region = regions_[i];
		if (void* block = memory_.heap_alloc(region.heap, bytes); block != nullptr) {
			region.live++;
			region.used_bytes += bytes + kTapeOverheadBytes;
			if (!margin_by_count()) {
				margin_ok_.store(false, std::memory_order_relaxed);
				memory_.request_maintenance();
			}
			return static_cast<dsp::StereoSample*>(block);
		}
	}
	shortfalls_.fetch_add(1, std::memory_order_relaxed);
	margin_ok_.store(false, std::memory_order_relaxed);
	memory_.request_maintenance();
	return nullptr;
}

void DelayArena::give_back(dsp::StereoSample* storage, std::size_t frames) {
	const std::size_t i = region_of(storage);
	if (i == count_) {
		return;
	}
	Region& region = regions_[i];
	memory_.heap_free(region.heap, storage);
	region.live--;
	region.used_bytes -= std::min(region.used_bytes, (frames * sizeof(dsp::StereoSample)) + kTapeOverheadBytes);
	if (margin_by_count()) {
		margin_ok_.store(true, std::memory_order_relaxed);
	}
}

ArenaChange DelayArena::prepare(int on_delta) {
	ArenaChange change{.on_delta = on_delta};
	if (on_delta >= 0 && on_ + on_delta > 0 && !margin_ok_.load(std::memory_order_relaxed)
	    && count_ < kArenaMaxRegions) {
		change.lent = memory_.lend(region_bytes_);
	}
	return change;
}

void DelayArena::commit(ArenaChange& change) {
	on_ = std::max(0, on_ + change.on_delta);
	if (change.lent != nullptr && on_ > 0 && !holds_margin() && attach(change.lent)) {
		change.lent = nullptr;
	}
	// Regions are returned only as owners leave: a delay ringing out keeps its region for its next start.
	for (std::size_t i = count_; change.on_delta < 0 && i-- > 0;) {
		if (regions_[i].live != 0 || (on_ > 0 && !holds_margin_without(i))) {
			continue;
		}
		change.returned[change.returned_count++] = regions_[i].base;
		std::shift_left(regions_.begin() + static_cast<std::ptrdiff_t>(i),
		                regions_.begin() + static_cast<std::ptrdiff_t>(count_), 1);
		count_--;
		regions_[count_] = Region{};
	}
	margin_ok_.store(on_ > 0 && holds_margin(), std::memory_order_relaxed);
}

void DelayArena::finish(ArenaChange& change) {
	if (change.lent != nullptr) {
		memory_.unlend(change.lent);
		change.lent = nullptr;
	}
	for (std::size_t i = 0; i < change.returned_count; i++) {
		memory_.unlend(change.returned[i]);
	}
	change.returned_count = 0;
}

bool DelayArena::holds_margin() {
	return holds_margin_without(kArenaMaxRegions);
}

std::size_t DelayArena::free_bytes_without(std::size_t skip) const {
	std::size_t free_bytes = 0;
	for (std::size_t i = 0; i < count_; i++) {
		if (i != skip) {
			free_bytes += region_bytes_ - std::min(region_bytes_, regions_[i].used_bytes);
		}
	}
	return free_bytes;
}

std::size_t DelayArena::margin_bytes() const {
	return (kMarginMaxTapes * kMaxTapeBytes)
	       + (static_cast<std::size_t>(on_) * (kNeutralTapeBytes + kTapeOverheadBytes));
}

bool DelayArena::margin_by_count() const {
	if (on_ <= 0 || free_bytes_without(kArenaMaxRegions) < margin_bytes()) {
		return false;
	}
	for (std::size_t i = 0; i < count_; i++) {
		if (regions_[i].live == 0) {
			return true;
		}
	}
	return false;
}

bool DelayArena::holds_margin_without(std::size_t skip) {
	if (free_bytes_without(skip) < margin_bytes()) {
		return false;
	}
	// The largest tapes need contiguous room: take them for a moment, each from the first region that has it.
	std::array<std::pair<void*, void*>, kMarginMaxTapes> trial{};
	bool fits = true;
	for (auto& [heap, block] : trial) {
		for (std::size_t i = 0; i < count_ && block == nullptr; i++) {
			if (i != skip) {
				heap = regions_[i].heap;
				block = memory_.heap_alloc(heap, kMaxTapeBytes);
			}
		}
		fits = fits && block != nullptr;
	}
	for (auto& [heap, block] : trial) {
		if (block != nullptr) {
			memory_.heap_free(heap, block);
		}
	}
	return fits;
}

std::size_t DelayArena::region_of(const void* storage) const {
	const auto* at = static_cast<const std::byte*>(storage);
	for (std::size_t i = 0; i < count_; i++) {
		if (at >= regions_[i].base && at < regions_[i].base + region_bytes_) {
			return i;
		}
	}
	return count_;
}

bool DelayArena::attach(void* region) {
	if (count_ == kArenaMaxRegions) {
		return false;
	}
	void* heap = memory_.create_heap(region, region_bytes_);
	if (heap == nullptr) {
		return false;
	}
	regions_[count_++] = Region{.base = static_cast<std::byte*>(region), .heap = heap};
	return true;
}

std::size_t DelayArena::live_tapes() const {
	std::size_t live = 0;
	for (std::size_t i = 0; i < count_; i++) {
		live += regions_[i].live;
	}
	return live;
}

} // namespace deluge::effects
