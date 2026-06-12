/*
 * Copyright © 2022-2023 Synthstrom Audible Limited
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
#include "storage/file_item.h"
#include "hid/display/display.h"
#include "io/debug/log.h"
#include "model/instrument/instrument.h"
#include <cstring>

Error FileItem::setupWithInstrument(Instrument* newInstrument, bool hibernating) {
	filename = newInstrument->name.c_str();
	filename += ".XML";
	filenameIncludesExtension = true;
	instrument = newInstrument;
	isFolder = false;
	instrumentAlreadyInSong = !hibernating;
	displayName = filename.c_str();
	maybeExistsOnCard = newInstrument->mightExistOnCard;

	return Error::NONE;
}

Error FileItem::getFilenameWithExtension(std::string* filenameWithExtension) {
	(*filenameWithExtension) = filename;
	if (!filenameIncludesExtension) {
		(*filenameWithExtension).append(".XML");
	}
	return Error::NONE;
}

Error FileItem::getFilenameWithoutExtension(std::string* filenameWithoutExtension) {
	(*filenameWithoutExtension) = filename;
	if (filenameIncludesExtension) {
		char const* chars = filenameWithoutExtension->c_str();
		char const* dotAddress = strrchr(chars, '.');
		if (dotAddress) {
			int32_t newLength = (uint32_t)dotAddress - (uint32_t)chars;
			(*filenameWithoutExtension).resize(newLength);
		}
	}
	return Error::NONE;
}

Error FileItem::getDisplayNameWithoutExtension(std::string* displayNameWithoutExtension) {
	if (display->haveOLED()) {
		return getFilenameWithoutExtension(displayNameWithoutExtension);
	}

	// 7SEG...
	(*displayNameWithoutExtension) = displayName;
	if (filenameIncludesExtension) {
		char const* chars = displayNameWithoutExtension->c_str();
		char const* dotAddress = strrchr(chars, '.');
		if (dotAddress) {
			int32_t newLength = (uint32_t)dotAddress - (uint32_t)chars;
			(*displayNameWithoutExtension).resize(newLength);
		}
	}
	return Error::NONE;
}
