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

#include "gui/ui/browser/folder_import.h"

#include "libdeluge/clock.h" // deluge_clock_now

#include "definitions_cxx.hpp"
#include "gui/l10n/l10n.h"
#include "gui/ui/browser/sample_browser.h"
#include "gui/ui/sound_editor.h"
#include "gui/ui/ui.h"
#include "gui/views/instrument_clip_view.h"
#include "hid/display/display.h"
#include "io/debug/log.h"
#include "io/file.hpp"
#include "memory/general_memory_allocator.h"
#include "model/clip/instrument_clip.h"
#include "model/instrument/instrument.h"
#include "model/instrument/kit.h"
#include "model/model_stack.h"
#include "model/sample/sample.h"
#include "model/song/song.h"
#include "modulation/automation/auto_param.h"
#include "modulation/params/param_manager.h"
#include "modulation/params/param_set.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound.h"
#include "processing/sound/sound_drum.h"
#include "processing/source.h"
#include "storage/multi_range/multisample_range.h"
#include "storage/owner.h"
#include "util/functions.h"

#include <cmath>
#include <cstring>
#include <iterator>

extern bool shouldInterpretNoteNames;
extern bool octaveStartsFromA;

namespace params = deluge::modulation::params;

namespace deluge::gui {

FolderImport folderImport{};

namespace {

#define NOTE_CHECK_ERROR_MARGIN 0.75

void closeGate() {
	currentUIMode = UI_MODE_LOADING_SONG_ESSENTIAL_SAMPLES;
}

void openGate() {
	// Clear-song and song-load close the same mode: only the import's own is released.
	if (currentUIMode == UI_MODE_LOADING_SONG_ESSENTIAL_SAMPLES) {
		currentUIMode = UI_MODE_NONE;
	}
}

bool pitchGreaterOrEqual(Sample* a, Sample* b) {
	return (a->midiNote >= b->midiNote);
}

bool filenameGreaterOrEqual(Sample* a, Sample* b) {
	shouldInterpretNoteNames = true;
	octaveStartsFromA = false;
	return (strcmpspecial(a->filePath.c_str(), b->filePath.c_str()) >= 0);
}

void sortSamples(bool (*sortFunction)(Sample*, Sample*), int32_t numSamples, Sample*** sortAreas, int32_t* readArea,
                 int32_t* writeArea) {
	int32_t numComparing = 1;

	// Go through various iterations of numComparing
	while (numComparing < numSamples) {

		// And now, for this selected comparison size, do a number of comparisions
		for (int32_t whichComparison = 0; whichComparison * numComparing * 2 < numSamples; whichComparison++) {

			int32_t a = numComparing * (whichComparison * 2);
			int32_t b = numComparing * (whichComparison * 2 + 1);

			for (int32_t writeI = numComparing * whichComparison * 2;
			     writeI < numComparing * (whichComparison + 1) * 2 && writeI < numSamples; writeI++) {
				Sample* sampleA = sortAreas[*readArea][a];
				Sample* sampleB = sortAreas[*readArea][b];

				Sample** sampleWrite = &sortAreas[*writeArea][writeI];

				if (b < numComparing * (whichComparison + 1) * 2 && b < numSamples
				    && (a >= numComparing * (whichComparison * 2 + 1) || sortFunction(sampleA, sampleB))) {
					*sampleWrite = sampleB;
					b++;
				}
				else {
					*sampleWrite = sampleA;
					a++;
				}
			}
		}

		*readArea = 1 - *readArea;
		*writeArea = 1 - *writeArea;
		numComparing *= 2;
	}
}

int32_t getNumTimesIncorrectSampleOrderSeen(int32_t numSamples, Sample** samples) {

	int32_t timesIncorrectOrderSeen = 0;

	for (int32_t s = 1; s < numSamples; s++) {
		Sample* sampleA = samples[s - 1];
		Sample* sampleB = samples[s];

		if (sampleB->midiNote < sampleA->midiNote) {
			timesIncorrectOrderSeen++;
		}
	}

	D_PRINT("timesIncorrectOrderSeen: %d", timesIncorrectOrderSeen);

	return timesIncorrectOrderSeen;
}

} // namespace

void FolderImport::request(FolderImportRequest request) {
	if (phase_ != Phase::Idle) {
		return;
	}
	request_ = std::move(request);
	names_.clear();
	samples_.clear();
	listError_ = Error::NONE;
	cursor_ = 0;
	numCharsInPrefix_ = 65535;
	doingSingleCycle_ = true; // Until we find a sample too long
	commonMIDINote_ = -2;
	sortArea_ = nullptr;
	numSamples_ = 0;

	AudioEngine::stopAnyPreviewing();
	display->displayLoadingAnimationText("Working");
	closeGate();
	phase_ = Phase::List;
	routine();
}

void FolderImport::cancel() {
	if (phase_ == Phase::Idle || phase_ == Phase::Draining) {
		return;
	}
	load_.cancel();
	scratch_.setAudioFile(nullptr);
	display->removeWorkingAnimation();
	openGate();
	if (opRunning_) {
		// The operation still reads a sample: nothing may release it until the operation reports.
		phase_ = Phase::Draining;
		return;
	}
	releaseSamples();
	phase_ = Phase::Idle;
	opDispatched_ = false;
}

void FolderImport::routine() {
	while (step()) {}
}

bool FolderImport::step() {
	switch (phase_) {
	case Phase::Idle:
		return false;
	case Phase::List:
		// Nothing is dispatched: the folder's open goes through the directory channel, and
		// collectOrRequestDir issues the first request itself when it reports UNKNOWN.
		listCollected_ = false;
		listSpan_.reset();
		phase_ = Phase::AwaitList;
		return false;
	case Phase::AwaitList:
		return stepAwaitList();
	case Phase::LoadFile:
		return stepLoadFile();
	case Phase::AwaitLoad:
		return stepAwaitLoad();
	case Phase::Prepare:
		return stepPrepare();
	case Phase::Detect:
		if (cursor_ >= numSamples_) {
			phase_ = Phase::Sort;
			return true;
		}
		opSample_ = sortArea_[cursor_];
		opLimited_ = false;
		dispatch(&FolderImport::runPitchOp);
		phase_ = Phase::AwaitDetect;
		return false;
	case Phase::AwaitDetect:
		if (!opReported()) {
			return false;
		}
		cursor_++;
		phase_ = Phase::Detect;
		return true;
	case Phase::Sort:
		return stepSort();
	case Phase::Redetect:
		return stepRedetect();
	case Phase::AwaitRedetect:
		if (!opReported()) {
			return false;
		}
		// If didn't work, see if we can pretend we're looking for 1 octave higher, where there'd probably be a
		// harmonic too. This can help if the fundamental isn't visible - it worked on Leo's piano samples before I
		// realised that those harmonics had just been deleted by my aggressive use of a threshold
		if (opSample_->midiNote == MIDI_NOTE_ERROR) {
			opMinFreqHz_ *= 2;
			opMaxFreqHz_ *= 2;
			D_PRINTLN("pretending an octave up...");
			dispatch(&FolderImport::runPitchOp);
			phase_ = Phase::AwaitOctaveUp;
			return false;
		}
		prevNote_ = opSample_->midiNote;
		cursor_++;
		phase_ = Phase::Redetect;
		return true;
	case Phase::AwaitOctaveUp:
		if (!opReported()) {
			return false;
		}
		if (opSample_->midiNote != MIDI_NOTE_ERROR) {
			opSample_->midiNote -= 12;
			prevNote_ = opSample_->midiNote;
		}
		cursor_++;
		phase_ = Phase::Redetect;
		return true;
	case Phase::FinishKit:
	case Phase::FinishMulti: {
		const bool kit = phase_ == Phase::FinishKit;
		openGate();
		const bool success = kit ? finishKit() : finishMulti();
		sortArea_ = nullptr; // freed by the finish, as the importers always have
		if (!success) {
			display->removeWorkingAnimation();
		}
		samples_.clear();
		names_.clear();
		phase_ = Phase::Idle;
		if (request_.completion != nullptr) {
			request_.completion(success);
		}
		return false;
	}
	case Phase::Draining:
		if (!opRunning_) {
			releaseSamples();
			phase_ = Phase::Idle;
			opDispatched_ = false;
		}
		return false;
	}
	return false;
}

bool FolderImport::stepAwaitList() {
	if (!listCollected_) {
		std::optional<deluge::io::Directory> dir;
		deluge::io::Status status = deluge::io::Status::OK;
		switch (deluge::io::collectOrRequestDir(request_.folder.c_str(), &dir, &status)) {
		case deluge::io::DirOpen::Pending:
			listSpan_.note(deluge_clock_now());
			if (listSpan_.elapsedUs > deluge::storage::kMaxUnproductiveUs) {
				// The open never answered. Giving up beats an import that waits for ever.
				listError_ = Error::SD_CARD;
				break;
			}
			return false;
		case deluge::io::DirOpen::Failed:
			listError_ = Error::SD_CARD;
			break;
		case deluge::io::DirOpen::Ready:
			// FS-free from here: the whole snapshot is drained in this tick, exactly as the owner
			// arm drained it.
			drainListing(*dir);
			break;
		}
		listCollected_ = true;
	}
	if (listError_ != Error::NONE) {
		abort(Error::SD_CARD);
		return false;
	}
	dirWithSlash_ = request_.folder;
	if (!dirWithSlash_.empty()) {
		dirWithSlash_.append("/");
	}
	cursor_ = 0;
	phase_ = Phase::LoadFile;
	return true;
}

bool FolderImport::stepLoadFile() {
	if (cursor_ >= std::ssize(names_)) {
		// Ok, the samples are now all in memory.
		D_PRINT("loaded from folder: %d", static_cast<int32_t>(samples_.size()));
		phase_ = Phase::Prepare;
		return true;
	}
	// Counted for the sample-claim gate (IM-TIER): a file load begun on the storage owner.
	if (deluge::storage::Owner::on_owner()) {
		loadsOnOwner_++;
	}
	scratch_.setAudioFile(nullptr);
	scratch_.filePath = dirWithSlash_;
	scratch_.filePath.append(names_[cursor_]);
	load_.request(
	    deluge::storage::SampleDemand{
	        .holder = &scratch_,
	        .clusterLoadInstruction = CLUSTER_DONT_LOAD,
	    },
	    /*context=*/nullptr, 0);
	phase_ = Phase::AwaitLoad;
	return true;
}

bool FolderImport::stepAwaitLoad() {
	using Result = deluge::storage::LatestSampleLoad<uint32_t>::Result;
	using Reason = deluge::storage::AsyncSampleLoad::Reason;

	const auto outcome = load_.tick();
	if (!outcome.has_value()) {
		return false; // still loading
	}
	if (outcome->result == Result::Failed) {
		if (outcome->reason == Reason::Stalled) {
			giveUps_++;
		}
		abort(Error::SD_CARD);
		return false;
	}
	if (outcome->result == Result::Empty) {
		abort(outcome->error != Error::NONE ? outcome->error : Error::SD_CARD);
		return false;
	}

	auto* newSample = static_cast<Sample*>(scratch_.audioFile);
	const char* name = names_[cursor_].c_str();

	// Keep investigating if there's a common prefix to all files in this folder, against the file the browser was on.
	if (!samples_.empty()) {
		const char* previouslyViewedFilename = request_.previouslyViewedFilename.c_str();
		for (int32_t i = 0; i < numCharsInPrefix_; i++) {
			if (!name[i] || name[i] != previouslyViewedFilename[i]) {
				numCharsInPrefix_ = i;
				break;
			}
		}
	}

	// The import's own reason, taken before the scratch holder lets its go.
	newSample->addReason();
	scratch_.setAudioFile(nullptr);
	samples_.push_back(newSample);

	if (newSample->getLengthInMSec() > 20) {
		doingSingleCycle_ = false;
	}

	if (commonMIDINote_ == -2) {
		commonMIDINote_ = newSample->midiNoteFromFile;
	}
	else if (commonMIDINote_ >= -1) {
		if (commonMIDINote_ != newSample->midiNoteFromFile) {
			commonMIDINote_ = -3;
		}
	}

	cursor_++;
	phase_ = Phase::LoadFile;
	return true;
}

bool FolderImport::stepPrepare() {
	numSamples_ = std::ssize(samples_);

	// If just one file, there's no prefix.
	if (numSamples_ <= 1) {
		numCharsInPrefix_ = 0;
	}
	prefixAndDirLength_ = std::ssize(dirWithSlash_) + numCharsInPrefix_;

	// If all samples were tagged with the same MIDI note, we get suspicious and delete them.
	const bool discardingMIDINoteFromFile = (numSamples_ > 1 && commonMIDINote_ >= 0);

	sortArea_ = (Sample**)deluge::memory::alloc_fast(numSamples_ * sizeof(Sample*) * 2);
	if (!sortArea_) {
		abort(Error::INSUFFICIENT_RAM);
		return false;
	}
	for (int32_t s = 0; s < numSamples_; s++) {
		if (discardingMIDINoteFromFile) {
			samples_[s]->midiNoteFromFile = -1;
		}
		sortArea_[s] = samples_[s];
	}

	cursor_ = 0;
	phase_ = (request_.kind == FolderImportKind::Multisamples) ? Phase::Detect : Phase::Sort;
	return true;
}

bool FolderImport::stepSort() {
	sortAreas_[0] = sortArea_;
	sortAreas_[1] = &sortArea_[numSamples_];
	readArea_ = 0;
	writeArea_ = 1;

	// Sort by filename
	sortSamples(filenameGreaterOrEqual, numSamples_, sortAreas_, &readArea_, &writeArea_);

	if (request_.kind == FolderImportKind::Kit) {
		settleSortArea();
		phase_ = Phase::FinishKit;
		return true;
	}

	int32_t badnessRatingFromC = getNumTimesIncorrectSampleOrderSeen(numSamples_, sortAreas_[readArea_]);
	if (!badnessRatingFromC) {
		settleSortArea(); // If that's all fine, we're done
		phase_ = Phase::FinishMulti;
		return true;
	}

	// If the Samples are in precisely the wrong order, something's happened like we've been interpretting a dash (-) in
	// the filenames as a minus sign. Just reverse the order.
	if (badnessRatingFromC == numSamples_ - 1) {
		for (int32_t s = 0; s < (numSamples_ >> 1); s++) {
			Sample* temp = sortAreas_[readArea_][s];
			sortAreas_[readArea_][s] = sortAreas_[readArea_][numSamples_ - 1 - s];
			sortAreas_[readArea_][numSamples_ - 1 - s] = temp;
		}
		settleSortArea();
		phase_ = Phase::FinishMulti;
		return true;
	}

	// Ok, we're here, the samples are optimally ordered by file, but, the pitch is out.
	D_PRINTLN("sample order by file finalized");

	float prevNote = sortAreas_[readArea_][0]->midiNote; // May be MIDI_NOTE_ERROR

	for (int32_t s = 1; s < numSamples_; s++) {

		prevNote += 1;

		Sample* thisSample = sortAreas_[readArea_][s];

		float noteHere = thisSample->midiNote;
		if (noteHere == MIDI_NOTE_ERROR) {
			continue;
		}

		if (noteHere < prevNote - NOTE_CHECK_ERROR_MARGIN) {

			// Ok, this one's lower than the last. Who's wrong?

			// If we correct backwards, how many would we have to redo?
			int32_t numIncorrectBackwards = 0;

			for (int32_t t = s - 1; t >= 0; t--) {
				Sample* thatSample = sortAreas_[readArea_][t];
				if (thatSample->midiNote == MIDI_NOTE_ERROR) {
					continue;
				}
				if (thatSample->midiNote < noteHere + t - s + NOTE_CHECK_ERROR_MARGIN) {
					break;
				}

				numIncorrectBackwards++; // If we're here, this note would have to be marked as incorrect
			}

			// Ok, and if we corrected forwards, how many?
			int32_t numIncorrectForwards = 1;

			for (int32_t t = s + 1; t < numSamples_; t++) {
				Sample* thatSample = sortAreas_[readArea_][t];
				if (thatSample->midiNote == MIDI_NOTE_ERROR) {
					continue;
				}
				if (thatSample->midiNote >= prevNote + t - s - NOTE_CHECK_ERROR_MARGIN) {
					break;
				}

				numIncorrectForwards++;
			}

			// If we decide to correct backwards...
			if (numIncorrectBackwards < numIncorrectForwards) {
				for (int32_t t = s - 1; t >= 0; t--) {
					Sample* thatSample = sortAreas_[readArea_][t];
					if (thatSample->midiNote == MIDI_NOTE_ERROR) {
						continue;
					}
					if (thatSample->midiNote < noteHere + t - s + NOTE_CHECK_ERROR_MARGIN) {
						break;
					}

					thatSample->midiNote = MIDI_NOTE_ERROR;
				}
			}

			// Or if we decide to correct forwards...
			else {
				thisSample->midiNote = MIDI_NOTE_ERROR;
				for (int32_t t = s + 1; t < numSamples_; t++) {
					Sample* thatSample = sortAreas_[readArea_][t];
					if (thatSample->midiNote == MIDI_NOTE_ERROR) {
						continue;
					}
					if (thatSample->midiNote >= prevNote + t - s - NOTE_CHECK_ERROR_MARGIN) {
						break;
					}

					thatSample->midiNote = MIDI_NOTE_ERROR;
				}
				continue; // Keep the old prevNote
			}
		}

		prevNote = noteHere;
	}

	// Ok, we've now marked a bunch of samples as having the incorrect pitch, which we know because it doesn't match the
	// filename order. So go through and correct them, one step each, now that we've got a better idea of the range they
	// should fit in.
	prevNote_ = MIDI_NOTE_ERROR;
	cursor_ = 0;
	phase_ = Phase::Redetect;
	return true;
}

bool FolderImport::stepRedetect() {
	if (cursor_ >= numSamples_) {
		// We've done all the correcting we can. Now re-sort by pitch
		sortSamples(pitchGreaterOrEqual, numSamples_, sortAreas_, &readArea_, &writeArea_);
		settleSortArea();
		phase_ = Phase::FinishMulti;
		return true;
	}

	const int32_t s = cursor_;
	Sample* thisSample = sortAreas_[readArea_][s];

	if (thisSample->midiNote != MIDI_NOTE_ERROR) {
		prevNote_ = thisSample->midiNote;
		cursor_++;
		return true;
	}

	float nextNote = 999;
	for (int32_t t = s + 1; t < numSamples_; t++) {
		Sample* thatSample = sortAreas_[readArea_][t];
		if (thatSample->midiNote != MIDI_NOTE_ERROR) {
			nextNote = thatSample->midiNote - t + s;
			break;
		}
	}

	prevNote_ += 1;

	// Ok, we got a range to search within
	float minFreqHz;
	if (prevNote_ < 0) {
		minFreqHz = 20;
	}
	else {
		minFreqHz = powf(2, ((prevNote_ - NOTE_CHECK_ERROR_MARGIN) - 69) / 12) * 440;
	}

	float maxFreqHz;
	if (nextNote == 999) {
		maxFreqHz = 10000;
	}
	else {
		maxFreqHz = powf(2, ((nextNote + NOTE_CHECK_ERROR_MARGIN) - 69) / 12) * 440;
	}

	// If maxFreqHz is too low, it's likely no use to us, and the whole mission's probably messed up, so just call this
	// one an error. Hopefully I can improve this one day. See Michael B's Mellotron samples
	if (maxFreqHz < minFreqHz) {
		thisSample->midiNote = MIDI_NOTE_ERROR;
		cursor_++;
		return true;
	}

	D_PRINT("redoing, limited to %d to %d", minFreqHz, maxFreqHz);

	opSample_ = thisSample;
	opLimited_ = true;
	opMinFreqHz_ = minFreqHz;
	opMaxFreqHz_ = maxFreqHz;
	dispatch(&FolderImport::runPitchOp);
	phase_ = Phase::AwaitRedetect;
	return false;
}

void FolderImport::settleSortArea() {
	// All sorted! If the sorted values have ended up in the secondary area, move them back to the first
	if (readArea_ == 1) {
		memcpy(sortArea_, &sortArea_[numSamples_], numSamples_ * sizeof(Sample*));
	}
}

bool FolderImport::finishMulti() {
	const int32_t numSamplesLoaded = numSamples_;
	int32_t numSamples = numSamples_;
	Sample** sortArea = sortArea_;
	const bool doingSingleCycle = doingSingleCycle_;

	D_PRINTLN("loaded and sorted samples");

	// Delete all but first pre-existing range
	int32_t oldNumRanges = std::ssize(soundEditor.currentSource->ranges);
	for (int32_t i = oldNumRanges - 1; i >= 1; i--) {
		soundEditor.currentSound->deleteMultiRange(soundEditor.currentSourceIndex, i);
	}

	// If we now want more than one range, be efficient by getting our array of ranges to pre-allocate all the memory
	// it's going to use
	if (numSamples > 1) {
		soundEditor.currentSound->killAllVoices();
		AudioEngine::audioRoutineLocked = true;
		bool success = soundEditor.currentSource->ranges.reserveExtra(numSamples - 1).has_value();
		AudioEngine::audioRoutineLocked = false;

		if (!success) {
			delugeDealloc(sortArea);
			for (int32_t s = 0; s < numSamplesLoaded; s++) {
				Sample* thisSample = samples_[s];
#if ALPHA_OR_BETA_VERSION
				if (!thisSample->isProjectReferenced()) {
					FREEZE_WITH_ERROR("E215"); // I put this here to try and catch an E004 Luc got
				}
#endif
				thisSample->removeReason("E393"); // Remove that temporary reason we added above
			}
			display->displayError(Error::INSUFFICIENT_RAM);
			return false;
		}
	}

	soundEditor.setCurrentMultiRange(0);

	AudioEngine::audioRoutineLocked = false;

	// If we've ended up with some samples a whole octave higher than the others, this may be in error
	int32_t whichSampleIsAnOctaveUp = 0;

	if (numSamples) {
		float prevNote = sortArea[0]->midiNote;
		for (int32_t s = 1; s < numSamples; s++) {
			Sample* thisSample = sortArea[s];
			float noteHere = thisSample->midiNote;
			if (noteHere >= prevNote + 12.5 && noteHere <= prevNote + 13.5) {
				if (whichSampleIsAnOctaveUp) {
					goto skipOctaveCorrection;
				}
				else {
					whichSampleIsAnOctaveUp = s;
				}
			}
			else {
				// If there are other intervals of more than a semitone, we can't really take it for granted what's
				// going on, so get out
				if (noteHere >= prevNote + 1.85) {
					D_PRINTLN("aaa");
					D_PRINTLN("%.2f", noteHere - prevNote);
					goto skipOctaveCorrection;
				}
			}

			prevNote = noteHere;
		}

		if (whichSampleIsAnOctaveUp) {
			D_PRINTLN("correcting octaves");
			// Correct earlier ones?
			if (whichSampleIsAnOctaveUp * 2 < numSamples) {
				for (int32_t s = 0; s < whichSampleIsAnOctaveUp; s++) {
					sortArea[s]->midiNote += 12;
				}
			}

			// Or correct later ones?
			else {
				for (int32_t s = whichSampleIsAnOctaveUp; s < numSamples; s++) {
					sortArea[s]->midiNote -= 12;
				}
			}
		}
	}

skipOctaveCorrection:

	int32_t rangeIndex = 0; // Keep this different to the sample index, just in case we need to skip a sample because it
	                        // has the same pitch as a previous one. Skipping a range would leave our rangeArray with
	                        // unused space allocated, but that's ok

	int32_t lastTopNote = MIDI_NOTE_ERROR;

	int32_t totalMSec = 0;
	int32_t numWithFileLoopPoints = 0;
	int32_t numWithResultingLoopEndPoints = 0;

	if (soundEditor.currentSource->oscType != OscType::SAMPLE) {
		soundEditor.currentSound->killAllVoices();
		soundEditor.currentSource->setOscType(OscType::SAMPLE);
	}

	D_PRINTLN("creating ranges");

	for (int32_t s = 0; s < numSamples; s++) {

		Sample* thisSample = sortArea[s];

		if (thisSample->midiNote == MIDI_NOTE_ERROR) {
			D_PRINTLN("dismissing 1 sample for which pitch couldn't be detected");
			// TODO: shouldn't we remove a reason here?
			continue;
		}

		int32_t topNote = 32767;

		if (s < numSamples - 1) {
			Sample* nextSample = sortArea[s + 1];

			float midPoint = (thisSample->midiNote + nextSample->midiNote) * 0.5;
			topNote = midPoint; // Round down
			if (topNote <= lastTopNote) {
				D_PRINTLN("skipping sample cos %d <= %d", topNote, lastTopNote);
				// TODO: shouldn't we remove a reason here?
				continue;
			}
		}

		MultisampleRange* range;
		if (rangeIndex == 0) {
			range = (MultisampleRange*)soundEditor.currentMultiRange;
		}
		else {
#if ALPHA_OR_BETA_VERSION
			if (soundEditor.currentSource->ranges.currentElementSize() != sizeof(MultisampleRange)) {
				FREEZE_WITH_ERROR("E431");
			}
#endif
			range = (MultisampleRange*)soundEditor.currentSource->ranges.insertMultiRange(
			    rangeIndex); // We know it's gonna succeed
		}

		D_PRINT("top note:  %d", topNote);

		range->topNote = topNote;

		range->sampleHolder.filePath = thisSample->filePath;
		range->sampleHolder.setAudioFile(thisSample, soundEditor.currentSource->sampleControls.isCurrentlyReversed(),
		                                 true);
		bool rangeCoversJustOneNote = (topNote == lastTopNote + 1);
		range->sampleHolder.setTransposeAccordingToSamplePitch(false, doingSingleCycle, rangeCoversJustOneNote,
		                                                       topNote);

		totalMSec += thisSample->getLengthInMSec();
		if (thisSample->fileLoopEndSamples) {
			numWithFileLoopPoints++;
		}
		if (range->sampleHolder.loopEndPos) {
			numWithResultingLoopEndPoints++;
		}

		if (ALPHA_OR_BETA_VERSION && !thisSample->isProjectReferenced()) {
			FREEZE_WITH_ERROR("E216"); // I put this here to try and catch an E004 Luc got
		}
		thisSample->removeReason("E394"); // Remove that temporary reason we added above

		rangeIndex++;
		lastTopNote = topNote;
	}

	numSamples = rangeIndex;

	if (!numSamples) {
		display->displayPopup(
		    deluge::l10n::get(deluge::l10n::String::STRING_FOR_ERROR_CREATING_MULTISAMPLED_INSTRUMENT));
		return false;
	}

	D_PRINTLN("distinct ranges: %d", numSamples);

	delugeDealloc(sortArea);

	sampleBrowser.audioFileIsNowSet();

	int32_t averageMSec = totalMSec / numSamples;

	// If source files had loop points set...
	if (numWithFileLoopPoints * 2 >= numSamples) {

		// If this led to an actual loop end pos, with more waveform after it, and the sample's not too long, we can do
		// a ONCE
		if (numWithResultingLoopEndPoints * 2 >= numSamples && averageMSec < 2002) {
			soundEditor.currentSource->repeatMode = SampleRepeatMode::ONCE;
		}
		else {
			soundEditor.currentSource->repeatMode = SampleRepeatMode::LOOP;
		}
	}

	// Or if no loop points set...
	else {
		// If 2 seconds or less, set play mode to ONCE. Otherwise, cut
		soundEditor.currentSource->repeatMode = (averageMSec < 2002) ? SampleRepeatMode::ONCE : SampleRepeatMode::CUT;
	}

	soundEditor.setCurrentMultiRange(numSamples >> 1);

	sampleBrowser.exitAndNeverDeleteDrum();
	getCurrentInstrument()->beenEdited();

	display->removeWorkingAnimation();
	return true;
}

bool FolderImport::finishKit() {
	const int32_t numSamples = numSamples_;
	Sample** sortArea = sortArea_;
	const int32_t prefixAndDirLength = prefixAndDirLength_;

	Kit* kit = getCurrentKit();
	SoundDrum* firstDrum = (SoundDrum*)soundEditor.currentSound;

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	{
		ModelStackWithThreeMainThings* modelStack = soundEditor.getCurrentModelStack(modelStackMemory);

		for (int32_t s = 0; s < numSamples; s++) {

			Sample* thisSample = sortArea[s];

			SoundDrum* drum;
			Source* source;
			MultiRange* range;

			// If the first sample...
			if (s == 0) {

				drum = firstDrum;
				source = &drum->sources[0];
				range = source->getOrCreateFirstRange();
				if (!range) {
getOut:
					display->displayError(Error::INSUFFICIENT_RAM);
					return false;
				}

				// Ensure osc type is "sample". For the later drums, calling setupAsSample() does this same thing
				if (soundEditor.currentSource->oscType != OscType::SAMPLE) {
					soundEditor.currentSound->killAllVoices();
					soundEditor.currentSource->setOscType(OscType::SAMPLE);
				}

				ParamCollectionSummary* summary = modelStack->paramManager->getPatchedParamSetSummary();
				ParamSet* paramSet = (ParamSet*)summary->paramCollection;
				int32_t paramId = params::LOCAL_OSC_A_VOLUME + soundEditor.currentSourceIndex;
				ModelStackWithAutoParam* modelStackWithParam =
				    modelStack->addParam(paramSet, summary, paramId, &paramSet->params[paramId]);

				// Reset osc volume, if it's not automated
				if (!modelStackWithParam->autoParam->isAutomated()) {
					modelStackWithParam->autoParam->setCurrentValueWithNoReversionOrRecording(modelStackWithParam,
					                                                                          2147483647);
				}

				drum->killAllVoices();
			}

			// Or, for subsequent samples...
			else {

				// Make the Drum and its ParamManager

				ParamManagerForTimeline paramManager;
				Error error = paramManager.setupWithPatching();
				if (error != Error::NONE) {
					goto getOut;
				}

				void* drumMemory = deluge::memory::alloc_fast(sizeof(SoundDrum));
				if (!drumMemory) {
					goto getOut;
				}

				drum = new (drumMemory) SoundDrum();
				source = &drum->sources[0];

				range = source->getOrCreateFirstRange();
				if (!range) {
					drum->~SoundDrum();
					delugeDealloc(drumMemory);
					goto getOut;
				}

				Sound::initParams(&paramManager);

				kit->addDrum(drum);
				drum->setupAsSample(&paramManager);
				drum->nameIsDiscardable = true;
				currentSong->backUpParamManager(drum, getCurrentClip(), &paramManager, true);
			}

			AudioFileHolder* holder = range->getAudioFileHolder();
			holder->setAudioFile(nullptr);
			holder->filePath = thisSample->filePath;
			holder->setAudioFile(thisSample, source->sampleControls.isCurrentlyReversed(), true);

			sampleBrowser.autoDetectSideChainSending(drum, source, thisSample->filePath.c_str());

			std::string newName;
			newName = &thisSample->filePath.c_str()[prefixAndDirLength];
			Error error = Error::NONE;
			if (error == Error::NONE) {

				char const* newNameChars = newName.c_str();
				char const* dotAddress = strrchr(newNameChars, '.');
				if (dotAddress) {
					int32_t dotPos = (uintptr_t)dotAddress - (uintptr_t)newNameChars;
					newName.resize(dotPos);
				}

				if (kit->getDrumFromName(newName.c_str())) {
					error = kit->makeDrumNameUnique(&newName, 2);
					if (error != Error::NONE) {
						goto skipNameStuff;
					}
				}

				drum->drumName = newName.c_str();
			}
skipNameStuff:

			source->repeatMode =
			    (thisSample->getLengthInMSec() < 2002) ? SampleRepeatMode::ONCE : SampleRepeatMode::CUT;

#if ALPHA_OR_BETA_VERSION
			if (!thisSample->isProjectReferenced()) {
				FREEZE_WITH_ERROR("E217"); // I put this here to try and catch an E004 Luc got
			}
#endif
			thisSample->removeReason("E395");
		}

		delugeDealloc(sortArea);
	}

	// Make NoteRows for all these new Drums
	getCurrentKit()->resetDrumTempValues();
	firstDrum->noteRowAssignedTemp = 1;
	ModelStackWithTimelineCounter* modelStack = (ModelStackWithTimelineCounter*)modelStackMemory;
	getCurrentInstrumentClip()->assignDrumsToNoteRows(modelStack);

	getCurrentInstrument()->beenEdited();

	sampleBrowser.exitAndNeverDeleteDrum();
	uiNeedsRendering(&instrumentClipView);
	display->removeWorkingAnimation();
	return true;
}

void FolderImport::releaseSamples() {
	// Remove reasons from any samples we loaded in just before
	for (Sample* thisSample : samples_) {
#if ALPHA_OR_BETA_VERSION
		if (!thisSample->isProjectReferenced()) {
			FREEZE_WITH_ERROR("E213"); // I put this here to try and catch an E004 Luc got
		}
#endif
		thisSample->removeReason("E392"); // Remove that temporary reason we added
	}
	samples_.clear();
	names_.clear();
	if (sortArea_ != nullptr) {
		delugeDealloc(sortArea_);
		sortArea_ = nullptr;
	}
}

void FolderImport::abort(Error error) {
	load_.cancel();
	scratch_.setAudioFile(nullptr);
	releaseSamples();
	display->displayError(error);
	end(false);
}

void FolderImport::end(bool success) {
	display->removeWorkingAnimation();
	openGate();
	phase_ = Phase::Idle;
	opDispatched_ = false;
	if (request_.completion != nullptr) {
		request_.completion(success);
	}
}

void FolderImport::dispatch(void (*fn)(void*)) {
	opFn_ = fn;
	// Marked before it is queued, so an operation that runs at once still clears it.
	opRunning_ = true;
	opDispatched_ = deluge::storage::Owner::run(fn, this);
	if (!opDispatched_) {
		opRunning_ = false;
	}
}

bool FolderImport::opReported() {
	if (!opDispatched_) {
		dispatch(opFn_);
		return false;
	}
	if (opRunning_) {
		return false;
	}
	opDispatched_ = false;
	return true;
}

void FolderImport::drainListing(deluge::io::Directory& dir) {
	// `listingsOnOwner_` is deliberately NOT incremented here and NOT deleted: IM-TIER reporting it
	// as zero is the evidence that this listing left the storage owner.
	for (;;) {
		auto entry = dir.read();
		if (!entry.has_value() || !entry->has_value()) {
			break; // an error, or the end of the folder
		}
		if ((*entry)->name[0] == '.' || (*entry)->is_directory || !isAudioFilename((*entry)->name)) {
			continue; // dot entries, folders, and anything that's not an audio file
		}
		names_.emplace_back((*entry)->name);
	}
}

void FolderImport::runPitchOp(void* self) {
	auto* import = static_cast<FolderImport*>(self);
	// Counted for the sample-claim gate (IM-TIER): a pitch detection on the storage owner.
	if (deluge::storage::Owner::on_owner()) {
		import->pitchOnOwner_++;
	}
	if (import->opLimited_) {
		import->opSample_->workOutMIDINote(import->doingSingleCycle_, import->opMinFreqHz_, import->opMaxFreqHz_,
		                                   false);
	}
	else {
		import->opSample_->workOutMIDINote(import->doingSingleCycle_);
	}
	import->opRunning_ = false;
}

} // namespace deluge::gui
