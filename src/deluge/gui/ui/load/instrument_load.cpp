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

#include "gui/ui/load/instrument_load.h"

#include "definitions_cxx.hpp"
#include "gui/ui/load/load_instrument_preset_ui.h"
#include "gui/ui/ui.h"
#include "hid/display/display.h"
#include "memory/general_memory_allocator.h"
#include "model/instrument/instrument.h"
#include "model/instrument/midi_instrument.h"
#include "model/song/song.h"
#include "processing/sound/sound_drum.h"
#include "storage/owner.h"

namespace deluge::gui {

InstrumentLoad instrumentLoad{};

namespace {

void closeGate() {
	currentUIMode = UI_MODE_LOADING_SONG_ESSENTIAL_SAMPLES;
}

void openGate() {
	// Clear-song and song-load close the same mode: only the load's own is released.
	if (currentUIMode == UI_MODE_LOADING_SONG_ESSENTIAL_SAMPLES) {
		currentUIMode = UI_MODE_NONE;
	}
}

/// Whether a request is a plain commit of the file @p active is already loading, and so promotes it.
bool promotes(const InstrumentLoadRequest& request, const InstrumentLoadRequest& active) {
	return request.commit && !request.clone && !active.commit && !active.clone
	       && request.target.path == active.target.path;
}

} // namespace

void InstrumentLoad::request(const InstrumentLoadRequest& request) {
	if (phase_ != Phase::Idle && promotes(request, active_)) {
		// Select on the file a scroll-load is already loading: that load becomes the commit, rather than start again.
		active_.commit = true;
		active_.completion = request.completion;
		coalescer_.clear_queued();
		closeGate();
		return;
	}
	if (request.commit) {
		closeGate();
	}
	if (!coalescer_.request(request)) {
		return; // queued: the load in flight is cancelled at its next phase boundary
	}
	start(coalescer_.current());
	routine();
}

void InstrumentLoad::cancel() {
	coalescer_.reset();
	if (phase_ == Phase::Idle) {
		return;
	}
	const bool commit = active_.commit;
	abandon();
	phase_ = Phase::Idle;
	if (commit) {
		openGate();
	}
}

void InstrumentLoad::routine() {
	while (step()) {}
}

void InstrumentLoad::start(const InstrumentLoadRequest& request) {
	active_ = request;
	decision_ = {};
	newInstrument_ = nullptr;
	newDrum_ = nullptr;
	swapped_ = nullptr;
	if (active_.commit) {
		closeGate();
	}
	phase_ = Phase::Decide;
}

bool InstrumentLoad::step() {
	// A newer request cancels this one at a phase boundary. The swap runs in one pass, so it never is one.
	if ((phase_ == Phase::Stage || phase_ == Phase::Samples) && coalescer_.queued().has_value()) {
		const bool commit = active_.commit;
		abandon();
		supersessions_++;
		if (commit) {
			openGate();
		}
		start(*coalescer_.complete());
		return true;
	}
	switch (phase_) {
	case Phase::Idle:
		return false;
	case Phase::Decide:
		return stepDecide();
	case Phase::Stage:
		return stepStage();
	case Phase::Parse:
		return stepParse();
	case Phase::Samples:
		return stepSamples();
	case Phase::Swap:
		return stepSwap();
	case Phase::MidiDef:
		return stepMidiDef();
	}
	return false;
}

bool InstrumentLoad::stepDecide() {
	decision_ = loadInstrumentPresetUI.decideLoad(active_);
	if (decision_.done) {
		return finish(decision_.error);
	}
	phase_ = (decision_.reuse != nullptr) ? Phase::Swap : Phase::Stage;
	return true;
}

StorageManager::StagedOpen InstrumentLoad::stage(const char* path, const char* firstTagName, const char* altTagName) {
	if (!stagingOpen_) {
		// Counted for the preset-load gate (PL-TIER): a document read, and whether it began on the storage owner.
		documentReads_++;
		documentReadsOnOwner_ += deluge::storage::Owner::on_owner() ? 1 : 0;
		previousDeserializer_ = activeDeserializer;
		stagingOpen_ = true;
	}
	const StorageManager::StagedOpen opened =
	    StorageManager::openDocumentStaged(path, firstTagName, altTagName, &staging_);
	if (opened == StorageManager::StagedOpen::Failed) {
		releaseStaging();
	}
	return opened;
}

void InstrumentLoad::releaseStaging() {
	if (!stagingOpen_) {
		return;
	}
	StorageManager::releaseStaging(&staging_);
	// The memory reader openDocumentStaged() left in activeDeserializer reads the bytes just freed.
	activeDeserializer = previousDeserializer_;
	stagingOpen_ = false;
}

bool InstrumentLoad::stepStage() {
	const char* firstTagName = nullptr;
	const char* altTagName = nullptr;
	StorageManager::instrumentDocumentTags(active_.target.loadingSynthToKitRow ? OutputType::SYNTH
	                                                                           : Browser::outputTypeToLoad,
	                                       &firstTagName, &altTagName);
	switch (stage(active_.target.path.c_str(), firstTagName, altTagName)) {
	case StorageManager::StagedOpen::Deferred:
		return false;
	case StorageManager::StagedOpen::Failed:
		return finish(Error::FILE_CORRUPTED);
	case StorageManager::StagedOpen::Opened:
		phase_ = Phase::Parse;
		return true;
	}
	return false;
}

bool InstrumentLoad::stepParse() {
	Error error = Error::NONE;
	std::string name = active_.target.name;
	std::string dirPath = active_.target.dirPath;
	if (active_.target.loadingSynthToKitRow) {
		error = StorageManager::parseSynthDrumDocument(currentSong, loadInstrumentPresetUI.instrumentClipToLoadFor,
		                                               &newDrum_);
	}
	else {
		error =
		    StorageManager::parseInstrumentDocument(currentSong, loadInstrumentPresetUI.instrumentClipToLoadFor,
		                                            Browser::outputTypeToLoad, false, &newInstrument_, &name, &dirPath);
	}
	releaseStaging();
	if (error != Error::NONE) {
		// The parse deleted what it had made.
		newInstrument_ = nullptr;
		newDrum_ = nullptr;
		return finish(error);
	}
	if (active_.clone) {
		newInstrument_->name = decision_.clonedName;
		newInstrument_->editedByUser = true;
	}

	// Counted for the preset-load gate (PL-TIER): a sample load, and whether it began on the storage owner.
	sampleLoads_++;
	sampleLoadsOnOwner_ += deluge::storage::Owner::on_owner() ? 1 : 0;
	display->displayLoadingAnimationText("Loading", false, true);
	if (active_.target.loadingSynthToKitRow) {
		load_.emplace(deluge::storage::AsyncSampleLoad::forDrum(*newDrum_, nullptr));
	}
	else {
		load_.emplace(deluge::storage::AsyncSampleLoad::forOutput(*newInstrument_, nullptr));
	}
	phase_ = Phase::Samples;
	return true;
}

bool InstrumentLoad::stepSamples() {
	using State = deluge::storage::AsyncSampleLoad::State;
	using Reason = deluge::storage::AsyncSampleLoad::Reason;

	const deluge::storage::AsyncSampleLoad::Progress progress = load_->tick();
	if (progress.state == State::Loading) {
		return false;
	}
	if (progress.state == State::Failed) {
		// The Instrument is kept: swapped in with the holders that never answered left empty.
		if (progress.reason == Reason::Stalled) {
			giveUps_++;
		}
		display->displayError(Error::SD_CARD);
	}
	load_.reset();
	display->removeLoadingAnimation();
	phase_ = Phase::Swap;
	return true;
}

bool InstrumentLoad::stepSwap() {
	Error error = Error::NONE;
	if (active_.target.loadingSynthToKitRow) {
		error = loadInstrumentPresetUI.swapKitRow(active_, newDrum_);
		newDrum_ = nullptr; // the kit's now, or discarded by the swap
	}
	else {
		Instrument* instrument = (decision_.reuse != nullptr) ? decision_.reuse : newInstrument_;
		error = loadInstrumentPresetUI.swapInstrument(active_, decision_, instrument);
		newInstrument_ = nullptr; // the song's now
		swapped_ = instrument;
	}
	if (error != Error::NONE) {
		return finish(error);
	}
	if (swapped_ != nullptr && swapped_->type == OutputType::MIDI_OUT
	    && static_cast<MIDIInstrument*>(swapped_)->loadDeviceDefinitionFile) {
		phase_ = Phase::MidiDef;
		return true;
	}
	return finish(Error::NONE);
}

bool InstrumentLoad::stepMidiDef() {
	auto* midiInstrument = static_cast<MIDIInstrument*>(swapped_);
	switch (stage(midiInstrument->deviceDefinitionFileName.c_str(), "midiDevice", "")) {
	case StorageManager::StagedOpen::Deferred:
		return false;
	case StorageManager::StagedOpen::Failed:
		// A definition that is not on the card is skipped, as it always was.
		return finish(Error::NONE);
	case StorageManager::StagedOpen::Opened:
		midiInstrument->loadDeviceDefinitionFile = false;
		(void)StorageManager::parseMidiDeviceDefinitionDocument(midiInstrument,
		                                                        &midiInstrument->deviceDefinitionFileName, false);
		releaseStaging();
		return finish(Error::NONE);
	}
	return false;
}

void InstrumentLoad::abandon() {
	releaseStaging();
	if (load_.has_value()) {
		load_.reset();
		display->removeLoadingAnimation();
	}
	if (newInstrument_ != nullptr) {
		// Parsed, never added to the song.
		currentSong->deleteOutput(newInstrument_);
		newInstrument_ = nullptr;
	}
	if (newDrum_ != nullptr) {
		currentSong->deleteBackedUpParamManagersForModControllable(newDrum_);
		void* toDealloc = static_cast<void*>(newDrum_);
		newDrum_->~SoundDrum();
		deluge::memory::dealloc(toDealloc);
		newDrum_ = nullptr;
	}
}

bool InstrumentLoad::finish(Error error) {
	const bool commit = active_.commit;
	const auto completion = active_.completion;
	abandon();
	phase_ = Phase::Idle;
	std::optional<InstrumentLoadRequest> next = coalescer_.complete();
	if (commit) {
		openGate();
	}
	if (completion != nullptr) {
		completion(error);
	}
	// The completion may have closed the browser, which cancels whatever was queued.
	if (next.has_value() && coalescer_.in_flight() && phase_ == Phase::Idle) {
		start(*next);
		return true;
	}
	return false;
}

} // namespace deluge::gui
