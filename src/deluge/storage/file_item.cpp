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
	filename.set(&newInstrument->name);
	Error error = filename.concatenate(".XML");
	if (error != Error::NONE) {
		return error;
	}
	filenameIncludesExtension = true;
	instrument = newInstrument;
	isFolder = false;
	instrumentAlreadyInSong = !hibernating;
	displayName = filename.get();
	if (newInstrument->existsOnCard && filePointer.sclust == 0) {
		String tempFilePath;
		tempFilePath.set(newInstrument->dirPath.get());
		tempFilePath.concatenate("/");
		tempFilePath.concatenate(filename.get());
		existsOnCard = StorageManager::fileExists(tempFilePath.get(), &filePointer);
		if (!existsOnCard) {
			// this is recoverable later - will make a default synth or browse from top folder when encountering the
			// null filepointer
			D_PRINTLN("couldn't get filepath for file %s", filename.get());
			// so we don't look for it again
			newInstrument->existsOnCard = false;
		}
	}
	else {
		existsOnCard = newInstrument->existsOnCard;
	}

	return Error::NONE;
}

Error FileItem::getFilenameWithExtension(String* filenameWithExtension) {
	filenameWithExtension->set(&filename);
	if (!filenameIncludesExtension) {
		Error error = filenameWithExtension->concatenate(".XML");
		if (error != Error::NONE) {
			return error;
		}
	}
	return Error::NONE;
}

Error FileItem::getFilenameWithoutExtension(String* filenameWithoutExtension) {
	filenameWithoutExtension->set(&filename);
	if (filenameIncludesExtension) {
		char const* chars = filenameWithoutExtension->get();
		char const* dotAddress = strrchr(chars, '.');
		if (dotAddress) {
			int32_t newLength = (uint32_t)dotAddress - (uint32_t)chars;
			Error error = filenameWithoutExtension->shorten(newLength);
			if (error != Error::NONE) {
				return error;
			}
		}
	}
	return Error::NONE;
}

Error FileItem::getDisplayNameWithoutExtension(String* displayNameWithoutExtension) {
	if (display->haveOLED()) {
		return getFilenameWithoutExtension(displayNameWithoutExtension);
	}

	// 7SEG...
	Error error = displayNameWithoutExtension->set(displayName);
	if (error != Error::NONE) {
		return error;
	}
	if (filenameIncludesExtension) {
		char const* chars = displayNameWithoutExtension->get();
		char const* dotAddress = strrchr(chars, '.');
		if (dotAddress) {
			int32_t newLength = (uint32_t)dotAddress - (uint32_t)chars;
			error = displayNameWithoutExtension->shorten(newLength);
			if (error != Error::NONE) {
				return error;
			}
		}
	}
	return Error::NONE;
}
