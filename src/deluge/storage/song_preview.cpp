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

#include "storage/song_preview.h"
#include "storage/storage_manager.h"
#include "util/c_string.h"

#include <cstring>

namespace deluge::storage {

namespace {

/// @brief Walk an already-opened document to its `preview` attribute and paint it.
///
/// Lifted unchanged out of the song browser's own preview render, including the early exits: this is the
/// one walk, and both entry points below run it so the buffer decode cannot drift from the file decode.
///
/// @param reader     A deserializer whose document is already open and positioned past the root tag.
/// @param imageStore Destination pad rows, written in place.
/// @return `true` if a `preview` attribute was found and painted.
bool runPreviewWalk(FileDeserializer* reader, RGB (*imageStore)[kDisplayWidth + kSideBarWidth]) {
	char const* tagName;
	bool rendered = false;

	// Set and never read, and kept anyway: readTagOrAttributeValueInt() ADVANCES the reader, so this arm
	// is what carries the walk past the previewNumPads attribute. Delete the variable and you delete the
	// read; the walk would then meet that attribute's value where it expects a tag name.
	int32_t previewNumPads = 40;
	while (*(tagName = reader->readNextTagOrAttributeName())) {

		if (!strcmp(tagName, "previewNumPads")) {
			previewNumPads = reader->readTagOrAttributeValueInt();
			reader->exitTag("previewNumPads");
		}
		else if (!strcmp(tagName, "preview")) {
			// Dead in the original too -- the row loop never skips. Preserved verbatim rather than
			// dropped, so this walk stays a pure relocation of the browser's.
			int32_t skipNumCharsAfterRow = 0;

			int32_t startX = 0;
			int32_t startY = 0;
			int32_t endX = kDisplayWidth + kSideBarWidth;
			int32_t endY = kDisplayHeight;

			int32_t width = endX - startX;
			int32_t numCharsToRead = width * 3 * 2;

			if (!reader->prepareToReadTagOrAttributeValueOneCharAtATime()) {
				goto stopLoadingPreview;
			}

			for (int32_t y = startY; y < endY; y++) {
				char const* hexChars = reader->readNextCharsOfTagOrAttributeValue(numCharsToRead);
				if (!hexChars) {
					goto stopLoadingPreview;
				}

				for (int32_t x = startX; x < endX; x++) {
					for (int32_t colour = 0; colour < 3; colour++) {
						imageStore[y][x][colour] = hexToByte(hexChars);
						hexChars += 2;
					}
					imageStore[y][x] = imageStore[y][x].greyOut(6500000);
				}
			}
			rendered = true;
			goto stopLoadingPreview;
		}
		else {
			reader->exitTag(tagName);
		}
	}
stopLoadingPreview:
	// Closes activeDeserializer, not `reader`, exactly as the browser did before this walk moved here.
	// They are the same object for both entry points below -- each takes its reader FROM
	// activeDeserializer straight after opening -- so this is a faithful move, not a latent mismatch.
	activeDeserializer->closeWriter();
	return rendered;
}

/// @brief decodeSongPreview()'s body, with the global `activeDeserializer` left wherever it lands.
///
/// Split out purely so decodeSongPreview() has ONE exit at which to put that global back -- see there.
SongPreviewOutcome decodeSongPreviewInto(std::span<const uint8_t> buffer, bool isJson,
                                         RGB (*imageStore)[kDisplayWidth + kSideBarWidth], ::Error* openError) {
	::Error error = StorageManager::openDelugeBuffer(buffer, isJson, "song");
	if (error != ::Error::NONE) {
		if (openError != nullptr) {
			*openError = error;
		}
		return SongPreviewOutcome::Error;
	}
	FileDeserializer* reader = activeDeserializer;
	if (isJson) {
		reader->match('{');
	}

	if (runPreviewWalk(reader, imageStore)) {
		return SongPreviewOutcome::Rendered;
	}
	// The buffer may be a prefix, so running out of bytes is NOT proof the song has no preview -- say
	// which of the two it was and let the caller decide whether blank artwork is the honest answer.
	return reader->reachedEnd() ? SongPreviewOutcome::Truncated : SongPreviewOutcome::NoPreview;
}

} // namespace

SongPreviewOutcome decodeSongPreview(std::span<const uint8_t> buffer, bool isJson,
                                     RGB (*imageStore)[kDisplayWidth + kSideBarWidth], ::Error* openError) {
	// Put `activeDeserializer` back on EVERY path, the failed open included (openDelugeBuffer() points
	// it at the memory reader before it knows whether the document opened). This is not tidiness: the
	// memory reader's `fileClusterBuffer` IS the caller's borrowed span, and code downstream writes
	// THROUGH that global without opening anything of its own -- SaveSongUI's sample collection moves a
	// file straight into `activeDeserializer->file` and then reads Cluster::size (32 KiB) at a time into
	// `activeDeserializer->fileClusterBuffer`. Left pointing here, that is a 32 KiB write into a buffer
	// that may be 4 KiB and may already be gone: heap corruption, not a stale read.
	FileDeserializer* const previous = activeDeserializer;
	const SongPreviewOutcome outcome = decodeSongPreviewInto(buffer, isJson, imageStore, openError);
	activeDeserializer = previous;
	return outcome;
}

SongPreviewOutcome decodeSongPreviewFromFile(char const* path, RGB (*imageStore)[kDisplayWidth + kSideBarWidth],
                                             ::Error* openError) {
	::Error error = StorageManager::openDelugeFile(path, "song");
	if (error != ::Error::NONE) {
		if (openError != nullptr) {
			*openError = error;
		}
		return SongPreviewOutcome::Error;
	}
	FileDeserializer* reader = activeDeserializer;
	if (activeDeserializer == &smJsonDeserializer) {
		activeDeserializer->match('{');
	}

	// No Truncated arm: this walk had the whole file, so ending without a preview means there is none.
	return runPreviewWalk(reader, imageStore) ? SongPreviewOutcome::Rendered : SongPreviewOutcome::NoPreview;
}

} // namespace deluge::storage
