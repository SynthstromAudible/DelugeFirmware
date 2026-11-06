
/*
 * Copyright © 2015-2026 Synthstrom Audible Limited
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

#include "io/stall_bound.h"
#include "libdeluge/file_prefix.h"
#include "runtime/app_loop.h"

#include <cstdint>
#include <string>

namespace deluge::gui::ui {

/// @brief A DX7 cartridge read: a `.syx` file's bytes through the file prefix channel, parsed into
/// `menu_item::dxCartridge` when they arrive.
///
/// Both menus' `.syx` browsers read through the one instance @ref dxCartridgeRead returns, so one read is in flight
/// at a time, as the channel's single slot is. The browser that started it stays up until it ends: a failure leaves
/// it up, so another file can be picked.
class DxCartridgeRead {
public:
	/// @brief Where a read stands.
	enum class Status : uint8_t {
		Idle,    ///< no read in flight
		Pending, ///< the read is in flight
		Loaded,  ///< the read ended with a cartridge parsed into `menu_item::dxCartridge`
		Failed,  ///< the read ended without one; a popup said why
	};

	/// @brief The channel a read goes through, swappable so a spec can answer it.
	struct Channel {
		DelugeChannelStatus (*collect)(const char* path, uint8_t* out, uint32_t out_cap, uint32_t* out_len);
		void (*request)(const char* path, uint32_t max_bytes);
		/// Whether boot has finished: before it nothing answers the channel, and the read runs inline.
		bool (*booted)();
	};

	/// @brief How long a read may find the channel unanswered before it gives up, in microseconds: 9 s, on the
	///        clock, however often the caller polls.
	static constexpr uint64_t kStallUs = 9'000'000;

	/// @param channel The file prefix channel, libdeluge's by default.
	/// @param clock   The clock the stall bound measures on, in microseconds.
	explicit DxCartridgeRead(Channel channel = libdelugeChannel(), io::StallBound::Clock clock = &runtime::nowUs);

	/// @brief Start reading @p path, unless a read is already in flight.
	///
	/// Before boot finishes the file is read and parsed inline (`DxCartridge::tryLoad`), so the result is known at
	/// once. After it, the request is left to @ref poll: the channel answers UNKNOWN for a path it has not been asked
	/// for, and asking (again) is what that means, so the first ask and every re-ask are one path.
	/// @return Pending, or Loaded or Failed for an inline read. Idle when a read was already in flight or @p path is
	///         empty, and nothing started.
	Status start(std::string path);

	/// @brief Collect the read in flight, on the caller's tick.
	/// @return Pending while it runs; Loaded or Failed once, as it ends; Idle when none is in flight.
	Status poll();

	/// @brief Whether a read is in flight.
	[[nodiscard]] bool busy() const { return in_flight_; }

	/// @brief Forget the read in flight, as its browser goes away: a late answer is displaced by the next request.
	void cancel();

	/// @brief How many reads gave up because the channel left them unanswered for @ref kStallUs.
	[[nodiscard]] int32_t give_ups() const { return give_ups_; }

	/// @brief Read through @p channel from now on: a spec answers the shared read's channel itself.
	void set_channel(Channel channel) { channel_ = channel; }

	/// @brief libdeluge's file prefix channel and boot flag.
	static Channel libdelugeChannel();

private:
	Channel channel_;
	std::string path_;
	bool in_flight_ = false;
	io::StallBound wait_;
	int32_t give_ups_ = 0;
};

/// @brief The one cartridge read both menus' `.syx` browsers share.
DxCartridgeRead& dxCartridgeRead();

} // namespace deluge::gui::ui
