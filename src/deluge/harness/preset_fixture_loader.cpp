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

#include "harness/preset_fixture_loader.h"

#ifdef DELUGE_HOST

#include "gui/views/session_view.h"
#include "model/instrument/instrument.h"
#include "model/song/song.h"
#include "storage/storage_manager.h"
#include "util/functions.h"

namespace deluge::harness {

Error loadPresetFixture(Song* song, OutputType outputType, Instrument** getInstrument, char const* path,
                        std::string* name, std::string* dirPath) {
	char const* firstTagName = nullptr;
	char const* altTagName = nullptr;
	StorageManager::instrumentDocumentTags(outputType, &firstTagName, &altTagName);

	Error error = StorageManager::openDocumentSynchronously(path, firstTagName, altTagName);
	if (error != Error::NONE) {
		return error;
	}

	error = StorageManager::parseInstrumentDocument(song, nullptr, outputType, false, getInstrument, name, dirPath);
	StorageManager::releaseDocumentStaging();
	return error;
}

namespace {

/// Load @p presetPath as the gesture's acquisition would, without the walk: a fixture knows which preset it wrote.
deluge::storage::AcquireResult loadNamedPreset(OutputType outputType, const char* presetPath) {
	deluge::storage::AcquireResult acquired{};
	const std::string path = presetPath;
	const size_t slash = path.rfind('/');
	std::string dirPath = (slash == std::string::npos) ? std::string{} : path.substr(0, slash);
	std::string name = path.substr(slash == std::string::npos ? 0 : slash + 1);
	if (const size_t dot = name.rfind('.'); dot != std::string::npos) {
		name.resize(dot);
	}
	Instrument* instrument = nullptr;
	acquired.error = loadPresetFixture(currentSong, outputType, &instrument, path.c_str(), &name, &dirPath);
	if (acquired.error != Error::NONE) {
		return acquired;
	}
	acquired.loadedFromFile = true;
	acquired.name = name;
	acquired.instrument = instrument;
	// What the gesture's own acquisition loads: the preset's samples.
	(void)instrument->loadAllAudioFiles(true);
	return acquired;
}

} // namespace

Clip* createNewInstrumentClip(OutputType outputType, int32_t yDisplay, const char* presetPath) {
	if (outputType != OutputType::SYNTH && outputType != OutputType::KIT) {
		return sessionView.createNewInstrumentClipAcquired(outputType, yDisplay, nullptr);
	}
	deluge::storage::AcquireResult acquired = loadNamedPreset(outputType, presetPath);
	if (acquired.error != Error::NONE) {
		return nullptr;
	}
	return sessionView.createNewInstrumentClipAcquired(outputType, yDisplay, &acquired);
}

} // namespace deluge::harness

#endif // DELUGE_HOST
