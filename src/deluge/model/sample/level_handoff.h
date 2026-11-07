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

/// @brief The render's per-block input levels, handed to the screen: the loudest mean absolute samples since the
///        screen last took them, and how many samples they cover.
/// @details Two slots, so a take never reads a slot the render is still raising: the render writes the active one,
///          and a take makes the other active before reading the one it left. On the device the render preempts the
///          screen, so a render that began before the switch has finished before the take reads on; one that begins
///          after it writes the other slot. Every field is atomic, so a host whose render runs on another thread can
///          at worst show a block a take late.
class LevelHandoff {
public:
	/// @brief What a take finds.
	struct Taken {
		float mean_l;     ///< The loudest left mean.
		float mean_r;     ///< The loudest right mean.
		uint32_t samples; ///< The samples the means cover; 0 when no block arrived.
	};

	/// @brief [render] One block's means: kept if louder than the slot's, its samples counted.
	void put(float mean_l, float mean_r, uint32_t samples) {
		Slot& slot = slots_[active_.load(std::memory_order_acquire)];
		if (slot.mean_l.load(std::memory_order_relaxed) < mean_l) {
			slot.mean_l.store(mean_l, std::memory_order_relaxed);
		}
		if (slot.mean_r.load(std::memory_order_relaxed) < mean_r) {
			slot.mean_r.store(mean_r, std::memory_order_relaxed);
		}
		slot.samples.fetch_add(samples, std::memory_order_relaxed);
	}

	/// @brief [interaction] Everything put since the last take, and an empty slot for the next.
	Taken take() { return finish_take(begin_take()); }

	/// @brief [interaction] take()'s first half: makes the other slot the one puts land in.
	/// @return The slot left, for finish_take().
	uint32_t begin_take() {
		const uint32_t left = active_.load(std::memory_order_relaxed);
		active_.store(1 - left, std::memory_order_release);
		return left;
	}

	/// @brief [interaction] take()'s second half: reads and empties the slot begin_take() left. A put between the two
	///        halves, as a render preempting the screen there makes, lands in the other slot for the next take.
	Taken finish_take(uint32_t left) {
		Slot& slot = slots_[left];
		return Taken{
		    .mean_l = slot.mean_l.exchange(0, std::memory_order_relaxed),
		    .mean_r = slot.mean_r.exchange(0, std::memory_order_relaxed),
		    .samples = slot.samples.exchange(0, std::memory_order_relaxed),
		};
	}

	/// @brief [interaction] Both slots empty, before the render starts putting.
	void clear() {
		for (Slot& slot : slots_) {
			slot.mean_l.store(0, std::memory_order_relaxed);
			slot.mean_r.store(0, std::memory_order_relaxed);
			slot.samples.store(0, std::memory_order_relaxed);
		}
	}

private:
	struct Slot {
		std::atomic<float> mean_l{0};
		std::atomic<float> mean_r{0};
		std::atomic<uint32_t> samples{0};
	};
	std::array<Slot, 2> slots_{};
	std::atomic<uint32_t> active_{0};
};
