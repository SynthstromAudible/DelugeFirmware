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

#include <cstdint>
#include <string>
#include <string_view>

namespace deluge::storage {

/// @brief The temp name a confirmed overwrite writes beside its target: `<target>.tmp`.
///
/// Hidden from every browser, which admits a file only by its last extension, and obvious on a computer. An existing
/// `<target>.tmp` can only be the leftover of an earlier failed save of that target, so the write overwrites it.
/// @param target The file being replaced.
/// @return The temp path, or an empty string when it would not fit the channels' 255-character path bound.
[[nodiscard]] std::string tempPathFor(std::string_view target);

/// @brief What one step of a temp-file swap produced.
enum class SwapTick : uint8_t {
	Waiting,  ///< Nothing answered this tick. The CALLER applies its own stall bound.
	Advanced, ///< The target's delete was answered; the rename remains.
	Done,     ///< The temp file now holds the target's name.
	Failed,   ///< The delete or the rename failed, or a path could not be keyed.
};

/// @brief Replace a target with its verified temp file, a tick at a time through the byte channel: delete the
///        target, then rename the temp file onto it.
///
/// The delete comes first because a FAT rename refuses an existing destination. A target that is already gone counts
/// as deleted. Holds no stall bound, as `ReadBackCheck` holds none: the caller already owns one.
class TempSwap {
public:
	/// @brief Start a swap.
	/// @param target The file being replaced.
	/// @param temp   The verified temp file that replaces it.
	void begin(std::string target, std::string temp) {
		target_ = std::move(target);
		temp_ = std::move(temp);
		stage_ = Stage::Unlink;
		servedOps_ = 0;
	}

	/// @brief Advance by at most one channel operation.
	[[nodiscard]] SwapTick step();

	/// @brief How many channel answers this swap has collected since the last call, whatever they said, and reset
	///        the count.
	[[nodiscard]] int32_t takeServedOps() {
		const int32_t served = servedOps_;
		servedOps_ = 0;
		return served;
	}

private:
	enum class Stage : uint8_t { Unlink, Rename, Done };

	std::string target_;
	std::string temp_;
	Stage stage_{Stage::Unlink};
	/// Answers collected since the last takeServedOps().
	int32_t servedOps_{0};
};

/// @brief The same swap, blocking. Legal ONLY on the storage owner.
/// @param target The file being replaced.
/// @param temp   The verified temp file that replaces it.
/// @return true when the temp file now holds the target's name.
[[nodiscard]] bool swapSynchronously(const std::string& target, const std::string& temp);

/// @brief Remove the temp file a failed save left, best effort: the outcome is not reported.
///
/// Through the byte channel's queue where the channel is the place for card I/O, and inline where blocking is legal.
/// @param temp The temp file.
void discardTemp(const std::string& temp);

} // namespace deluge::storage
