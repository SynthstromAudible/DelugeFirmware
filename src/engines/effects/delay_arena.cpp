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

#include "foundation/panic.h"

#include <algorithm>
#include <array>
#include <functional>

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
		FREEZE_WITH_ERROR("DA02"); // storage the arena never handed out: a double give-back or a foreign tape
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
	if (on_ + change.on_delta < 0) {
		FREEZE_WITH_ERROR("DA01"); // an owner counted out that was never counted in
	}
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

bool DelayArena::margin_packs(std::size_t skip, std::span<const std::size_t> reserves) const {
	std::array<std::size_t, kArenaMaxRegions> free{};
	for (std::size_t i = 0; i < count_; i++) {
		free[i] = i == skip ? 0 : region_bytes_ - std::min(region_bytes_, regions_[i].used_bytes);
	}
	const auto place = [&](std::size_t bytes) {
		for (std::size_t i = 0; i < count_; i++) {
			if (free[i] >= bytes) {
				free[i] -= bytes;
				return true;
			}
		}
		return false;
	};
	for (std::size_t m = 0; m < kMarginMaxTapes; m++) {
		if (!place(kMaxTapeBytes + kTapeOverheadBytes)) {
			return false;
		}
	}
	const auto bytes_of = [](std::size_t frames) { return (frames * sizeof(dsp::StereoSample)) + kTapeOverheadBytes; };
	for (const std::size_t frames : reserves) {
		if (frames != 0 && !place(bytes_of(frames))) {
			return false;
		}
	}
	const int unslotted = on_ - static_cast<int>(open_slots_);
	for (int o = 0; o < unslotted; o++) {
		if (!place(kNeutralTapeBytes + kTapeOverheadBytes)) {
			return false;
		}
	}
	return true;
}

bool DelayArena::margin_by_count() const {
	if (on_ <= 0) {
		return false;
	}
	bool empty_region = false;
	for (std::size_t i = 0; i < count_; i++) {
		empty_region = empty_region || regions_[i].live == 0;
	}
	// The render's own count packs in the slots' order, without the interaction tier's sorted scratch.
	return empty_region && margin_packs(kArenaMaxRegions, slots_);
}

bool DelayArena::holds_margin_without(std::size_t skip) {
	// Largest first packs tightest; the sort's scratch belongs to the interaction tier.
	scratch_.assign(slots_.begin(), slots_.end());
	std::ranges::sort(scratch_, std::ranges::greater{});
	if (!margin_packs(skip, scratch_)) {
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

std::size_t DelayArena::open_slot(std::size_t frames) {
	open_slots_++;
	for (std::size_t slot = 0; slot < slots_.size(); slot++) {
		if (slots_[slot] == 0) {
			slots_[slot] = std::max<std::size_t>(frames, 1);
			return slot;
		}
	}
	slots_.push_back(std::max<std::size_t>(frames, 1));
	return slots_.size() - 1;
}

void DelayArena::set_reserve(std::size_t slot, std::size_t frames) {
	if (slot < slots_.size() && slots_[slot] != 0) {
		slots_[slot] = std::max<std::size_t>(frames, 1);
		margin_ok_.store(margin_by_count(), std::memory_order_relaxed);
	}
}

void DelayArena::raise_reserve(std::size_t slot, std::size_t frames) {
	if (slot >= slots_.size() || slots_[slot] == 0 || frames <= slots_[slot]) {
		return;
	}
	slots_[slot] = frames;
	if (!margin_by_count()) {
		margin_ok_.store(false, std::memory_order_relaxed);
		memory_.request_maintenance();
	}
}

void DelayArena::close_slot(std::size_t slot) {
	if (slot < slots_.size() && slots_[slot] != 0) {
		slots_[slot] = 0;
		open_slots_--;
	}
}

std::size_t DelayArena::live_tapes() const {
	std::size_t live = 0;
	for (std::size_t i = 0; i < count_; i++) {
		live += regions_[i].live;
	}
	return live;
}

} // namespace deluge::effects
