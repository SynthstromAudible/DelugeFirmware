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

#include "scenarios/preset_fixture_loader.h"

#include "gui/views/session_view.h"
#include "model/instrument/instrument.h"
#include "model/mod_controllable/mod_controllable_audio.h"
#include "model/song/song.h"
#include "modulation/params/param_manager.h"
#include "processing/sound/sound_instrument.h"
#include "processing/source.h"
#include "storage/audio/audio_file_holder.h"
#include "storage/multi_range/multi_range.h"
#include "storage/storage_manager.h"
#include "storage/tree_document.h"
#include "util/functions.h"

namespace deluge::harness {

Error loadPresetFixture(Song* song, OutputType outputType, Instrument** getInstrument, char const* path,
                        std::string* name, std::string* dirPath) {
	char const* firstTagName = nullptr;
	char const* altTagName = nullptr;
	StorageManager::instrumentDocumentTags(outputType, &firstTagName, &altTagName);

	deluge::storage::TreeDocument document;
	Error error = StorageManager::openDocumentTreeSynchronously(path, firstTagName, altTagName, document);
	if (error != Error::NONE) {
		return error;
	}

	return StorageManager::parseInstrumentDocument(document, song, nullptr, outputType, false, getInstrument, name,
	                                               dirPath);
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

bool writeSampleSynthPreset(const char* path, const char* samplePath) {
	ParamManagerForTimeline paramManager;
	Instrument* instrument = StorageManager::createNewInstrument(OutputType::SYNTH, &paramManager);
	if (instrument == nullptr) {
		return false;
	}
	auto* synth = static_cast<SoundInstrument*>(instrument);
	synth->setupAsBlankSynth(&paramManager, false);
	synth->sources[0].setOscType(OscType::SAMPLE);
	MultiRange* range = synth->sources[0].getOrCreateFirstRange();
	AudioFileHolder* holder = (range != nullptr) ? range->getAudioFileHolder() : nullptr;
	if (holder == nullptr) {
		return false;
	}
	holder->filePath = samplePath;
	currentSong->live.param_backup.backUp(static_cast<ModControllableAudio*>(instrument->toModControllable()), nullptr,
	                                      &paramManager, true);
	if (StorageManager::createXMLFile(path, smSerializer, /*mayOverwrite=*/true, /*displayErrors=*/false)
	    != Error::NONE) {
		return false;
	}
	instrument->writeToFile(nullptr, currentSong);
	return GetSerializer().closeFileAfterWriting(path, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n", "\n</sound>\n")
	       == Error::NONE;
}

} // namespace deluge::harness
