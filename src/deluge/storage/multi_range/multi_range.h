/*
 * Copyright © 2018-2023 Synthstrom Audible Limited
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

class AudioFileHolder;

class MultiRange {
public:
	MultiRange();
	virtual ~MultiRange();
	MultiRange(MultiRange&&) noexcept = default;
	MultiRange& operator=(MultiRange&&) noexcept = default;

	virtual AudioFileHolder* getAudioFileHolder() = 0;

	/// @brief This range's identity: no other range made in this run has it. The range keeps it while its source's
	///        ranges are inserted and deleted around it and while another file is loaded into it; a range a type change
	///        or a load makes has its own. Never saved: what is keyed by it (the marker editor's view) is not either.
	[[nodiscard]] uint64_t identity() const { return identity_; }

	int16_t topNote{};

private:
	/// @brief The next identity: counts up for the whole run.
	static uint64_t nextIdentity();

	/// Minted by every constructor but the move, which carries it: the source's vector moves a range as others are
	/// inserted or erased, and the range is still the same one.
	uint64_t identity_{nextIdentity()};
};
