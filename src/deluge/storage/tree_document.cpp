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

#include "storage/tree_document.h"

#include "processing/engines/audio_engine.h"
#include "storage/parse_json.h"
#include "storage/parse_xml.h"
#include "storage/storage_manager.h"

#include <cstring>
#include <string_view>

namespace deluge::storage {

Error TreeDocument::open(std::span<const uint8_t> bytes, bool isJson, char const* firstTagName, char const* altTagName,
                         bool ignoreIncorrectFirmware) {
	AudioEngine::logAction("TreeDocument::open");
	release();
	// The document is read as its own stamp says, or as the oldest official firmware when it has none: never as
	// whatever was read before it, which would skip an unstamped file's old-file rules.
	song_firmware_version = FirmwareVersion::official({});

	const std::string_view text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
	tree_ = isJson ? parseJson(text) : parseXml(text);
	if (!tree_) {
		return Error::FILE_CORRUPTED;
	}
	reader_.emplace(*tree_, isJson ? SourceFormat::Json : SourceFormat::Xml);
	if (isJson && !reader_->match('{')) { // a JSON document is one object, whose members are read from inside it
		release();
		return Error::FILE_CORRUPTED;
	}

	char const* tagName;
	while (*(tagName = reader_->readNextTagOrAttributeName())) {
		if (!strcmp(tagName, firstTagName) || (altTagName != nullptr && *altTagName && !strcmp(tagName, altTagName))) {
			return Error::NONE;
		}
		const Error result = reader_->tryReadingFirmwareTagFromFile(tagName, ignoreIncorrectFirmware);
		if (result != Error::NONE && result != Error::RESULT_TAG_UNUSED) {
			release();
			return result;
		}
		reader_->exitTag(tagName);
	}
	release();
	return Error::FILE_CORRUPTED;
}

void TreeDocument::release() {
	reader_.reset();
	tree_.reset();
}

} // namespace deluge::storage
