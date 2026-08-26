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

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

/// @brief Power-of-two ring buffer lane shared by transport-specific queue managers.
///
/// @warning Capacity MUST be a power of two (checked by MIDIQueueStorage): positions wrap with a
///          bitmask, not a modulo by an arbitrary size.
/// @note The lane always keeps one slot unused so `read_pos == write_pos` can unambiguously mean
///       empty. Usable capacity is therefore `capacity() - 1`. All logical offsets passed to and
///       returned from this class are relative to `read_pos`, which lets callers scan or overwrite
///       queued messages without exposing wrapped physical indices.
/// @note A view, not an owner. MIDIQueueStorage owns one flat pool and hands each lane its slice, so
///       lanes can have different capacities without becoming different types.
template <typename T>
class MIDIQueueLane {
public:
	/// @brief Backing storage for this lane, owned by MIDIQueueStorage.
	T* data{nullptr};
	/// @brief `capacity() - 1`. Positions wrap with this mask, so capacity must be a power of two.
	uint16_t mask{0};

	/// @brief Total slots in this lane, one of which is always kept unused.
	[[nodiscard]] uint16_t capacity() const { return static_cast<uint16_t>(mask + 1); }

	/// @brief Physical index of the oldest queued entry.
	uint16_t read_pos{0};
	/// @brief Physical index of the next slot to write.
	///
	/// @warning Owned by the producer. See remove_span_via_head_swap() for why the consumer must never
	///          write this field directly.
	uint16_t write_pos{0};

	/// @brief Returns whether the lane holds no queued entries.
	///
	/// @note The acquire fence pairs with the release fence in push(). Producer and consumer are on the
	///       same core (mainline vs. ISR), so no hardware barrier is needed and none is emitted; this
	///       only stops the compiler hoisting a read of `data[]` above the read of `write_pos` that
	///       proved the entry was published. Every other accessor that needs the pairing reaches it
	///       through this function or size().
	[[nodiscard]] bool empty() const {
		uint16_t published = write_pos;
		std::atomic_signal_fence(std::memory_order_acquire);
		return read_pos == published;
	}
	/// @brief Returns the number of queued entries.
	///
	/// @note Acquires for the same reason as empty(); pop_many(), space() and
	///       remove_span_via_head_swap() all read occupancy through here.
	[[nodiscard]] uint16_t size() const {
		uint16_t published = write_pos;
		std::atomic_signal_fence(std::memory_order_acquire);
		return static_cast<uint16_t>((published - read_pos) & mask);
	}
	/// @brief Returns the number of additional entries that can be pushed before the lane is full.
	[[nodiscard]] uint16_t space() const { return static_cast<uint16_t>(mask - size()); }
	/// @brief Reads a queued entry without removing it.
	/// @param offset Logical offset from `read_pos`; 0 is the lane head.
	/// @return The entry at @p offset.
	[[nodiscard]] T peek(uint16_t offset = 0) const { return data[(read_pos + offset) & mask]; }

	/// @brief Appends one entry to the tail of the lane.
	/// @param value Entry to enqueue.
	/// @return True if the entry was stored; false if the lane is full (usable capacity is `capacity() - 1`).
	bool push(T value) {
		// Advance write position first so we can detect the one-unused-slot full case.
		uint16_t next = static_cast<uint16_t>((write_pos + 1) & mask);
		if (next == read_pos) {
			return false;
		}
		// Store into the current write slot, then publish it by moving write_pos. The release fence keeps
		// the compiler from sinking the slot store past the write_pos store, which would let a consumer
		// that interrupts between the two read an unwritten slot. Compiler barrier only: producer and
		// consumer share a core, so this emits no instructions.
		data[write_pos] = value;
		std::atomic_signal_fence(std::memory_order_release);
		write_pos = next;
		return true;
	}

	/// @brief Removes and returns the oldest queued entry.
	/// @param out Out: set to the removed entry on success.
	/// @return True if an entry was popped; false if the lane was empty.
	bool pop(T& out) {
		if (empty()) {
			return false;
		}
		// Copy the oldest queued entry before advancing the read position.
		out = data[read_pos];
		read_pos = static_cast<uint16_t>((read_pos + 1) & mask);
		return true;
	}

	/// @brief Removes @p count queued entries as one atomic span, oldest first.
	/// @param out   Destination buffer; must hold at least @p count entries.
	/// @param count Number of entries to pop.
	/// @return True if all @p count entries were popped; false (with no state change) if fewer were queued.
	bool pop_many(T* out, uint16_t count) {
		if (size() < count) {
			// The caller asked for an atomic span; do not partially pop it.
			return false;
		}
		// Copy the contiguous logical span, even if the physical ring wraps.
		for (uint16_t i = 0; i < count; i++) {
			out[i] = data[(read_pos + i) & mask];
		}
		// Commit the pop only after all requested entries were copied.
		read_pos = static_cast<uint16_t>((read_pos + count) & mask);
		return true;
	}

	/// @brief Removes a logical span by exchanging it with the head, then dropping the head.
	///
	/// Scheduled CC dequeue can select an entry that is not at the lane head. Two properties matter here:
	///
	/// @warning `write_pos` is owned by the producer, and `consume_queued_messages()` runs in an ISR
	///          (see the warnings in midi_engine.cpp) - so the consumer must never write it. Clearing
	///          and rebuilding the ring to remove a mid-lane span would race a mainline `push()` and
	///          could lose or reorder queued MIDI. Swapping with the head instead touches only
	///          `read_pos` and the two spans being exchanged.
	/// @note The freed slot is reclaimed immediately. Marking the span dead in place instead would leak
	///       one slot per out-of-order removal until it reached the head, which under sustained CC
	///       automation fills the lane and starts dropping messages.
	///
	/// Scheduled removal only runs when the head is itself a three-byte channel CC, so the head span and
	/// the selected span are always the same width. The entry displaced from the head moves to the
	/// selected position; CC entries are independent of one another, and this feature already reorders
	/// them by design, so that is within the ordering the scheduler is allowed to produce.
	///
	/// @param target_offset Logical offset of the span to remove, relative to `read_pos`.
	/// @param span          Width of the span, in entries.
	/// @param removed_out   Destination buffer for the removed entries; must hold at least @p span entries.
	/// @return True if the span was removed; false if it falls outside the current logical queue contents.
	bool remove_span_via_head_swap(uint16_t target_offset, uint16_t span, T* removed_out) {
		uint16_t queue_size = size();
		if (span > queue_size || target_offset > static_cast<uint16_t>(queue_size - span)) {
			// The requested span is outside the current logical queue contents.
			return false;
		}

		for (uint16_t i = 0; i < span; i++) {
			uint16_t head_index = static_cast<uint16_t>((read_pos + i) & mask);
			uint16_t selected_index = static_cast<uint16_t>((read_pos + target_offset + i) & mask);
			// Copy the selected entry out, then move the head entry into the slot it vacated.
			removed_out[i] = data[selected_index];
			data[selected_index] = data[head_index];
		}

		// Dropping the head is what actually frees the slot. When target_offset is 0 this degenerates to a
		// plain pop, which is exactly right.
		read_pos = static_cast<uint16_t>((read_pos + span) & mask);
		return true;
	}

	/// @brief Empties the lane by resetting both positions to zero.
	void clear() {
		// With this ring representation, equal positions mean empty.
		read_pos = 0;
		write_pos = 0;
	}

	/// @brief Overwrites a queued entry in place without changing queue occupancy.
	/// @param logical_offset Logical offset from `read_pos` of the entry to overwrite.
	/// @param value          New value for that entry.
	void overwrite_at(uint16_t logical_offset, T value) { data[(read_pos + logical_offset) & mask] = value; }
};

/// @brief Fixed set of power-of-two queue lanes shared by a transport-specific manager.
template <typename T, size_t LaneCount, uint16_t const (&Capacities)[LaneCount]>
class MIDIQueueStorage {
public:
	/// @brief The priority lanes, indexed by QueuePriority.
	std::array<MIDIQueueLane<T>, LaneCount> lanes{};

	/// @brief Points each lane at its slice of the pool and sets its wrap mask.
	MIDIQueueStorage() {
		uint32_t offset = 0;
		for (size_t i = 0; i < LaneCount; i++) {
			lanes[i].data = pool.data() + offset;
			lanes[i].mask = static_cast<uint16_t>(Capacities[i] - 1);
			offset += Capacities[i];
		}
	}

	/// @brief Total slots in one lane, one of which is always kept unused.
	/// @param lane Priority lane index.
	/// @return That lane's capacity.
	[[nodiscard]] uint16_t lane_capacity(uint8_t lane) const { return lanes[lane].capacity(); }

	/// @name Per-lane accessors
	/// @{

	/// @brief Returns the number of queued entries in one lane.
	/// @param lane Priority lane index.
	/// @return Entries currently queued in that lane.
	[[nodiscard]] uint16_t queue_count(uint8_t lane) const { return lanes[lane].size(); }
	/// @brief Returns the total number of entries queued across all lanes.
	/// @return Sum of every lane's queued entry count.
	[[nodiscard]] uint32_t total_queued_messages() const {
		uint32_t queued = 0;
		for (auto const& queue_lane : lanes) {
			// Sum all priority lanes so callers can make whole-device decisions.
			queued += queue_lane.size();
		}
		return queued;
	}

	/// @brief Reads one lane's head entry without removing it.
	/// @param lane Priority lane index.
	/// @return The entry at the lane head.
	[[nodiscard]] T head(uint8_t lane) const { return lanes[lane].peek(); }
	/// @brief Removes and returns one lane's head entry.
	/// @param lane Priority lane index.
	/// @param out  Out: set to the removed entry on success.
	/// @return True if an entry was popped; false if that lane was empty.
	bool pop_head(uint8_t lane, T& out) { return lanes[lane].pop(out); }
	/// @brief Removes @p count entries from one lane as one atomic span, oldest first.
	/// @param lane  Priority lane index.
	/// @param out   Destination buffer; must hold at least @p count entries.
	/// @param count Number of entries to pop.
	/// @return True if all @p count entries were popped; false (with no state change) if fewer were queued.
	bool pop_many(uint8_t lane, T* out, uint16_t count) { return lanes[lane].pop_many(out, count); }
	/// @brief Appends one entry to one lane.
	/// @param lane  Priority lane index.
	/// @param value Entry to enqueue.
	/// @return True if the entry was stored; false if that lane is full.
	bool push(uint8_t lane, T value) { return lanes[lane].push(value); }
	/// @brief Returns whether one lane holds no queued entries.
	/// @param lane Priority lane index.
	/// @return True if that lane is empty.
	[[nodiscard]] bool empty(uint8_t lane) const { return lanes[lane].empty(); }
	/// @brief Returns how many more entries can be pushed to one lane before it is full.
	/// @param lane Priority lane index.
	/// @return Remaining usable capacity of that lane.
	[[nodiscard]] uint16_t space(uint8_t lane) const { return lanes[lane].space(); }

	/// @}

	/// @brief Empties every lane.
	///
	/// @note Storage only. Policy state layered above the lanes (CC debt, scheduling bookkeeping) is
	///       owned elsewhere and must be reset by its owner.
	void clear() {
		for (auto& queue_lane : lanes) {
			// Drop queued transport data from every priority lane.
			queue_lane.clear();
		}
	}

private:
	/// @brief Sum of every lane's capacity: the size of the flat pool the lanes are carved from.
	static constexpr uint32_t total_capacity() {
		uint32_t total = 0;
		for (size_t i = 0; i < LaneCount; i++) {
			total += Capacities[i];
		}
		return total;
	}

	static_assert(([] {
		              for (size_t i = 0; i < LaneCount; i++) {
			              if (Capacities[i] == 0 || (Capacities[i] & (Capacities[i] - 1)) != 0) {
				              return false;
			              }
		              }
		              return true;
	              })(),
	              "every lane capacity must be an exact power of two: positions wrap with a bitmask");

	/// @brief One flat allocation the lanes carve slices from, so lanes can differ in capacity without
	///        becoming different types.
	std::array<T, total_capacity()> pool{};
};
