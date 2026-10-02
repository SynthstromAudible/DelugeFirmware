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
#include <cstdint>
#include <optional>

namespace deluge::playback {

/// Rising edges from the trigger-clock input, carried from the BSP's handler to PlaybackHandler::routine().
///
/// One producer (the BSP's trigger-clock task, through the handler) and one consumer (routine()). Each entry is
/// the edge's capture stamp: the TX play frame at the edge (see `deluge_audio_stamp_to_render_offset`).
class TriggerClockEdges {
public:
	/// Edges that can wait between two routine() calls. A power of two.
	static constexpr uint32_t kCapacity = 4;

	/// Record an edge. When kCapacity edges are already waiting, this edge is dropped and counted: the waiting
	/// edges are older and already owed.
	void push(uint32_t stamp) noexcept {
		const uint32_t received = received_.load(std::memory_order_relaxed);
		if (received - processed_.load(std::memory_order_acquire) >= kCapacity) {
			dropped_.fetch_add(1, std::memory_order_relaxed);
			return;
		}
		stamps_[received & (kCapacity - 1)].store(stamp, std::memory_order_relaxed);
		received_.store(received + 1, std::memory_order_release);
	}

	/// The oldest waiting edge's stamp, if an edge is waiting.
	std::optional<uint32_t> pop() noexcept {
		const uint32_t processed = processed_.load(std::memory_order_relaxed);
		if (processed == received_.load(std::memory_order_acquire)) {
			return std::nullopt;
		}
		const uint32_t stamp = stamps_[processed & (kCapacity - 1)].load(std::memory_order_relaxed);
		processed_.store(processed + 1, std::memory_order_release);
		return stamp;
	}

	/// Edges dropped because the ring was full.
	[[nodiscard]] uint32_t dropped() const noexcept { return dropped_.load(std::memory_order_relaxed); }

private:
	static_assert((kCapacity & (kCapacity - 1)) == 0);
	std::array<std::atomic<uint32_t>, kCapacity> stamps_{};
	std::atomic<uint32_t> received_{0};
	std::atomic<uint32_t> processed_{0};
	std::atomic<uint32_t> dropped_{0};
};

/// The edges the trigger-clock input has delivered.
extern TriggerClockEdges triggerClockEdges;

/// Point the BSP's trigger-clock handler at @ref triggerClockEdges.
void installTriggerClockInput() noexcept;

} // namespace deluge::playback
