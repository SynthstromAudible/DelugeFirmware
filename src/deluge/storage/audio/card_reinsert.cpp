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

#include "storage/audio/card_reinsert.h"

#include "io/stream.hpp"
#include "libdeluge/clock.h"
#include "libdeluge/stream_io.h"
#include "model/sample/sample.h"
#include "storage/audio/audio_file.h"
#include "storage/audio/audio_file_manager.h"
#include "storage/byte_channel.h"

deluge::storage::CardReinsert cardReinsert;

namespace deluge::storage {

namespace {

/// The path this Sample's identity hangs on: the temp recording path while it has one, else the file
/// path. Unchanged from the blocking walk this replaces.
const char* pathOf(Sample& sample) {
	const char* path = sample.tempFilePathForRecording.c_str();
	return (*path != '\0') ? path : sample.filePath.c_str();
}

/// What one resident's check concluded.
enum class Verdict : uint8_t {
	Wait,         ///< No answer yet; ask again next tick.
	Present,      ///< The file is there.
	KnownAbsent,  ///< The file is definitely not there.
	Unestablished ///< The channel could not say. NOT absence.
};

} // namespace

void CardReinsert::arm() {
	if (phase_ == CardReinsertPhase::Walk) {
		// A second card came back mid-walk. Do not restart here: this resident may have a request in
		// flight, and its collect is keyed to a path that must be consumed before another is issued.
		rearm_ = true;
		return;
	}
	phase_ = CardReinsertPhase::Walk;
	cursor_ = 0;
	rearm_ = false;
	span_.reset();
}

bool CardReinsert::tick() {
	if (phase_ != CardReinsertPhase::Walk) {
		return false;
	}

	if (cursor_ >= static_cast<int32_t>(audioFileManager.audioFiles.size())) {
		walks_++;
		if (rearm_) {
			rearm_ = false;
			cursor_ = 0;
			span_.reset();
			return true;
		}
		phase_ = CardReinsertPhase::Done;
		return false;
	}

	AudioFile* file = audioFileManager.audioFiles[cursor_];
	if (file == nullptr) {
		cursor_++;
		return true;
	}

	// Nothing holds it: this is the opportunity to give the memory back. The cursor deliberately does
	// NOT advance -- the container closes up behind the removed element, which is what the blocking
	// loop's `e--` accounted for.
	if (!file->isProjectReferenced()) {
		audioFileManager.deleteUnusedAudioFileFromMemory(*file, cursor_);
		return true;
	}

	if (file->type != AudioFileType::SAMPLE) {
		cursor_++;
		return true;
	}

	auto& sample = *static_cast<Sample*>(file);
	const char* path = pathOf(sample);

	Verdict verdict = Verdict::Wait;
	if (!bytes::usingByteChannel()) {
		// Before the channel's task is running -- the boot path -- blocking is legal and is the only
		// thing that can answer. Same selector the rest of the storage tier uses.
		verdict =
		    deluge::io::Stream::open(path, DELUGE_STREAM_READ).has_value() ? Verdict::Present : Verdict::KnownAbsent;
	}
	else {
		bool present = false;
		DelugeStatus status = DELUGE_OK;
		const bytes::ByteIo io = bytes::collectExists(path, &present, &status);
		switch (io) {
		case bytes::ByteIo::Ready:
			// THREE-STATE, and this is the distinction that matters: a definite answer, either way.
			verdict = present ? Verdict::Present : Verdict::KnownAbsent;
			break;
		case bytes::ByteIo::Failed:
			// "Could not establish", which is NOT absence. Leave this sample's state alone rather than
			// condemning a file that may be perfectly fine -- see the class doc, and `f37a8ef41`.
			verdict = Verdict::Unestablished;
			break;
		case bytes::ByteIo::Unknown:
			(void)bytes::requestExists(path);
			span_.note(deluge_clock_now());
			break;
		case bytes::ByteIo::Pending:
			span_.note(deluge_clock_now());
			break;
		}
		if (verdict == Verdict::Wait && span_.elapsedUs >= kMaxUnproductiveUs) {
			// The card stopped answering for this one. Not absence either: give up on the RESIDENT, not
			// on the sample's playability.
			verdict = Verdict::Unestablished;
		}
	}

	switch (verdict) {
	case Verdict::Wait:
		return true; // Same resident next tick.

	case Verdict::Present:
		if (sample.unloadable) {
			// It just became loadable again: re-arm the background overview pre-scan so its waveform
			// overview gets rebuilt (the scan skips unloadable samples and may have gone idle) (#4460).
			audioFileManager.overviewScanAllDone = false;
		}
		sample.unloadable = false;
		break;

	case Verdict::KnownAbsent:
		sample.markAsUnloadable();
		break;

	case Verdict::Unestablished:
		unestablished_++;
		break;
	}

	cursor_++;
	span_.reset();
	return true;
}

} // namespace deluge::storage
