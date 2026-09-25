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
#include "gui/ui/browser/browser.h"
#include "gui/ui/load/load_instrument_preset_ui.h"
#include "gui/ui/ui.h"
#include "gui/views/view.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "memory/general_memory_allocator.h"
#include "model/instrument/instrument.h"
#include "model/instrument/midi_instrument.h"
#include "model/song/song.h"
#include "processing/sound/sound_drum.h"
#include "storage/file_item.h"
#include "storage/owner.h"
#include "util/functions.h"

namespace deluge::gui {

InstrumentLoad instrumentLoad{};

namespace {

using Kind = InstrumentLoadRequest::Kind;

void closeGate() {
	currentUIMode = UI_MODE_LOADING_SONG_ESSENTIAL_SAMPLES;
}

void openGate() {
	// Clear-song and song-load close the same mode: only the load's own is released.
	if (currentUIMode == UI_MODE_LOADING_SONG_ESSENTIAL_SAMPLES) {
		currentUIMode = UI_MODE_NONE;
	}
}

/// Whether @p request creates a new Instrument (a new instrument, new track or clone).
bool isCreation(const InstrumentLoadRequest& request) {
	return request.kind == Kind::Acquire && deluge::storage::isCreationConsumer(request.preset.consumer);
}

/// Whether @p request is one a newer request may not cancel or replace: a creation, the browser's exit revert, or a
/// device definition.
bool neverSuperseded(const InstrumentLoadRequest& request) {
	return isCreation(request) || request.kind == Kind::Revert || request.kind == Kind::MidiDefinition;
}

/// Whether a request is a plain commit of the file @p active is already loading, and so promotes it.
bool promotes(const InstrumentLoadRequest& request, const InstrumentLoadRequest& active) {
	return request.kind == Kind::Browse && active.kind == Kind::Browse && request.commit && !request.clone
	       && !active.commit && !active.clone && request.target.path == active.target.path;
}

} // namespace

bool InstrumentLoad::request(const InstrumentLoadRequest& request) {
	if (const std::optional<InstrumentLoadRequest>& queued = coalescer_.queued();
	    queued.has_value() && neverSuperseded(*queued)) {
		// A queued creation, revert or definition is never replaced: superseding the middle of three quick new-track
		// presses would lose a track, and superseding a revert would leave the clip on what the user backed out of.
		// What comes after it is refused, and the gesture kept is the one the user made first.
		return false;
	}
	if (phase_ != Phase::Idle && promotes(request, active_)) {
		// Select on the file a scroll-load is already loading: that load becomes the commit, rather than start again.
		active_.commit = true;
		active_.completion = request.completion;
		coalescer_.clear_queued();
		closeGate();
		return true;
	}
	if (request.commit) {
		closeGate();
	}
	if (!coalescer_.request(request)) {
		return true; // queued: the load in flight is cancelled at its next phase boundary, unless it is a creation
	}
	start(coalescer_.current());
	routine();
	return true;
}

void InstrumentLoad::cancel() {
	const std::optional<InstrumentLoadRequest>& queued = coalescer_.queued();
	if (queued.has_value() && queued->kind == Kind::Browse) {
		coalescer_.clear_queued();
	}
	if (phase_ == Phase::Idle || active_.kind != Kind::Browse) {
		return;
	}
	const bool commit = active_.commit;
	abandon();
	phase_ = Phase::Idle;
	if (commit) {
		openGate();
	}
	// An acquisition queued behind the browser's load still runs.
	std::optional<InstrumentLoadRequest> next = coalescer_.complete();
	if (next.has_value()) {
		start(*next);
	}
}

void InstrumentLoad::routine() {
	tickWalkHere();
	if (phase_ == Phase::Idle && currentSong != nullptr && currentSong->blankPresetPending) {
		// A blank song asks for its first preset here, on this tier: its builder runs before any tier exists (boot) or
		// inside an owner operation (clear-song), so neither can ask. Cleared first: request() runs a pass of its own.
		currentSong->blankPresetPending = false;
		(void)request({.kind = Kind::Acquire,
		               .preset = {.consumer = deluge::storage::PresetConsumer::BlankSongPreset,
		                          .outputType = OutputType::SYNTH,
		                          .dirPath = getInstrumentFolder(OutputType::SYNTH)}});
	}
	while (step()) {}
}

bool InstrumentLoad::acquisitionInFlight() const {
	const std::optional<InstrumentLoadRequest>& queued = coalescer_.queued();
	return (phase_ != Phase::Idle && active_.kind == Kind::Acquire)
	       || (queued.has_value() && queued->kind == Kind::Acquire);
}

void InstrumentLoad::start(const InstrumentLoadRequest& request) {
	active_ = request;
	decision_ = {};
	newInstrument_ = nullptr;
	newDrum_ = nullptr;
	acquired_ = {};
	walk_ = {};
	walkBegun_ = false;
	walkDispatched_ = false;
	if (active_.commit) {
		closeGate();
	}
	// A revert and a definition know their file already: they read it straight away.
	phase_ = (active_.kind == Kind::Acquire)          ? Phase::Walk
	         : (active_.kind == Kind::Revert)         ? Phase::Stage
	         : (active_.kind == Kind::MidiDefinition) ? Phase::MidiDef
	                                                  : Phase::Decide;
}

bool InstrumentLoad::supersede() {
	const bool commit = active_.commit;
	abandon();
	supersessions_++;
	if (commit) {
		openGate();
	}
	start(*coalescer_.complete());
	return true;
}

bool InstrumentLoad::step() {
	// A newer request cancels this one at a phase boundary. The swap runs in one pass, so it never is one; a walk's is
	// once it has landed (stepWalk()).
	if ((phase_ == Phase::Stage || phase_ == Phase::Samples) && coalescer_.queued().has_value()
	    && !neverSuperseded(active_)) {
		return supersede();
	}
	switch (phase_) {
	case Phase::Idle:
		return false;
	case Phase::Decide:
		return stepDecide();
	case Phase::Walk:
		return stepWalk();
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

bool InstrumentLoad::stepWalk() {
	if (walkRunning_) {
		return false; // a walk is out, perhaps an abandoned load's: nothing new is queued until it ends
	}
	if (!walkDispatched_) {
		if (!walkBegun_) {
			walkBegun_ = true;
			// The gesture's card-free prologue, as the request starts: a request superseded while queued never runs it.
			if (!loadInstrumentPresetUI.beginAcquisition(active_.preset, &walk_)) {
				return finish(Error::NONE, false);
			}
			if (walk_.walk == deluge::storage::AcquireWalk::Navigate) {
				if (originValid_) {
					// A navigate superseding a navigate walks on from where that one's walk landed.
					walk_.name = originName_;
					walk_.dirPath = originDir_;
				}
			}
			else {
				originValid_ = false;
			}
			// Counted for song-preview-ab: an acquisition that runs, not one coalesced away.
			LoadInstrumentPresetUI::presetAcquisitionsRun_++;
		}
		walkOut_ = walk_;
		if (walkOut_.walk == deluge::storage::AcquireWalk::Unlaunched
		    || walkOut_.walk == deluge::storage::AcquireWalk::UnusedSlot) {
			// The Unlaunched search no longer needs the owner: it spans ticks instead of blocking,
			// and it holds its own listing, so there is nothing for it to collide with. The first
			// attempt at this waited on Browser::isListingInProgress() because the two shared the
			// browser's file list; they no longer share anything, so the wait is gone too. The
			// UnusedSlot scan runs here for the same reason: one folder read, into a listing of its own.
			walkRunning_ = true;
			walkHere_ = true;
			walkDispatched_ = true;
			beginWalkHere();
			return false;
		}
		walkHere_ = false;
		walkRunning_ = true;
		walkDispatched_ = deluge::storage::Owner::run(&InstrumentLoad::runWalkOp, this);
		if (!walkDispatched_) {
			walkRunning_ = false; // the owner's queue was full: queue it on a later pass
		}
		return false;
	}

	// The walk has landed.
	const bool navigate = walk_.walk == deluge::storage::AcquireWalk::Navigate;
	if (navigate && walked_.error == Error::NONE) {
		originValid_ = true;
		originName_ = walked_.name;
		originDir_ = walked_.dirPath;
		echoNavigation();
	}
	if (coalescer_.queued().has_value() && !neverSuperseded(active_)) {
		return supersede();
	}
	if (walked_.error != Error::NONE) {
		return finish(walked_.error);
	}
	if (walk_.walk == deluge::storage::AcquireWalk::UnusedSlot) {
		acquired_.name = walked_.name; // a name, and nothing to load
		return finish(Error::NONE);
	}
	acquired_.instrument = walked_.existing;
	acquired_.alreadyInSong = walked_.existing != nullptr && walked_.alreadyInSong;
	acquired_.wasHibernating = walked_.existing != nullptr && !walked_.alreadyInSong;
	if (acquired_.alreadyInSong) {
		return finish(Error::NONE);
	}
	if (acquired_.wasHibernating) {
		beginSampleLoad(*walked_.existing);
		phase_ = Phase::Samples;
		return true;
	}
	phase_ = Phase::Stage;
	return true;
}

void InstrumentLoad::tickWalkHere() {
	if (!walkRunning_ || !walkHere_) {
		return;
	}
	// Driven from HERE, not from stepWalk(), and that is not a detail. abandon() states the
	// invariant the dispatched arm relied on -- "a walk still out ends on its own" -- which held
	// because the storage owner ran that operation whether or not this load was still interested.
	// An inline walk has no such driver. cancel() and supersede() set phase_ = Idle while a walk is
	// out, and step() returns at Phase::Idle without ever reaching stepWalk(), so a walk ticked only
	// from there is stranded: walkRunning_ stays true and every later walk queues behind a search
	// nobody is advancing. Measured: the save gate got one tick and then stopped forever.
	const bool waiting =
	    (walkOut_.walk == deluge::storage::AcquireWalk::UnusedSlot) ? slot_scan_.tick() : walk_machine_.tick();
	if (waiting) {
		return;
	}
	finishWalkHere();
	walkRunning_ = false;
}

void InstrumentLoad::beginWalkHere() {
	if (walkOut_.walk == deluge::storage::AcquireWalk::UnusedSlot) {
		// Counted for the preset-load gate (AQ-SLOT): where the walk began.
		slotScans_++;
		slotScansOnOwner_ += deluge::storage::Owner::on_owner() ? 1 : 0;
		slot_scan_.arm(loadInstrumentPresetUI, walkOut_.dirPath, walkOut_.outputType);
		return;
	}
	// The search's folder goes straight to the walk. It used to be parked in Browser::currentDir for
	// the walk to read back -- which moved whatever browser was open: an acquisition for "SYNTHS"
	// relocated a save browser sitting in "SONGS", and its save then wrote nothing.
	walk_machine_.arm(loadInstrumentPresetUI, walkOut_.dirPath, walkOut_.includeSong ? currentSong : nullptr,
	                  walkOut_.outputType, walkOut_.availability);
}

void InstrumentLoad::finishWalkHere() {
	if (walkOut_.walk == deluge::storage::AcquireWalk::UnusedSlot) {
		// The fields runWalkOp's UnusedSlot arm filled: a name, or the error, and nothing to load.
		AcquireSnapshot found{};
		const auto result = slot_scan_.result();
		if (result.has_value()) {
			found.name = *result;
		}
		else {
			found.error = result.error();
		}
		walked_ = std::move(found);
		return;
	}
	// The same tail runWalkOp writes, over the same fields, so nothing downstream can tell which arm
	// produced the snapshot.
	AcquireSnapshot found{};
	FileItem* item = nullptr;
	const auto result = walk_machine_.result();
	if (result.has_value()) {
		item = *result;
	}
	else {
		found.error = result.error();
	}
	if (item != nullptr) {
		found.existing = item->instrument;
		found.alreadyInSong = item->instrumentAlreadyInSong;
		found.name = (item->instrument != nullptr) ? item->instrument->name : item->getFilenameWithoutExtension();
		// The folder the SEARCH ended in, read off the walk's own listing. Not Browser::currentDir:
		// the walk no longer moves the browser, so the browser's directory is no longer an answer to
		// where this preset lives.
		found.dirPath = walk_machine_.foundDir();
		found.path = walk_machine_.foundDir() + "/" + item->getFilenameWithExtension();
	}
	// No emptyFileItems() here, unlike runWalkOp's tail: the items are in the WALK's listing, not the
	// browser's, and clearing the browser's would blank a list this search never touched.
	walked_ = std::move(found);
}

void InstrumentLoad::runWalkOp(void* self) {
	auto* load = static_cast<InstrumentLoad*>(self);
	const deluge::storage::AcquireWalkSpec& spec = load->walkOut_;
	AcquireSnapshot found{};
	Browser::currentDir = spec.dirPath;
	FileItem* item = nullptr;
	switch (spec.walk) {
	case deluge::storage::AcquireWalk::Unlaunched:
		// Unreachable: stepWalk() never queues this kind any more, it runs the search itself through
		// PresetWalk. The label stays so the switch stays exhaustive, and so that re-adding the
		// dispatch has to be a deliberate edit rather than a fall-through.
		break;
	case deluge::storage::AcquireWalk::Confirm: {
		std::string searchName = spec.name;
		auto result =
		    loadInstrumentPresetUI.confirmPresetOrNextUnlaunchedOne(spec.outputType, &searchName, spec.availability);
		if (result.has_value()) {
			item = *result;
		}
		else {
			found.error = result.error();
		}
		break;
	}
	case deluge::storage::AcquireWalk::UnusedSlot:
		// Unreachable: stepWalk() runs this kind itself, through SlotNameScan. The label stays so the switch stays
		// exhaustive, and so that re-adding the dispatch has to be a deliberate edit rather than a fall-through.
		break;
	case deluge::storage::AcquireWalk::Navigate: {
		PresetNavigationResult result = loadInstrumentPresetUI.doPresetNavigation(
		    spec.offset, spec.oldInstrument, spec.outputType, spec.name, spec.dirPath, spec.availability);
		if (result.error == Error::NONE) {
			item = result.fileItem;
		}
		else {
			found.error = result.error;
		}
		break;
	}
	}
	if (item != nullptr) {
		found.existing = item->instrument;
		found.alreadyInSong = item->instrumentAlreadyInSong;
		found.name = (item->instrument != nullptr) ? item->instrument->name : item->getFilenameWithoutExtension();
		found.dirPath = Browser::currentDir;
		found.path = Browser::currentDir + "/" + item->getFilenameWithExtension();
	}
	Browser::emptyFileItems();
	load->walked_ = std::move(found);
	load->walkRunning_ = false;
}

void InstrumentLoad::echoNavigation() const {
	if (walked_.existing != nullptr) {
		view.displayOutputName(walked_.existing, walk_.blink);
	}
	else {
		view.drawOutputNameFromDetails(walk_.outputType, 0, 0, walked_.name.c_str(), walked_.name.empty(), false,
		                               walk_.blink);
	}
	deluge::hid::display::OLED::sendMainImage(); // Sorta cheating - bypassing the UI layered renderer.
}

StorageManager::StagedOpen InstrumentLoad::stage(const char* path, const char* firstTagName, const char* altTagName) {
	if (!stagingOpen_) {
		// Counted for the preset-load gate (PL-TIER, AQ-TIER): a document read, and whether it began on the storage
		// owner.
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

OutputType InstrumentLoad::outputTypeToParse() const {
	if (active_.kind == Kind::Acquire) {
		return walk_.outputType;
	}
	if (active_.kind == Kind::Revert) {
		return active_.revert.outputType;
	}
	return active_.target.loadingSynthToKitRow ? OutputType::SYNTH : Browser::outputTypeToLoad;
}

bool InstrumentLoad::stepStage() {
	const char* firstTagName = nullptr;
	const char* altTagName = nullptr;
	StorageManager::instrumentDocumentTags(outputTypeToParse(), &firstTagName, &altTagName);
	const std::string& path = (active_.kind == Kind::Acquire)  ? walked_.path
	                          : (active_.kind == Kind::Revert) ? active_.revert.path
	                                                           : active_.target.path;
	switch (stage(path.c_str(), firstTagName, altTagName)) {
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

void InstrumentLoad::beginSampleLoad(Instrument& instrument) {
	// Counted for the preset-load gate (PL-TIER, AQ-TIER): a sample load, and whether it began on the storage owner.
	sampleLoads_++;
	sampleLoadsOnOwner_ += deluge::storage::Owner::on_owner() ? 1 : 0;
	display->displayLoadingAnimationText("Loading", false, true);
	load_.emplace(deluge::storage::AsyncSampleLoad::forOutput(instrument, nullptr));
}

bool InstrumentLoad::stepParse() {
	Error error = Error::NONE;
	const bool acquire = active_.kind == Kind::Acquire;
	const bool revert = active_.kind == Kind::Revert;
	std::string name = acquire ? walked_.name : revert ? active_.revert.name : active_.target.name;
	std::string dirPath = acquire ? walked_.dirPath : revert ? active_.revert.dirPath : active_.target.dirPath;
	if (!acquire && active_.target.loadingSynthToKitRow) {
		error = StorageManager::parseSynthDrumDocument(currentSong, loadInstrumentPresetUI.instrumentClipToLoadFor,
		                                               &newDrum_);
	}
	else {
		InstrumentClip* clip = acquire  ? nullptr
		                       : revert ? active_.revert.clip
		                                : loadInstrumentPresetUI.instrumentClipToLoadFor;
		error = StorageManager::parseInstrumentDocument(currentSong, clip, outputTypeToParse(), false, &newInstrument_,
		                                                &name, &dirPath);
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

	if (newDrum_ != nullptr) {
		// Counted for the preset-load gate (PL-TIER): a sample load, and whether it began on the storage owner.
		sampleLoads_++;
		sampleLoadsOnOwner_ += deluge::storage::Owner::on_owner() ? 1 : 0;
		display->displayLoadingAnimationText("Loading", false, true);
		load_.emplace(deluge::storage::AsyncSampleLoad::forDrum(*newDrum_, nullptr));
	}
	else {
		if (acquire) {
			acquired_.instrument = newInstrument_;
			acquired_.loadedFromFile = true;
		}
		beginSampleLoad(*newInstrument_);
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
		// The Instrument is kept: swapped in or handed over with the holders that never answered left empty.
		if (progress.reason == Reason::Stalled) {
			giveUps_++;
		}
		display->displayError(Error::SD_CARD);
	}
	load_.reset();
	display->removeLoadingAnimation();
	if (active_.kind == Kind::Acquire) {
		return finish(Error::NONE);
	}
	phase_ = Phase::Swap;
	return true;
}

bool InstrumentLoad::stepSwap() {
	if (active_.kind == Kind::Revert) {
		loadInstrumentPresetUI.swapRevert(active_.revert, newInstrument_);
		newInstrument_ = nullptr; // the song's now
		return finish(Error::NONE);
	}
	Error error = Error::NONE;
	if (active_.target.loadingSynthToKitRow) {
		error = loadInstrumentPresetUI.swapKitRow(active_, newDrum_);
		newDrum_ = nullptr; // the kit's now, or discarded by the swap
	}
	else {
		Instrument* instrument = (decision_.reuse != nullptr) ? decision_.reuse : newInstrument_;
		error = loadInstrumentPresetUI.swapInstrument(active_, decision_, instrument);
		newInstrument_ = nullptr; // the song's now
	}
	if (error != Error::NONE) {
		return finish(error);
	}
	return finish(Error::NONE);
}

bool InstrumentLoad::stepMidiDef() {
	switch (stage(active_.definition.path.c_str(), "midiDevice", "")) {
	case StorageManager::StagedOpen::Deferred:
		return false;
	case StorageManager::StagedOpen::Failed:
		return finish(Error::FILE_CORRUPTED);
	case StorageManager::StagedOpen::Opened: {
		// The Instrument keeps the file it was read from: the definition browser opens on it next time.
		std::string fileName = active_.definition.path;
		const Error error =
		    StorageManager::parseMidiDeviceDefinitionDocument(active_.definition.instrument, &fileName, true);
		releaseStaging();
		return finish(error);
	}
	}
	return false;
}

void InstrumentLoad::abandon() {
	releaseStaging();
	if (load_.has_value()) {
		load_.reset();
		display->removeLoadingAnimation();
	}
	// A walk still out ends on its own; what it finds is not read (start() clears walkDispatched_).
	walkDispatched_ = false;
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

bool InstrumentLoad::finish(Error error, bool deliver) {
	lastError_ = error;
	const bool commit = active_.commit;
	const auto completion = active_.completion;
	const bool acquire = active_.kind == Kind::Acquire;
	const deluge::storage::PresetRequest preset = acquire ? active_.preset : deluge::storage::PresetRequest{};
	deluge::storage::AcquireResult acquired = acquired_;
	acquired.error = error;
	if (acquire && deliver && error == Error::NONE) {
		newInstrument_ = nullptr; // handed over: the gesture's follow-up adds it to the song, or deletes it
	}
	abandon();
	phase_ = Phase::Idle;
	std::optional<InstrumentLoadRequest> next = coalescer_.complete();
	if (commit) {
		openGate();
	}
	if (acquire && deliver) {
		if (walk_.walk == deluge::storage::AcquireWalk::Navigate) {
			originValid_ = false; // the song's Instrument is where the walk landed now
		}
		loadInstrumentPresetUI.deliverAcquisition(preset, acquired);
	}
	else if (completion != nullptr) {
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
