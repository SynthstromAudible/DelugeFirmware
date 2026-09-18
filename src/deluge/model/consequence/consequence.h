/*
 * Copyright © 2017-2023 Synthstrom Audible Limited
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
#include <cstdint>
#include <string>

class InstrumentClip;
class Song;
class ModelStack;

class Consequence {
public:
	enum {
		CLIP_LENGTH = 1,
		CLIP_BEGIN_LINEAR_RECORD = 2,
		PARAM_CHANGE = 3,
		NOTE_ARRAY_CHANGE = 4,
	};

	Consequence();
	virtual ~Consequence();

	virtual void prepareForDestruction(int32_t whichQueueActionIn, Song* song) {}
	virtual Error revert(TimeType time, ModelStack* modelStack) = 0;
	/// @brief The file this consequence's revert() will bind, if it binds one.
	///
	/// Reverting used to read the card through here, which is why the whole reversion ran on the storage owner.
	/// deluge::gui::Reversion asks every consequence this BEFORE reverting and loads what comes back, so revert()
	/// finds the file in memory and reads nothing. A consequence that binds no file returns null, which is almost
	/// all of them.
	[[nodiscard]] virtual const std::string* filePathNeededForRevert() const { return nullptr; }
	Consequence* next{};
	uint8_t type;
};
