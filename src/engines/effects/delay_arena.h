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

#include "dsp-next/core/stereo_sample.h"
#include "dsp-next/fixed/delay/delay_tape.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace deluge::effects {

/// @brief What the delay arena needs from the app: SDRAM regions from the general heap, a heap over each, and a wake
/// for the arena's job. `deluge_effects` reaches no allocator, heap or app loop itself.
///
/// lend, unlend and create_heap are called on the interaction tier only; heap_alloc, heap_free and
/// request_maintenance also from the render.
struct DelayArenaMemory {
	void* (*lend)(std::size_t bytes); ///< A region from the general heap, 16-byte aligned; null when out of memory.
	void (*unlend)(void* region);     ///< Returns a region lend gave.
	void* (*create_heap)(void* base, std::size_t bytes); ///< A heap over a whole region; null when it can't hold one.
	void* (*heap_alloc)(void* heap, std::size_t bytes);  ///< Bounded time; null when the heap has no room.
	void (*heap_free)(void* heap, void* block);          ///< Bounded time.
	void (*request_maintenance)();                       ///< Asks for the arena's job to run; safe from any tier.
};

/// The most regions the arena holds.
inline constexpr std::size_t kArenaMaxRegions = 32;

/// @brief A change to the arena's regions: the region prepare lent before the owner's audio-excluded section, the
/// regions commit detached inside it, and how many owners the change counts in (+) or out (-).
struct ArenaChange {
	void* lent = nullptr; ///< attached by commit, or returned by finish
	std::array<void*, kArenaMaxRegions> returned{};
	std::size_t returned_count = 0;
	int on_delta = 0;
};

/// @brief The memory the delays' tapes come from: regions of SDRAM the interaction tier lends, each its own heap,
/// which the render takes tapes from and gives them back to without calling the general heap.
///
/// While any owner is counted on (its delay type not OFF), the arena holds a margin: room for two tapes of the
/// largest size, and for each owner on the reserve its slot holds (the tape its settings or its largest take need; a
/// neutral-rate tape for an owner with no slot), packed largest first into the regions' free space; the two largest
/// are confirmed by a trial. A take that leaves
/// the margin short asks for maintenance; the job (or an owner's projection) then lends a region. Regions are kept
/// while their owners stay on, so a delay starting and ringing out again and again takes nothing new from the general
/// heap; they are returned when owners are counted out and what remains holds the margin, all of them when none is
/// on.
class DelayArena {
public:
	/// A region's size: room for one largest-size pair and its heap's own control block.
	static constexpr std::size_t kRegionBytes = std::size_t{1536} * 1024;
	/// The largest tape's storage: 88,200 frames plus the 20-frame write lag, 8 bytes a frame.
	static constexpr std::size_t kMaxTapeBytes =
	    (dsp::fixed::DelayTape::kMaxSize + dsp::fixed::DelayTape::kWriteLag) * sizeof(dsp::StereoSample);
	/// A tape's storage at the neutral rate: 16,384 frames plus the write lag.
	static constexpr std::size_t kNeutralTapeBytes =
	    (dsp::fixed::DelayTape::kNeutralSize + dsp::fixed::DelayTape::kWriteLag) * sizeof(dsp::StereoSample);
	/// Each tape's heap overhead, counted against a region's free bytes.
	static constexpr std::size_t kTapeOverheadBytes = 64;
	/// How many largest-size tapes the margin holds room for.
	static constexpr std::size_t kMarginMaxTapes = 2;

	explicit DelayArena(DelayArenaMemory memory, std::size_t region_bytes = kRegionBytes);
	/// Returns every region. No tape may be live.
	~DelayArena();
	DelayArena(const DelayArena&) = delete;
	DelayArena& operator=(const DelayArena&) = delete;

	/// @brief [render] A tape's storage of @p frames frames, from the first region with room; null when none has
	/// room (a shortfall). Asks for maintenance when it leaves the margin short.
	[[nodiscard]] dsp::StereoSample* take(std::size_t frames);
	/// @brief [render] Gives back @p storage, @p frames frames, as take gave it.
	void give_back(dsp::StereoSample* storage, std::size_t frames);

	/// @brief [task] Before the section: lends a region when the change counts no owner out, owners will be on after
	/// it, and the margin was last found short.
	[[nodiscard]] ArenaChange prepare(int on_delta);
	/// @brief [task] Inside the section: counts the change's owners in or out, attaches the lent region if the margin
	/// is short without it, and, when the change counts owners out, detaches every empty region not needed for the
	/// margin (every one when none is on).
	void commit(ArenaChange& change);
	/// @brief [task] After the section: returns the lent region commit did not attach, and the detached ones.
	void finish(ArenaChange& change);

	/// @brief [task, inside the section] Whether the arena holds the margin. Takes and gives back two trial tapes.
	[[nodiscard]] bool holds_margin();

	/// @brief [task, inside the section] Opens a slot reserving @p frames frames for one owner's next tape.
	/// @return The slot, for set_reserve, raise_reserve and close_slot.
	[[nodiscard]] std::size_t open_slot(std::size_t frames);
	/// @brief [task, inside the section] Sets @p slot's reserve to @p frames frames.
	void set_reserve(std::size_t slot, std::size_t frames);
	/// @brief [render] Raises @p slot's reserve to @p frames frames when that is larger, asking for maintenance when
	/// the margin no longer holds.
	void raise_reserve(std::size_t slot, std::size_t frames);
	/// @brief [task, inside the section] Closes @p slot.
	void close_slot(std::size_t slot);
	/// @return @p slot's reserve, in frames.
	[[nodiscard]] std::size_t reserve(std::size_t slot) const { return slot < slots_.size() ? slots_[slot] : 0; }

	[[nodiscard]] std::size_t regions() const { return count_; }
	/// @return Whether the margin was last found to hold.
	[[nodiscard]] bool margin_holds() const { return margin_ok_.load(std::memory_order_relaxed); }
	[[nodiscard]] std::size_t live_tapes() const;
	[[nodiscard]] int owners_on() const { return on_; }
	[[nodiscard]] uint32_t shortfalls() const { return shortfalls_.load(std::memory_order_relaxed); }

private:
	/// A lent region: its storage, the heap over it, and the tapes taken from it.
	struct Region {
		std::byte* base = nullptr;
		void* heap = nullptr;
		uint32_t live = 0;          ///< tapes taken and not given back
		std::size_t used_bytes = 0; ///< their bytes plus kTapeOverheadBytes each
	};

	/// @return Whether the regions other than @p skip (none when out of range) hold the margin.
	[[nodiscard]] bool holds_margin_without(std::size_t skip);
	/// @return [render] Whether the margin holds by the render's own count: it packs in the slots' order (no sort,
	/// no allocation) and a region is empty (an empty region holds the two largest tapes). No trial: it may find the
	/// margin short when the trial would not, never the other way.
	[[nodiscard]] bool margin_by_count() const;
	/// @return Whether the margin's items pack into the free space of the regions other than @p skip, each into the
	/// first with room: the two largest tapes, then @p reserves (frames; 0 for a closed slot) in their order, then a
	/// neutral-rate tape for each owner on with no slot.
	[[nodiscard]] bool margin_packs(std::size_t skip, std::span<const std::size_t> reserves) const;
	/// @return The index of the region holding @p storage; count_ when none does.
	[[nodiscard]] std::size_t region_of(const void* storage) const;
	/// @return Whether @p region was attached: a heap built over it.
	bool attach(void* region);

	DelayArenaMemory memory_;
	std::size_t region_bytes_;
	std::array<Region, kArenaMaxRegions> regions_{};
	std::size_t count_ = 0;
	int on_ = 0;
	/// Each open slot's reserve, in frames; 0 for a closed slot. Grown only inside the section; the render writes
	/// only its own owner's slot.
	std::vector<std::size_t> slots_;
	std::size_t open_slots_ = 0;
	/// The sorted reserves' scratch, for margin_packs on the interaction tier.
	std::vector<std::size_t> scratch_;
	std::atomic<bool> margin_ok_{false}; ///< written by take (the render) and commit; read by prepare
	std::atomic<uint32_t> shortfalls_{0};
};

} // namespace deluge::effects
