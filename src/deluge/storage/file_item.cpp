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

std::string FileItem::getFilenameWithExtension() const {
	std::string result = filename;
	if (!filenameIncludesExtension) {
		result.append(".XML");
	}
	return result;
}

std::string FileItem::getFilenameWithoutExtension() const {
	std::string result = filename;
	if (filenameIncludesExtension) {
		size_t dotPos = result.rfind('.');
		if (dotPos != std::string::npos) {
			result.resize(dotPos);
		}
	}
	return result;
}
