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

#include "gui/ui/browser/browser.h"
#include "gui/ui/load/load_instrument_preset_ui.h"
#include "gui/views/session_view.h"
#include "model/instrument/instrument.h"
#include "model/song/song.h"
#include "storage/file_item.h"
#include "storage/owner.h"
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

deluge::storage::AcquireResult acquireUnlaunchedPreset(OutputType outputType) {
	deluge::storage::AcquireResult acquired{};
	Browser::currentDir = getInstrumentFolder(outputType);

	auto result = loadInstrumentPresetUI.findAnUnlaunchedPresetIncludingWithinSubfolders(
	    currentSong, outputType, Availability::INSTRUMENT_UNUSED);
	if (!result.has_value()) {
		acquired.error = result.error();
		Browser::emptyFileItems();
		return acquired;
	}

	FileItem* fileItem = *result;
	Instrument* instrument = fileItem->instrument;
	acquired.alreadyInSong = instrument != nullptr && fileItem->instrumentAlreadyInSong;
	acquired.wasHibernating = instrument != nullptr && !fileItem->instrumentAlreadyInSong;
	if (instrument == nullptr) {
		std::string name = fileItem->getFilenameWithoutExtension();
		std::string dirPath = Browser::currentDir;
		const std::string path = Browser::currentDir + "/" + fileItem->getFilenameWithExtension();
		acquired.error = loadPresetFixture(currentSong, outputType, &instrument, path.c_str(), &name, &dirPath);
		acquired.loadedFromFile = acquired.error == Error::NONE;
		acquired.name = name;
	}
	else {
		acquired.name = instrument->name;
	}
	// The list is rebuilt by the next listing; nothing above may outlive it, so it is emptied before the samples.
	Browser::emptyFileItems();

	if (acquired.error != Error::NONE) {
		return acquired;
	}
	acquired.instrument = instrument;
	if (!acquired.alreadyInSong) {
		// What the gesture's own acquisition loads: a preset read from the card, or a hibernating Instrument's
		// samples. One already active in the song is taken as it is.
		(void)instrument->loadAllAudioFiles(true);
	}
	return acquired;
}

Clip* createNewInstrumentClip(OutputType outputType, int32_t yDisplay) {
	struct Ctx {
		OutputType outputType;
		int32_t yDisplay;
		Clip* result;
	} ctx{outputType, yDisplay, nullptr};

	// On the owner: the walk recurses over subfolders and must run start-to-finish there, which is what the gesture's
	// own walk does through InstrumentLoad.
	deluge::storage::Owner::run_or_inline(
	    [](void* ctxPtr) {
		    auto* c = static_cast<Ctx*>(ctxPtr);
		    if (c->outputType != OutputType::SYNTH && c->outputType != OutputType::KIT) {
			    c->result = sessionView.createNewInstrumentClipAcquired(c->outputType, c->yDisplay, nullptr);
			    return;
		    }
		    deluge::storage::AcquireResult acquired = acquireUnlaunchedPreset(c->outputType);
		    c->result = sessionView.createNewInstrumentClipAcquired(c->outputType, c->yDisplay, &acquired);
	    },
	    &ctx);
	return ctx.result;
}

} // namespace deluge::harness

#endif // DELUGE_HOST
