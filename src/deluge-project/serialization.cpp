/*
 * Copyright © 2024 Synthstrom Audible Limited
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

#include "deluge-project/serialization.h"
#include "deluge-project/groove.h"
#include "model/song/song.h"
#include "storage/storage_manager.h" // Serializer / Deserializer
#include <algorithm>
#include <cstring>

namespace deluge::project {

void saveSong(::Song& song, Serializer& out) {
	song.writeToFile(out);
}

Error loadSong(::Song& song, Deserializer& in) {
	return song.readFromFile(in);
}

// --- Groove ---

void serialize(const Groove& groove, Serializer& out, ::Song* song) {
	out.writeAttribute("swingAmount", groove.swing_amount);
	out.writeAbsoluteSyncLevelToFile(song, "swingInterval", (SyncLevel)groove.swing_interval, true);
}

bool deserialize(Groove& groove, Deserializer& in, char const* tagName) {
	if (!strcmp(tagName, "swingAmount")) {
		groove.swing_amount = in.readTagOrAttributeValueInt();
		groove.swing_amount = std::min(groove.swing_amount, (int8_t)49);
		groove.swing_amount = std::max(groove.swing_amount, (int8_t)-49);
		in.exitTag("swingAmount");
		return true;
	}
	if (!strcmp(tagName, "swingInterval")) {
		// swingInterval, unlike other "sync" type params, we read as just its plain int32_t value, and only shift it by
		// insideWorldTickMagnitude after reading the whole song (the two attributes can be stored in either order, so
		// we won't know both until the end; also pre-V3.1.0-alpha stored all "sync" values as plain ints).
		groove.swing_interval = in.readTagOrAttributeValueInt();
		groove.swing_interval = std::min(groove.swing_interval, (uint8_t)9);
		in.exitTag("swingInterval");
		return true;
	}
	return false;
}

} // namespace deluge::project
