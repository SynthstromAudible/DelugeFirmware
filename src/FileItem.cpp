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
#include <FileItem.h>
#include "instrument.h"
#include <string.h>

FileItem::FileItem() {
	instrument = NULL;
	filenameIncludesExtension = true;
	instrumentAlreadyInSong = false;
}

int FileItem::setupWithInstrument(Instrument* newInstrument, bool hibernating) {
	filename.set(&newInstrument->name);
	int error = filename.concatenate(".XML");
	if (error) return error;
	filenameIncludesExtension = true;
	instrument = newInstrument;
	isFolder = false;
	instrumentAlreadyInSong = !hibernating;
	displayName = filename.get();
	return NO_ERROR;
}

int FileItem::getFilenameWithExtension(String* filenameWithExtension) {
	filenameWithExtension->set(&filename);
	if (!filenameIncludesExtension) {
		int error = filenameWithExtension->concatenate(".XML");
		if (error) return error;
	}
	return NO_ERROR;
}

int FileItem::getFilenameWithoutExtension(String* filenameWithoutExtension) {
	filenameWithoutExtension->set(&filename);
	if (filenameIncludesExtension) {
		char const* chars = filenameWithoutExtension->get();
		char const* dotAddress = strrchr(chars, '.');
		if (dotAddress) {
			int newLength = (uint32_t)dotAddress - (uint32_t)chars;
			int error = filenameWithoutExtension->shorten(newLength);
			if (error) return error;
		}
	}
	return NO_ERROR;
}

int FileItem::getDisplayNameWithoutExtension(String* displayNameWithoutExtension) {
#if !HAVE_OLED
	if (displayName != filename.get()) {
		int error = displayNameWithoutExtension->set(displayName);
		if (error) return error;
		if (filenameIncludesExtension) {
			char const* chars = displayNameWithoutExtension->get();
			char const* dotAddress = strrchr(chars, '.');
			if (dotAddress) {
				int newLength = (uint32_t)dotAddress - (uint32_t)chars;
				error = displayNameWithoutExtension->shorten(newLength);
				if (error) return error;
			}
		}
		return NO_ERROR;
	}
	else
#endif
	{
		return getFilenameWithoutExtension(displayNameWithoutExtension);
	}
}
