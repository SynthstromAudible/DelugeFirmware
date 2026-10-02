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
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound_drum.h"
#include "storage/file_item.h"
#include "util/functions.h"

#include <algorithm>

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

LoadTicket InstrumentLoad::request(const InstrumentLoadRequest& request) {
	if (const std::optional<InstrumentLoadRequest>& queued = coalescer_.queued();
	    queued.has_value() && neverSuperseded(*queued)) {
		// A queued creation, revert or definition is never replaced: superseding the middle of three quick new-track
		// presses would lose a track, and superseding a revert would leave the clip on what the user backed out of.
		// What comes after it is refused, and the gesture kept is the one the user made first.
		return {};
	}
	const LoadTicket ticket{.generation = nextGeneration_};
	nextGeneration_ = (nextGeneration_ == UINT32_MAX) ? 1 : nextGeneration_ + 1;
	if (phase_ != Phase::Idle && promotes(request, active_)) {
		// Select on the file a scroll-load is already loading: that load becomes the commit, rather than start again.
		// It is the commit's load now, so the scroll-load's ticket no longer names it.
		active_.commit = true;
		active_.completion = request.completion;
		active_.generation = ticket.generation;
		coalescer_.clear_queued();
		closeGate();
		return ticket;
	}
	if (request.commit) {
		closeGate();
	}
	InstrumentLoadRequest ticketed = request;
	ticketed.generation = ticket.generation;
	if (!coalescer_.request(ticketed)) {
		// Queued: the load in flight is cancelled at its next phase boundary, unless it is a creation.
		deluge::runtime::appLoop.request(jobId);
		return ticket;
	}
	start(coalescer_.current());
	routine();
	// The run above may have been made outside the loop's pass, where a time it asked for wakes nothing.
	deluge::runtime::appLoop.request(jobId);
	return ticket;
}

CancelResult InstrumentLoad::cancel(LoadTicket ticket) {
	if (!ticket.accepted()) {
		return CancelResult::Stale;
	}
	if (const std::optional<InstrumentLoadRequest>& queued = coalescer_.queued();
	    queued.has_value() && queued->generation == ticket.generation) {
		if (neverSuperseded(*queued)) {
			return CancelResult::Refused;
		}
		const bool queuedCommit = queued->commit;
		coalescer_.clear_queued();
		if (phase_ != Phase::Idle && !neverSuperseded(active_)) {
			// The load in flight was to be dropped for this one at its next phase boundary, so it goes with it:
			// cancelling the newest of a fast scroll leaves none of it loading.
			originValid_ = false;
			dropActive();
		}
		// A queued commit closed the gate as it was asked for; only a commit still in flight keeps it closed.
		if (queuedCommit && !(phase_ != Phase::Idle && active_.commit)) {
			openGate();
		}
		cancellations_++;
		return CancelResult::Cancelled;
	}
	if (phase_ == Phase::Idle || active_.generation != ticket.generation) {
		return CancelResult::Stale;
	}
	if (neverSuperseded(active_)) {
		return CancelResult::Refused;
	}
	if (coalescer_.queued().has_value()) {
		return CancelResult::Stale; // a newer request has replaced it, and drops it at its next phase boundary
	}
	// Where a navigate's walk landed is not where the clip is any more: the next navigate walks on from the clip.
	originValid_ = false;
	cancellations_++;
	dropActive();
	return CancelResult::Cancelled;
}

void InstrumentLoad::cancelBrowserLoads() {
	const std::optional<InstrumentLoadRequest>& queued = coalescer_.queued();
	if (queued.has_value() && queued->kind == Kind::Browse) {
		coalescer_.clear_queued();
	}
	if (phase_ == Phase::Idle || active_.kind != Kind::Browse) {
		return;
	}
	// An acquisition queued behind the browser's load still runs.
	dropActive();
}

void InstrumentLoad::dropActive() {
	const bool commit = active_.commit;
	// Releases the staging and drops the sample load before stepSamples() can tick it again, so a load cancelled
	// here never reaches the failed-load path, which keeps a partial Instrument and shows a card error.
	abandon();
	phase_ = Phase::Idle;
	if (commit) {
		openGate();
	}
	std::optional<InstrumentLoadRequest> next = coalescer_.complete();
	if (next.has_value()) {
		start(*next);
		deluge::runtime::appLoop.request(jobId);
	}
}

void InstrumentLoad::routine() {
	tickWalkHere();
	if (phase_ == Phase::Idle && currentSong != nullptr && currentSong->blankPresetPending) {
		// A blank song asks for its first preset here, on this tier: its builder runs before any tier exists (boot) or
		// before the song it builds is current (clear-song), so neither can ask. Cleared first: request() runs a pass
		// of its own.
		currentSong->blankPresetPending = false;
		(void)request({.kind = Kind::Acquire,
		               .preset = {.consumer = deluge::storage::PresetConsumer::BlankSongPreset,
		                          .outputType = OutputType::SYNTH,
		                          .dirPath = getInstrumentFolder(OutputType::SYNTH)}});
	}
	while (step()) {}
	armNextRun();
}

void InstrumentLoad::armNextRun() {
	using deluge::runtime::appLoop;
	if (walkUnticked_) {
		appLoop.request(jobId);
		return;
	}
	uint64_t next = deluge::runtime::kNever;
	if (walkRunning_) {
		// Every walk waits only on a directory open, whose answer raises WAKE_STORAGE and wakes the job. This deadline,
		// read from the browser's shared directory bound, is a watchdog only: the run that finds the open's bound over.
		const uint64_t deadline = Browser::directoryWaitDeadlineUs();
		next = (deadline != deluge::runtime::kNever) ? deadline : deluge::runtime::nowUs() + Browser::kDirectoryStallUs;
	}
	if ((phase_ == Phase::Stage || phase_ == Phase::MidiDef) && stagingOpen_) {
		next = std::min(next, StorageManager::stagingNextRunUs(staging_));
	}
	if (phase_ == Phase::Samples && load_.has_value()) {
		next = std::min(next, load_->nextTickUs());
	}
	if (next == 0) {
		appLoop.request(jobId);
	}
	else if (next != deluge::runtime::kNever) {
		appLoop.requestAt(jobId, next);
	}
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
	walkStarted_ = false;
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
	if (!walkStarted_) {
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
		// Every kind runs here now, on the interaction tier, a folder at a time into a listing of its own: the
		// Unlaunched search through PresetWalk, the slot name through SlotNameScan, the preset scroll through
		// PresetNavigation, and the change-type through ConfirmPreset. Nothing is dispatched.
		walkRunning_ = true;
		walkStarted_ = true;
		beginWalkHere();
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
	if (!walkRunning_) {
		return;
	}
	// Driven from HERE, not from stepWalk(), and that is not a detail. abandon() relies on the invariant "a walk
	// still out ends on its own", and a walk on this tier has no driver but routine(). cancel() and supersede()
	// set phase_ = Idle while a walk is out, and step() returns at Phase::Idle without ever reaching stepWalk(),
	// so a walk ticked only from there would be stranded: walkRunning_ stays true and every later walk queues
	// behind a search nobody is advancing.
	walkUnticked_ = false;
	bool waiting = false;
	switch (walkOut_.walk) {
	case deluge::storage::AcquireWalk::UnusedSlot:
		waiting = slot_scan_.tick();
		break;
	case deluge::storage::AcquireWalk::Navigate:
		waiting = navigation_.tick();
		break;
	case deluge::storage::AcquireWalk::Confirm:
		waiting = confirm_.tick();
		break;
	default:
		waiting = walk_machine_.tick();
		break;
	}
	if (waiting) {
		return;
	}
	finishWalkHere();
	walkRunning_ = false;
}

void InstrumentLoad::beginWalkHere() {
	walkUnticked_ = true;
	if (walkOut_.walk == deluge::storage::AcquireWalk::UnusedSlot) {
		// Counted for the preset-load gate (AQ-SLOT): a walk begun.
		slotScans_++;
		slot_scan_.arm(loadInstrumentPresetUI, walkOut_.dirPath, walkOut_.outputType);
		return;
	}
	if (walkOut_.walk == deluge::storage::AcquireWalk::Navigate) {
		// Counted for the preset-load gate (AQ-NAV-TIER): a walk begun.
		navigationsRun_++;
		navigation_.arm(loadInstrumentPresetUI, walkOut_);
		return;
	}
	if (walkOut_.walk == deluge::storage::AcquireWalk::Confirm) {
		// Counted for the preset-load gate (AQ-CHANGE-TIER): a walk begun.
		confirmsRun_++;
		confirm_.arm(loadInstrumentPresetUI, walkOut_);
		return;
	}
	// The search's folder goes straight to the walk, never through Browser::currentDir: reading it back through
	// the browser's directory would move whichever browser is open underneath the walk.
	walk_machine_.arm(loadInstrumentPresetUI, walkOut_.dirPath, walkOut_.includeSong ? currentSong : nullptr,
	                  walkOut_.outputType, walkOut_.availability);
}

void InstrumentLoad::finishWalkHere() {
	if (walkOut_.walk == deluge::storage::AcquireWalk::UnusedSlot) {
		// A name, or the error, and nothing to load.
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
	if (walkOut_.walk == deluge::storage::AcquireWalk::Navigate) {
		// The folder read off the walk, never Browser::currentDir.
		AcquireSnapshot found{};
		const auto result = navigation_.result();
		if (result.has_value()) {
			FileItem* item = *result;
			found.existing = item->instrument;
			found.alreadyInSong = item->instrumentAlreadyInSong;
			found.name = (item->instrument != nullptr) ? item->instrument->name : item->getFilenameWithoutExtension();
			found.dirPath = navigation_.dir();
			found.path = navigation_.dir() + "/" + item->getFilenameWithExtension();
		}
		else {
			found.error = result.error();
		}
		walked_ = std::move(found);
		return;
	}
	if (walkOut_.walk == deluge::storage::AcquireWalk::Confirm) {
		AcquireSnapshot found{};
		const auto result = confirm_.result();
		if (result.has_value()) {
			FileItem* item = *result;
			found.existing = item->instrument;
			found.alreadyInSong = item->instrumentAlreadyInSong;
			found.name = (item->instrument != nullptr) ? item->instrument->name : item->getFilenameWithoutExtension();
			found.dirPath = confirm_.dir();
			found.path = confirm_.dir() + "/" + item->getFilenameWithExtension();
		}
		else {
			found.error = result.error();
		}
		walked_ = std::move(found);
		return;
	}
	// The Unlaunched search.
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
		// The folder the SEARCH ended in, read off the walk's own listing, not Browser::currentDir: the walk
		// doesn't move the browser, so the browser's directory isn't an answer to where this preset lives.
		found.dirPath = walk_machine_.foundDir();
		found.path = walk_machine_.foundDir() + "/" + item->getFilenameWithExtension();
	}
	// No emptyFileItems() here: the items are in the WALK's listing, not the browser's, and clearing the
	// browser's would blank a list this search never touched.
	walked_ = std::move(found);
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
		// Counted for the preset-load gate (PL-TIER, AQ-TIER): a document read.
		documentReads_++;
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
	// Counted for the preset-load gate (PL-TIER, AQ-TIER): a sample load.
	sampleLoads_++;
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
		newInstrument_->setName(decision_.clonedName);
		newInstrument_->editedByUser = true;
	}

	if (newDrum_ != nullptr) {
		// Counted for the preset-load gate (PL-TIER): a sample load.
		sampleLoads_++;
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
	// A walk still out ends on its own; what it finds is not read (start() clears walkStarted_).
	walkStarted_ = false;
	if (newInstrument_ != nullptr) {
		// Parsed, never added to the song.
		currentSong->deleteOutput(newInstrument_);
		newInstrument_ = nullptr;
	}
	if (newDrum_ != nullptr) {
		currentSong->project.param_backup.deleteForModControllable(newDrum_);
		AudioEngine::retire(newDrum_);
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
