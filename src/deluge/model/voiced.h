/*
 * Copyright © 2025 Katherine Whitlock
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

class ModelStackWithSoundFlags;

struct Voiced {
	virtual ~Voiced() = default;
	virtual bool anyNoteIsOn() = 0;
	virtual bool hasAnyVoices() = 0;
	virtual void unassignAllVoices() = 0;
	virtual bool allowNoteTails(ModelStackWithSoundFlags* modelStack, bool disregardSampleLoop = false) = 0;
	virtual void prepareForHibernation() {}
};
