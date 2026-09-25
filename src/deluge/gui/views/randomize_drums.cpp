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

#include "gui/views/randomize_drums.h"

#include "libdeluge/clock.h" // deluge_clock_now
#include "libdeluge/file_directory.h"

#include "definitions_cxx.hpp"
#include "gui/ui/ui.h"
#include "gui/views/instrument_clip_view.h"
#include "hid/display/display.h"
#include "io/file.hpp"
#include "model/clip/instrument_clip.h"
#include "model/drum/drum.h"
#include "model/instrument/kit.h"
#include "model/note/note_row.h"
#include "model/song/song.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound_drum.h"
#include "storage/audio/audio_file_holder.h"
#include "storage/multi_range/multi_range.h"
#include "storage/owner.h"
#include "util/functions.h"

#include <cstring>
#include <iterator>
#include <string>

namespace deluge::gui {

RandomizeDrums randomizeDrums{};

namespace {

void releaseGate() {
	// Clear-song and song-load close the same mode: only the walk's own is released.
	if (currentUIMode == UI_MODE_LOADING_SONG_ESSENTIAL_SAMPLES) {
		currentUIMode = UI_MODE_NONE;
	}
}

} // namespace

void RandomizeDrums::request(const RandomizeRequest& request) {
	if (phase_ == Phase::Draining) {
		// A cancelled walk still waiting on its scan released the gate, so this press closed it again: nothing would
		// release it, so it is released here and the press is dropped.
		releaseGate();
		return;
	}
	if (phase_ != Phase::Idle) {
		return; // a running walk keeps the gate closed, so padAction() refuses the press before it gets here
	}
	current_ = request;
	phase_ = Phase::Start;
	routine();
}

void RandomizeDrums::cancelWalk() {
	if (phase_ == Phase::Idle || phase_ == Phase::Draining) {
		return;
	}
	load_.cancel();
	if (opRunning_) {
		// The scan still reads the row's folder: nothing may unbind the row's holder until it reports. Its result is
		// discarded.
		phase_ = Phase::Draining;
		releaseGate();
		return;
	}
	releaseGate();
	phase_ = Phase::Idle;
	opDispatched_ = false;
}

void RandomizeDrums::routine() {
	while (step()) {}
}

bool RandomizeDrums::step() {
	switch (phase_) {
	case Phase::Idle:
		return false;
	case Phase::Start:
		return stepStart();
	case Phase::ScanRow:
		return stepScanRow();
	case Phase::AwaitScan:
		return stepAwaitScan();
	case Phase::LoadRow:
		return stepLoadRow();
	case Phase::AwaitLoad:
		return stepAwaitLoad();
	case Phase::Finish:
		return stepFinish();
	case Phase::Draining: {
		// A cancelled walk may have an open outstanding. It must still be COLLECTED, because a
		// handle that arrives and is never adopted is a handle burned for good -- there are only
		// eight, and eight such leaks stop directory browsing working. Adopting into `dir` here
		// means its destructor closes it, which IS the discard.
		//
		// ⚠️ Deliberately NOT collectOrRequestDir: that re-requests on UNKNOWN, which is right for a
		// caller trying to OPEN and wrong for one trying to STOP -- it would keep re-opening a
		// cancelled walk's folder for ever.
		uint32_t handle = 0;
		DelugeStatus st = DELUGE_OK;
		const uint8_t answer = deluge_file_directory_collect(rowFolder_.c_str(), &handle, &st);
		if (answer == DELUGE_FILE_DIRECTORY_READY) {
			(void)deluge::io::Directory::adopt(handle); // closed on the spot by RAII
		}
		else if (answer == DELUGE_FILE_DIRECTORY_PENDING) {
			// Bounded: a cancelled walk must not wait for an answer for ever either.
			scanSpan_.note(deluge_clock_now());
			if (scanSpan_.elapsedUs <= deluge::storage::kMaxUnproductiveUs) {
				return false;
			}
		}
		// UNKNOWN or FAILED: nothing is outstanding for us, or it already ended.
		phase_ = Phase::Idle;
		opDispatched_ = false;
		return false;
	}
	}
	return false;
}

bool RandomizeDrums::stepStart() {
	cursor_ = 0;
	rowsRandomized_ = 0;
	lastName_.fill(0);
	phase_ = Phase::ScanRow;
	return true;
}

Drum* RandomizeDrums::nextRow() {
	const RandomizeRequest& r = current_;
	if (r.randomizeAll) {
		const int32_t nRows = std::ssize(r.instrumentClip->noteRows);
		while (cursor_ < nRows) {
			NoteRow& row = r.instrumentClip->noteRows[cursor_++];
			if (!row.muted && !row.hasNoNotes()) {
				return row.drum;
			}
		}
		return nullptr;
	}
	if (r.auditioning) {
		while (cursor_ < kDisplayHeight) {
			const int32_t y = cursor_++;
			if (!r.auditionPadIsPressed[y]) {
				continue;
			}
			NoteRow* row = r.instrumentClip->getNoteRowOnScreen(y, currentSong);
			if (row != nullptr) {
				return row->drum;
			}
		}
		return nullptr;
	}
	if (cursor_ == 0) {
		cursor_ = 1;
		return r.selectedDrum;
	}
	return nullptr;
}

bool RandomizeDrums::stepScanRow() {
	for (;;) {
		Drum* drum = nextRow();
		if (drum == nullptr) {
			// The rows are exhausted.
			phase_ = Phase::Finish;
			return true;
		}
		if (!InstrumentClipView::drumIsRandomizable(drum)) {
			continue;
		}
		auto* soundDrum = static_cast<SoundDrum*>(drum);
		AudioFileHolder* holder = soundDrum->sources[0].getRange(0)->getAudioFileHolder();
		const std::string& path = holder->filePath;
		const size_t slash = path.rfind('/');
		if (slash == std::string::npos) {
			continue; // a file at the card's root has no folder to pick from
		}
		rowDrum_ = soundDrum;
		rowHolder_ = holder;
		rowFolder_ = path.substr(0, slash);
		scan_ = {};
		// Per ROW: the walk scans one folder for each randomizable row, so a flag left set would
		// make the next row reuse this one's result. Nothing is dispatched -- collectOrRequestDir
		// issues the first request itself when it reports UNKNOWN.
		scanCollected_ = false;
		scanSpan_.reset();
		phase_ = Phase::AwaitScan;
		return false;
	}
}

bool RandomizeDrums::stepAwaitScan() {
	if (!scanCollected_) {
		std::optional<deluge::io::Directory> dir;
		deluge::io::Status status = deluge::io::Status::OK;
		switch (deluge::io::collectOrRequestDir(rowFolder_.c_str(), &dir, &status)) {
		case deluge::io::DirOpen::Pending:
			scanSpan_.note(deluge_clock_now());
			if (scanSpan_.elapsedUs > deluge::storage::kMaxUnproductiveUs) {
				// The open never answered. The existing body reports it as a folder that would not
				// open, which is what a user sees either way.
				scan_.error = Error::SD_CARD;
				break;
			}
			return false;
		case deluge::io::DirOpen::Failed:
			scan_.error = Error::SD_CARD;
			break;
		case deluge::io::DirOpen::Ready:
			// FS-free from here: the whole snapshot is drained in this tick.
			drainScan(*dir);
			break;
		}
		scanCollected_ = true;
	}
	if (scan_.error != Error::NONE) {
		// The folder could not be opened.
		end(Error::SD_CARD);
		return false;
	}
	if (scan_.fileCount == 0) {
		// No audio file to land on: this row is left as it is.
		phase_ = Phase::ScanRow;
		return true;
	}
	phase_ = Phase::LoadRow;
	return true;
}

bool RandomizeDrums::stepLoadRow() {
	// Counted for the sample-claim gate (RD-TIER): a row load that began on the storage owner.
	if (deluge::storage::Owner::on_owner()) {
		loadsOnOwner_++;
	}
	AudioEngine::stopAnyPreviewing();
	rowDrum_->killAllVoices();
	rowHolder_->setAudioFile(nullptr);
	rowHolder_->filePath = rowFolder_;
	rowHolder_->filePath.append("/");
	rowHolder_->filePath.append(scan_.filename.data());
	load_.request(
	    deluge::storage::SampleDemand{
	        .holder = rowHolder_,
	        .manuallySelected = true,
	        .clusterLoadInstruction = CLUSTER_ENQUEUE,
	    },
	    /*context=*/nullptr, 0);
	phase_ = Phase::AwaitLoad;
	return true;
}

bool RandomizeDrums::stepAwaitLoad() {
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
		end(Error::SD_CARD);
		return false;
	}
	if (outcome->result == Result::Empty) {
		end(outcome->error != Error::NONE ? outcome->error : Error::SD_CARD);
		return false;
	}

	// The drum is named after the file it landed on, without its extension.
	lastName_ = scan_.filename;
	char* dot = std::strrchr(lastName_.data(), '.');
	if (dot != nullptr) {
		*dot = '\0';
	}
	rowDrum_->drumName = lastName_.data();
	current_.kit->beenEdited();
	rowsRandomized_++;
	phase_ = Phase::ScanRow;
	return true;
}

bool RandomizeDrums::stepFinish() {
	switch (rowsRandomized_) {
	case 0:
		break; // nothing eligible had files to land on: no popup, as before
	case 1:
		display->displayPopup(lastName_.data());
		break;
	default:
		display->displayPopup(current_.randomizeAll ? "Randomized active rows" : "Randomized selected rows");
		break;
	}
	end(Error::NONE);
	return false;
}

void RandomizeDrums::end(Error error) {
	if (error != Error::NONE) {
		display->displayError(error);
	}
	load_.cancel();
	releaseGate();
	phase_ = Phase::Idle;
	opDispatched_ = false;
}

void RandomizeDrums::drainScan(deluge::io::Directory& dir) {
	// `scansOnOwner_` is deliberately NOT incremented here and NOT deleted: RD-TIER reporting it as
	// zero is the evidence that this scan left the storage owner.
	scansRun_++;
	ScanResult& scan = scan_;
	for (;;) {
		auto entry = dir.read();
		if (!entry.has_value() || !entry->has_value()) {
			break;
		}
		if ((*entry)->is_directory || !isAudioFilename((*entry)->name)) {
			continue;
		}
		if (random(scan.fileCount++) == 0) { // reservoir sampling, k = 1
			std::strncpy(scan.filename.data(), (*entry)->name, scan.filename.size() - 1);
			scan.filename.back() = '\0';
		}
	}
	scan.done = true;
}

} // namespace deluge::gui
