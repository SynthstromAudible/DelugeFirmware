/*
 * Copyright © 2014-2023 Synthstrom Audible Limited
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
#include "gui/ui/save/save_song_ui.h"
#include "definitions_cxx.hpp"
#include "gui/context_menu/overwrite_file.h"
#include "gui/l10n/l10n.h"
#include "gui/ui/audio_recorder.h"
#include "hid/display/display.h"
#include "hid/led/indicator_leds.h"
#include "hid/led/pad_leds.h"
#include "io/debug/log.h"
#include "io/file.hpp"
#include "libdeluge/file_bytes.h"
#include "libdeluge/file_io.h"
#include "libdeluge/file_write.h"
#include "model/sample/sample.h"
#include "model/settings/runtime_feature_settings.h"
#include "model/song/song.h"
#include "storage/audio/audio_file_manager.h"
#include "storage/byte_channel.h"
#include "storage/existence_policy.h"
#include "storage/flash_storage.h"
#include "storage/owner.h"
#include "storage/readback_check.h"
#include "storage/storage_manager.h"
#include "storage/wave_table/wave_table.h"
#include "sync/sd_access.h"
#include "util/functions.h"
#include "util/string.h"
#include "util/try.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>

using namespace deluge;
using namespace gui;

SaveSongUI saveSongUI{};

SaveSongUI::SaveSongUI() {
	filePrefix = "SONG";
	title = "Save song";
}

bool SaveSongUI::opened() {
	outputTypeToLoad = OutputType::NONE;

	// Grab screenshot of song, for saving, before qwerty drawn
	memcpy(PadLEDs::imageStore, PadLEDs::image, sizeof(PadLEDs::image));

	bool success = SaveUI::opened(); // Clears enteredText
	if (!success) {                  // In this case, an error will have already displayed.
doReturnFalse:
		renderingNeededRegardlessOfUI(); // Because unlike many UIs we've already gone and drawn the QWERTY interface on
		                                 // the pads.
		return false;
	}

	Error error;

	std::string searchFilename;
	searchFilename = currentSong->name;
	if (!searchFilename.empty()) {
		if (writeJsonFlag) {
			searchFilename.append(".Json");
			error = Error::NONE;
		}
		else {
			searchFilename.append(".XML");
			error = Error::NONE;
		}
		if (error != Error::NONE) {
			display->displayError(error);
			goto doReturnFalse;
		}
	}
	currentFolderIsEmpty = false;

	currentDir = currentSong->dirPath;

	// TODO: create folder if doesn't exist.

	// The listing (and the LED-blink tail in onBrowserOpened()) happens async: dispatch it and
	// return optimistically. Failure goes through the base Browser::onListingFailed()
	// (displayError + close()) once the listing completes.
	beginListing(
	    {.action = ListingAction::Open, .direction = 0, .filenameToStartAt = searchFilename, .defaultDir = "SONGS"});

	return true;
}

void SaveSongUI::onBrowserOpened() {
	indicator_leds::setLedState(IndicatorLED::SYNTH, false);
	indicator_leds::setLedState(IndicatorLED::KIT, false);
	indicator_leds::setLedState(IndicatorLED::MIDI, false);

	indicator_leds::setLedState(IndicatorLED::CROSS_SCREEN_EDIT, false);
	indicator_leds::setLedState(IndicatorLED::CLIP_VIEW, false);
	indicator_leds::setLedState(IndicatorLED::SCALE_MODE, false);

	indicator_leds::blinkLed(IndicatorLED::SESSION_VIEW);

	focusRegained();
	// do this after focus regained, otherwise the first scroll starts
	// from the beginning instead of showing the incremented number
	enteredTextEditPos = 0; // enteredText.getLength();
}

void SaveSongUI::focusRegained() {
	collectingSamples = false;
	return SaveUI::focusRegained();
}

bool SaveSongUI::planTransfers(std::vector<PlannedTransfer>& into, bool collectingSamples,
                               const std::string& songAlternatePath, int32_t alternateDirLength) {
	for (AudioFile* audioFile : audioFileManager.audioFiles) {
		if (!audioFile->isProjectReferenced()) {
			continue;
		}

		PlannedTransfer entry;
		entry.file = audioFile;

		// A recording still at its temporary location has to be moved into place. The move CLEARS the
		// temporary path, which is what the copy below then consults -- so both the move and the
		// clearing are recorded here rather than done.
		if (audioFile->type == AudioFileType::SAMPLE) {
			auto& sample = *static_cast<Sample*>(audioFile);
			if (!sample.tempFilePathForRecording.empty()) {
				entry.renameTemp = true;
				entry.renameFrom = sample.tempFilePathForRecording;
				entry.renameTo = audioFile->filePath;
				entry.clearTempPath = true;
			}
		}

		// Or the file needs copying to or from an "alt" location -- either a collect-media save, or a
		// file imported from such a folder. The obscure combination matters: a collect-media save can
		// ALSO have just moved (or failed to move) a recording out of its temporary location.
		if (collectingSamples || !audioFile->loadedFromAlternatePath.empty()) {

			// Saving under the same name and folder, collecting, and it already came from the alternate
			// location: there is nothing to do.
			if (collectingSamples && !audioFile->loadedFromAlternatePath.empty()
			    && deluge::string::caselessEquals(currentDir, currentSong->dirPath)
			    && deluge::string::caselessEquals(enteredText, currentSong->name)) {
				if (entry.renameTemp) {
					into.push_back(std::move(entry)); // the move above still has to happen
				}
				continue;
			}

			// The source, in both the worlds TRANSFER might find itself in. If the rename above
			// succeeds the temporary file is gone and `filePath` is the source; if it fails the
			// temporary file is still there and is the source -- which is what the old code expressed
			// by reading a field the rename had or had not cleared.
			if (!audioFile->loadedFromAlternatePath.empty()) {
				entry.copyFromAfterRename = audioFile->loadedFromAlternatePath;
				entry.copyFromIfRenameFailed = audioFile->loadedFromAlternatePath;
			}
			else {
				entry.copyFromAfterRename = audioFile->filePath;
				entry.copyFromIfRenameFailed = entry.renameTemp ? entry.renameFrom : audioFile->filePath;
			}

			// The destination, and the renaming that goes with it.
			std::string pathForNaming = audioFile->filePath;
			bool needToPretendLoadedAlternate = false;

			if (collectingSamples) {
				// A recording gets a random suffix, so that re-saving does not collide with the copy a
				// previous save made.
				const char* normalFilePath = pathForNaming.c_str();
				if (!memcasecmp(normalFilePath, "SAMPLES/RECORD/REC", 18)
				    || !memcasecmp(normalFilePath, "SAMPLES/RESAMPLE/REC", 20)
				    || !memcasecmp(normalFilePath, "SAMPLES/CLIPS/REC", 17)) {
					const char* slashAddr = strrchr(normalFilePath, '/');
					const int32_t slashPos = static_cast<int32_t>(slashAddr - normalFilePath);
					const int32_t fileNamePos = slashPos + 1;

					if (static_cast<int32_t>(pathForNaming.size()) - fileNamePos == 12
					    && !strcasecmp(normalFilePath + fileNamePos + 8, ".WAV")) {
						char buffer[11];
						buffer[0] = '_';
						buffer[6] = '.';
						buffer[7] = 'W';
						buffer[8] = 'A';
						buffer[9] = 'V';
						buffer[10] = 0;
						seedRandom();
						for (int32_t i = 1; i < 6; i++) {
							const int32_t rand = random(35);
							buffer[i] =
							    (rand < 10) ? static_cast<char>('0' + rand) : static_cast<char>('A' + (rand - 10));
						}
						// COMPUTED, not applied: the vector is sorted by this field and searched by
						// bisection, so rewriting it now would break every lookup until the transfers
						// finish.
						entry.newFilePath = pathForNaming.substr(0, fileNamePos + 8) + buffer;
						pathForNaming = entry.newFilePath;
						// The file will exist only in the alternate location, so it has to be marked as
						// having come from there -- or a later collect-media save would copy it again.
						needToPretendLoadedAlternate = true;
					}
				}

				// SEEDED with the song's alternate folder, not empty: setupAlternateAudioFilePath
				// APPENDS to this string and uses `alternateDirLength` to know where its own prefix
				// ends. Handing it a fresh string leaves it building a path with no root, which
				// reaches the filesystem as an empty name and panics it outright.
				std::string destination = songAlternatePath;
				if (!memcasecmp(pathForNaming.c_str(), "SAMPLES/", 8)) {
					const Error error =
					    audioFileManager.setupAlternateAudioFilePath(destination, alternateDirLength, pathForNaming);
					if (error != Error::NONE) {
						return false;
					}
				}
				else {
					destination += getFileNameFromEndOfPath(pathForNaming.c_str());
				}
				if (destination.empty()) {
					// Never hand an empty name to the card: it does not fail, it panics the filesystem.
					return false;
				}
				entry.copyTo = destination;
				if (needToPretendLoadedAlternate) {
					entry.newAlternatePath = destination;
				}
			}
			else {
				entry.copyTo = pathForNaming;
				// A successful copy back into the main location means the file is no longer only in the
				// alternate one.
				entry.clearAlternatePath = true;
			}

			entry.copy = true;
		}

		if (entry.renameTemp || entry.copy) {
			into.push_back(std::move(entry));
		}
	}

	// Nothing above writes to any AudioFile or to audioFiles. That is not tidiness: the vector is
	// sorted by filePath and search() binary-searches it, so a path rewritten here would break the
	// ordering for every lookup between now and the end of the transfers. SC-ATOMIC in the
	// sample-collect gate is what holds this true.
	return true;
}

bool SaveSongUI::beginUnstagedSave(const deluge::storage::SaveRequest& request) {
	if (currentSong->hasAnyPendingNextOverdubs()) {
		display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_OVERDUBS_PENDING));
		return false;
	}
	display->displayLoadingAnimationText("Saving");

	// The target name is checked on a later tick, off the storage owner. Nothing is planned and no
	// sample is copied until the answer says the name may be written: the collection copies media into
	// the new song's folder BEFORE the document is written, so learning at the write that the name was
	// taken would be too late.
	existsRequest_ = request;
	collectStallTicks_ = 0;
	collectPhase_ = CollectPhase::EXISTS;
	return true;
}

void SaveSongUI::runExistsPhase() {
	using deluge::storage::bytes::ByteIo;

	const char* path = existsRequest_.filePath.c_str();
	const bool onOwner = deluge::storage::Owner::on_owner() || !deluge::storage::Owner::started();

	if (onOwner) {
		// Blocking is legal here, and a channel request issued from a context that will not yield could
		// never be answered.
		++documentOwnerServedOps_;
		++documentSteps_;
		resolveExists(StorageManager::fileExists(path));
		return;
	}

	// Off the owner after boot, so the byte channel is here: the Rust BSP always has it, and a C++-only build
	// never starts the owner, so it never reaches this line.
	++offOwnerDocumentTicks_;
	bool present = false;
	DelugeStatus status = DELUGE_OK;
	switch (deluge::storage::bytes::collectExists(path, &present, &status)) {
	case ByteIo::Unknown:
		if (deluge::storage::bytes::requestExists(path) == ByteIo::Failed) {
			// A path the channel cannot key will never be answered: "could not tell", which is refused.
			resolveExists(std::unexpected(deluge::io::Status::PARAM));
			return;
		}
		if (!deferCollection()) {
			abandonExists();
		}
		return;
	case ByteIo::Pending:
		if (!deferCollection()) {
			abandonExists();
		}
		return;
	case ByteIo::Failed:
		++documentChannelServedOps_;
		++documentSteps_;
		// NOT absence. save_action() refuses an undeterminable answer rather than writing over a file
		// whose absence was never confirmed.
		resolveExists(std::unexpected(deluge::io::to_status(status)));
		return;
	case ByteIo::Ready:
		++documentChannelServedOps_;
		++documentSteps_;
		resolveExists(std::expected<bool, deluge::io::Status>(present));
		return;
	}
}

void SaveSongUI::abandonExists() {
	collectPhase_ = CollectPhase::NONE;
	display->removeLoadingAnimation();
	display->displayError(Error::SD_CARD);
	endUnstagedDispatch(existsRequest_, false);
}

void SaveSongUI::resolveExists(const std::expected<bool, deluge::io::Status>& presence) {
	collectStallTicks_ = 0;
	// By value: the dispatch ends below, and ending it may start another save that reuses the member.
	const deluge::storage::SaveRequest request = existsRequest_;

	switch (deluge::storage::save_action(presence, request.mayOverwrite)) {
	case deluge::storage::SaveAction::Refuse:
		// The card did not answer. Writing here would overwrite a file whose absence was never
		// confirmed -- see existence_policy.h. Write nothing.
		collectPhase_ = CollectPhase::NONE;
		display->removeLoadingAnimation();
		display->displayError(Error::SD_CARD);
		endUnstagedDispatch(request, false);
		return;
	case deluge::storage::SaveAction::Occupied:
		collectPhase_ = CollectPhase::NONE;
		context_menu::overwriteFile.currentSaveUI = this;
		if (!context_menu::overwriteFile.setupAndCheckAvailability()) { // Always available.
			display->removeLoadingAnimation();
			display->displayError(Error::UNSPECIFIED);
			endUnstagedDispatch(request, false);
			return;
		}
		display->removeWorkingAnimation();
		openUI(&context_menu::overwriteFile);
		endUnstagedDispatch(request, true);
		return;
	case deluge::storage::SaveAction::Write:
		break;
	}

	// We might want to copy some samples around - either because we're "collecting" them to a folder, or
	// because they were loaded in from a collected folder and we now need to put them in the main samples folder.
	std::string newSongAlternatePath;
	Error error = audioFileManager.setupAlternateAudioFileDir(newSongAlternatePath, request.dirPath.c_str(),
	                                                          request.enteredText.c_str());
	if (error == Error::NONE) {
		newSongAlternatePath.append("/");
		const int32_t dirPathLengthNew = static_cast<int32_t>(newSongAlternatePath.size());
		// Decide everything first, then perform it a tick at a time. The list of audio files is sorted by
		// path and searched by bisection, and the transfers rewrite paths -- so nothing is applied until
		// every transfer has finished.
		transfers_.clear();
		if (!planTransfers(transfers_, request.collectingSamples, newSongAlternatePath, dirPathLengthNew)) {
			error = Error::UNSPECIFIED;
		}
	}
	if (error != Error::NONE) {
		collectPhase_ = CollectPhase::NONE;
		display->removeLoadingAnimation();
		display->displayError(error);
		endUnstagedDispatch(request, false);
		return;
	}

	// Write covers both "nothing there" and "the user confirmed an overwrite"; only the latter has a file
	// to unlink afterwards.
	pendingFilePath_ = request.filePath;
	pendingFileAlreadyExisted_ = request.mayOverwrite;
	anyErrorMovingTempFiles_ = false;
	transferCursor_ = 0;
	renameIssued_ = false;
	copyIssued_ = false;
	renameSucceeded_ = true;
	collectPhase_ = CollectPhase::TRANSFER;
	// "Dealt with": the collection has started, and finishes on later ticks.
	endUnstagedDispatch(request, true);
}

namespace {
/// How many consecutive deferrals one collection tolerates without progress. A bound, not a timeout:
/// an unbounded retry is not patience, it is a wedge that presents as the device having hung.
constexpr int32_t kMaxCollectStallTicks = 600;
} // namespace

void SaveSongUI::collectRoutine() {
	switch (collectPhase_) {
	case CollectPhase::NONE:
		return;
	case CollectPhase::EXISTS:
		runExistsPhase();
		return;
	case CollectPhase::TRANSFER:
		runTransferPhase();
		return;
	case CollectPhase::APPLY:
		applyTransfers();
		transfers_.clear();
		collectStallTicks_ = 0;
		collectPhase_ = CollectPhase::FINISH;
		return;
	case CollectPhase::FINISH:
		// The document write is no longer a blocking owner call, so it is no longer dispatched: the
		// machine it starts picks an arm per step.
		collectPhase_ = CollectPhase::NONE;
		(void)finishSaveAfterCollection();
		return;
	}
}

bool SaveSongUI::deferCollection() {
	if (++collectStallTicks_ > kMaxCollectStallTicks) {
		return false;
	}
	return true;
}

void SaveSongUI::abandonCollection() {
	// NOTHING is applied. Every decided path stays decided and unwritten, so what is in memory still
	// matches what is on the card -- which is the whole reason the decisions were kept apart from the
	// transfers.
	collectPhase_ = CollectPhase::NONE;
	transfers_.clear();
	display->removeLoadingAnimation();
	display->displayError(Error::UNSPECIFIED);
}

void SaveSongUI::runTransferPhase() {
	using deluge::storage::bytes::ByteIo;

	if (transferCursor_ >= transfers_.size()) {
		collectPhase_ = CollectPhase::APPLY;
		return;
	}
	PlannedTransfer& entry = transfers_[transferCursor_];

	const bool onOwner = deluge::storage::Owner::on_owner() || !deluge::storage::Owner::started();

	if (onOwner) {
		// Blocking is legal here, and asking the channel would be worse than pointless: a request
		// issued from a context that will not yield can never be answered.
		++transferOwnerServedOps_;
		if (!transferOneSynchronously()) {
			abandonCollection();
			return;
		}
		++transferCursor_;
		renameIssued_ = false;
		copyIssued_ = false;
		renameSucceeded_ = true;
		collectStallTicks_ = 0;
		return;
	}

	// Past this point every transfer goes through the channel, from the interaction tier. THIS is the
	// rung's claim, and the number a gate can read it from. Counting the on-owner case here too would
	// have made the number say "off the owner" about work that ran ON it.
	++offOwnerTransferTicks_;

	DelugeStatus status = DELUGE_OK;

	// The move out of a temporary location comes first, because whether it SUCCEEDS is what decides
	// where the copy below reads from.
	if (entry.renameTemp && !renameIssued_) {
		switch (deluge::storage::bytes::collectRename(entry.renameTo.c_str(), &status)) {
		case ByteIo::Unknown:
			// Ask -- and honour the answer: a path that cannot be keyed reports Failed, and discarding
			// that leaves this asking a channel that will never reply.
			if (deluge::storage::bytes::requestRename(entry.renameFrom.c_str(), entry.renameTo.c_str())
			    == ByteIo::Failed) {
				renameIssued_ = true;
				renameSucceeded_ = false;
				anyErrorMovingTempFiles_ = true;
				return;
			}
			if (!deferCollection()) {
				abandonCollection();
			}
			return;
		case ByteIo::Pending:
			if (!deferCollection()) {
				abandonCollection();
			}
			return;
		case ByteIo::Failed:
			// NOT fatal, and deliberately so: the old code warned, left the recording where it was, and
			// carried on copying from there.
			renameIssued_ = true;
			renameSucceeded_ = false;
			anyErrorMovingTempFiles_ = true;
			collectStallTicks_ = 0;
			return;
		case ByteIo::Ready:
			renameIssued_ = true;
			renameSucceeded_ = true;
			collectStallTicks_ = 0;
			return;
		}
	}

	if (entry.copy) {
		const std::string& from = renameSucceeded_ ? entry.copyFromAfterRename : entry.copyFromIfRenameFailed;
		switch (deluge::storage::bytes::collectCopy(entry.copyTo.c_str(), &status)) {
		case ByteIo::Unknown:
			if (deluge::storage::bytes::requestCopy(from.c_str(), entry.copyTo.c_str()) == ByteIo::Failed) {
				abandonCollection();
				return;
			}
			if (!deferCollection()) {
				abandonCollection();
			}
			return;
		case ByteIo::Pending:
			if (!deferCollection()) {
				abandonCollection();
			}
			return;
		case ByteIo::Failed:
			// A failed COPY does abandon the save. Collect-media promises the song's folder holds
			// everything it references; half of that, believed whole, is worse than a visible error.
			abandonCollection();
			return;
		case ByteIo::Ready:
			++transferChannelServedOps_;
			break;
		}
	}

	// This entry is done.
	++transferCursor_;
	renameIssued_ = false;
	copyIssued_ = false;
	renameSucceeded_ = true;
	collectStallTicks_ = 0;
}

bool SaveSongUI::transferOneSynchronously() {
	PlannedTransfer& entry = transfers_[transferCursor_];

	if (entry.renameTemp) {
		// A failed move is NOT fatal: the recording stays where it is and the copy reads from there,
		// which is what the old code expressed by consulting a field the move had or had not cleared.
		StorageManager::buildPathToFile(entry.renameTo.c_str());
		deluge_file_invalidate_cache();
		const auto renamed = deluge::io::rename(entry.renameFrom, entry.renameTo);
		renameSucceeded_ = renamed.has_value();
		if (!renameSucceeded_) {
			anyErrorMovingTempFiles_ = true;
		}
	}

	if (!entry.copy) {
		return true;
	}

	const std::string& from = renameSucceeded_ ? entry.copyFromAfterRename : entry.copyFromIfRenameFailed;
	auto source = deluge::io::File::open(from.c_str(), DELUGE_FILE_READ);
	if (!source) {
		return false;
	}
	auto created = StorageManager::createFile(entry.copyTo.c_str(), false);
	if (!created) {
		// Already there from a previous save is SUCCESS, not failure -- the old loop said exactly
		// this, and treating it as an error would abandon perfectly good saves.
		return created.error() == Error::FILE_ALREADY_EXISTS;
	}

	std::array<std::byte, 4096> buffer{};
	while (true) {
		auto read = source->read(std::span<std::byte>(buffer));
		if (!read) {
			return false;
		}
		const size_t got = read->size();
		if (got == 0) {
			break;
		}
		auto written = created->write(std::span<const std::byte>(buffer.data(), got));
		if (!written || *written != got) {
			return false;
		}
		if (got < buffer.size()) {
			break;
		}
	}
	(void)created->close();
	(void)source->close();
	return true;
}

void SaveSongUI::applyTransfers() {
	for (PlannedTransfer& entry : transfers_) {
		const bool changesPath = !entry.newFilePath.empty();
		const bool changesAlternate = !entry.newAlternatePath.empty() || entry.clearAlternatePath;
		if (entry.clearTempPath && entry.file->type == AudioFileType::SAMPLE && renameSucceeded_) {
			static_cast<Sample*>(entry.file)->tempFilePathForRecording.clear();
		}
		if (!changesPath && !changesAlternate) {
			continue;
		}

		// THIS ORDER AND NO OTHER. searchForExactObject bisects on the CURRENT filePath, so locating
		// must happen while the vector is still ordered by it. Mutating first searches for the new path
		// in a vector ordered by the old one and returns -1, silently losing the file from the index.
		int32_t i = -1;
		if (changesPath) {
			i = audioFileManager.audioFiles.searchForExactObject(entry.file);
			if (i >= 0) {
				audioFileManager.audioFiles.erase(audioFileManager.audioFiles.begin() + i);
			}
		}
		if (changesPath) {
			entry.file->filePath = entry.newFilePath;
		}
		if (!entry.newAlternatePath.empty()) {
			entry.file->loadedFromAlternatePath = entry.newAlternatePath;
		}
		else if (entry.clearAlternatePath) {
			entry.file->loadedFromAlternatePath.clear();
		}
		if (changesPath && i >= 0) {
			(void)audioFileManager.audioFiles.insertElement(entry.file);
		}
	}
}

namespace {
/// How many consecutive deferrals one document step tolerates without progress. A bound, not a
/// timeout: an unbounded retry is not patience, it is a wedge that presents as the device hanging.
constexpr int32_t kMaxDocStallTicks = 600;
/// The TEMP + four-digit name space. Exhausting it fails the save; it does not keep asking.
constexpr int32_t kMaxTempNameAttempts = 10000;
} // namespace

bool SaveSongUI::docOnOwner() {
	return deluge::storage::Owner::on_owner() || !deluge::storage::Owner::started();
}

bool SaveSongUI::docMayUseWriteChannel() {
	return !docOnOwner() && deluge_file_write_async_active();
}

bool SaveSongUI::deferDocument() {
	return ++docStallTicks_ <= kMaxDocStallTicks;
}

void SaveSongUI::abandonDocument(Error error) {
	// Nothing destructive has run unless UNLINK completed, and UNLINK runs only after the temp file
	// has been written AND verified -- so on the overwrite path the user's existing song is still
	// there. That ordering is the whole reason the temp dance survives this conversion.
	releaseSongWriter();
	docPhase_ = DocPhase::NONE;
	display->removeLoadingAnimation();
	display->displayError(error);
}

void SaveSongUI::docRoutine() {
	switch (docPhase_) {
	case DocPhase::NONE:
		return;
	case DocPhase::SERIALIZE:
		runSerializePhase();
		return;
	case DocPhase::TEMPNAME:
		runTempNamePhase();
		return;
	case DocPhase::WRITE:
		runWritePhase();
		return;
	case DocPhase::VERIFY:
		runVerifyPhase();
		return;
	case DocPhase::UNLINK:
		runUnlinkPhase();
		return;
	case DocPhase::RENAME:
		runRenamePhase();
		return;
	case DocPhase::FINISH:
		runDocFinishPhase();
		return;
	}
}

void SaveSongUI::runSerializePhase() {
	// No card contact whatsoever. This is the half of a save that never needed the owner.
	const uint32_t capacityBefore = activeSongWriter().bufferCapacity();
	if (!serializeSongToBuffer()) {
		abandonDocument(Error::WRITE_FAIL);
		return;
	}
	// Count the doublings, so a gate can tell a song that exercised the growth path from one that
	// fitted the initial buffer and proved nothing about it.
	stagedDocumentGrowths_ = 0;
	for (uint32_t c = capacityBefore; c < activeSongWriter().bufferCapacity(); c *= 2) {
		stagedDocumentGrowths_++;
	}

	docTempNumber_ = 0;
	docStallTicks_ = 0;
	if (pendingFileAlreadyExisted_) {
		docPhase_ = DocPhase::TEMPNAME;
	}
	else {
		docWritePath_ = pendingFilePath_;
		docPhase_ = DocPhase::WRITE;
	}
}

void SaveSongUI::runTempNamePhase() {
	using deluge::storage::bytes::ByteIo;

	if (docTempNumber_ >= kMaxTempNameAttempts) {
		// Every candidate name is taken. Fail rather than keep asking: this is a bound on the name
		// space, separate from the per-request stall bound above it.
		abandonDocument(Error::SD_CARD);
		return;
	}

	docWritePath_ = "SONGS/TEMP";
	docWritePath_.append(deluge::string::fromInt(docTempNumber_, 4));
	docWritePath_.append(writeJsonFlag ? ".Json" : ".XML");

	if (docOnOwner()) {
		(void)runOneDocStep();
		return;
	}

	++offOwnerDocumentTicks_;
	DelugeStatus status = DELUGE_OK;
	bool present = false;
	switch (deluge::storage::bytes::collectExists(docWritePath_.c_str(), &present, &status)) {
	case ByteIo::Unknown:
		if (deluge::storage::bytes::requestExists(docWritePath_.c_str()) == ByteIo::Failed) {
			abandonDocument(Error::SD_CARD);
		}
		else if (!deferDocument()) {
			abandonDocument(Error::SD_CARD);
		}
		return;
	case ByteIo::Pending:
		if (!deferDocument()) {
			abandonDocument(Error::SD_CARD);
		}
		return;
	case ByteIo::Failed:
		// The card did not answer. Treating an unanswerable name as free would write over whatever
		// is there -- the same rule the target's existence check follows.
		abandonDocument(Error::SD_CARD);
		return;
	case ByteIo::Ready:
		++documentChannelServedOps_;
		++documentSteps_;
		docStallTicks_ = 0;
		if (present) {
			docTempNumber_++; // Taken; try the next one on the next tick.
			return;
		}
		docPhase_ = DocPhase::WRITE;
		return;
	}
}

void SaveSongUI::runWritePhase() {
	if (docOnOwner() || !docMayUseWriteChannel()) {
		(void)runOneDocStep();
		return;
	}

	// The WRITE channel, not the byte channel every other phase here uses. The byte channel copies
	// into a fixed staging buffer and refuses anything over 8 KiB; this one passes the pointer
	// across. No song has ever fitted the first: the smallest measured is 11.5 KB and the largest
	// 1.10 MB. The staged buffer must stay alive and unmodified until the collect is terminal, which
	// is why nothing releases the serialiser before FINISH.
	++offOwnerDocumentTicks_;

	if (!docWriteIssued_) {
		// may_overwrite is false in both cases: on the overwrite path this is a temp name just found
		// free, and on the direct path the target name was free or we would be on the temp path. A
		// refusal is safer than a race either way.
		if (!deluge_file_write_request_write_file(docWritePath_.c_str(), stagedDocument_, stagedLength_, false)) {
			// Refused, not failed: nothing was queued, so ask again next tick.
			if (!deferDocument()) {
				abandonDocument(Error::WRITE_FAIL);
			}
			return;
		}
		docWriteIssued_ = true;
		docStallTicks_ = 0;
		return;
	}

	uint32_t writtenLen = 0;
	uint8_t exists = 0;
	const uint8_t collected = deluge_file_write_collect_write_file(docWritePath_.c_str(), &writtenLen, &exists);
	if (collected == DELUGE_FILE_WRITE_PENDING) {
		if (!deferDocument()) {
			abandonDocument(Error::WRITE_FAIL);
		}
		return;
	}
	if (collected == DELUGE_FILE_WRITE_UNKNOWN) {
		// Never keyed, so nothing is coming. Re-issue rather than wait forever.
		docWriteIssued_ = false;
		if (!deferDocument()) {
			abandonDocument(Error::WRITE_FAIL);
		}
		return;
	}
	docWriteIssued_ = false;
	if (collected == DELUGE_FILE_WRITE_FAILED || writtenLen != stagedLength_) {
		// A short write is a card error, never a smaller document.
		abandonDocument(Error::WRITE_FAIL);
		return;
	}
	++documentChannelServedOps_;
	++documentWriteChannelServedOps_;
	++documentSteps_;
	docStallTicks_ = 0;
	docPhase_ = DocPhase::VERIFY;
	docReadBack_.begin(songReadBackExpectation());
}

void SaveSongUI::runVerifyPhase() {
	using deluge::storage::ReadBackTick;

	if (docOnOwner()) {
		(void)runOneDocStep();
		return;
	}

	++offOwnerDocumentTicks_;
	switch (docReadBack_.step(docWritePath_.c_str())) {
	case ReadBackTick::Waiting:
		if (!deferDocument()) {
			abandonDocument(Error::WRITE_FAIL);
		}
		return;
	case ReadBackTick::Failed:
		// A document whose length or ends differ from what was built is a short or wrong write, and a
		// short song parses as a smaller one -- the quiet corruption this check exists to catch.
		abandonDocument(Error::WRITE_FAIL);
		return;
	case ReadBackTick::Advanced:
		++documentChannelServedOps_;
		++documentSteps_;
		docStallTicks_ = 0;
		return;
	case ReadBackTick::Passed:
		++documentChannelServedOps_;
		++documentSteps_;
		docStallTicks_ = 0;
		docPhase_ = pendingFileAlreadyExisted_ ? DocPhase::UNLINK : DocPhase::FINISH;
		return;
	}
}

void SaveSongUI::runUnlinkPhase() {
	using deluge::storage::bytes::ByteIo;

	if (docOnOwner()) {
		(void)runOneDocStep();
		return;
	}

	++offOwnerDocumentTicks_;
	DelugeStatus status = DELUGE_OK;
	switch (deluge::storage::bytes::collectUnlink(pendingFilePath_.c_str(), &status)) {
	case ByteIo::Unknown:
		if (deluge::storage::bytes::requestUnlink(pendingFilePath_.c_str()) == ByteIo::Failed) {
			abandonDocument(Error::SD_CARD);
		}
		else if (!deferDocument()) {
			abandonDocument(Error::SD_CARD);
		}
		return;
	case ByteIo::Pending:
		if (!deferDocument()) {
			abandonDocument(Error::SD_CARD);
		}
		return;
	case ByteIo::Failed:
		abandonDocument(Error::SD_CARD);
		return;
	case ByteIo::Ready:
		++documentChannelServedOps_;
		++documentSteps_;
		docStallTicks_ = 0;
		docPhase_ = DocPhase::RENAME;
		return;
	}
}

void SaveSongUI::runRenamePhase() {
	using deluge::storage::bytes::ByteIo;

	if (docOnOwner()) {
		(void)runOneDocStep();
		return;
	}

	++offOwnerDocumentTicks_;
	DelugeStatus status = DELUGE_OK;
	switch (deluge::storage::bytes::collectRename(pendingFilePath_.c_str(), &status)) {
	case ByteIo::Unknown:
		if (deluge::storage::bytes::requestRename(docWritePath_.c_str(), pendingFilePath_.c_str()) == ByteIo::Failed) {
			abandonDocument(Error::SD_CARD);
		}
		else if (!deferDocument()) {
			abandonDocument(Error::SD_CARD);
		}
		return;
	case ByteIo::Pending:
		if (!deferDocument()) {
			abandonDocument(Error::SD_CARD);
		}
		return;
	case ByteIo::Failed:
		abandonDocument(Error::SD_CARD);
		return;
	case ByteIo::Ready:
		++documentChannelServedOps_;
		++documentSteps_;
		docStallTicks_ = 0;
		docPhase_ = DocPhase::FINISH;
		return;
	}
}

void SaveSongUI::runDocFinishPhase() {
	// Hand the shared serialiser back BEFORE anything else: it is global, and the two settings
	// writes below reach code that writes files through it.
	releaseSongWriter();
	docPhase_ = DocPhase::NONE;
	// Reaching here is the only evidence a save SUCCEEDED. Every failure path abandons after the
	// file has already been written, so the file's presence proves nothing on its own.
	++documentSavesCompleted_;

	display->removeWorkingAnimation();

	// Carried across from the old tail unchanged. `anyErrorMovingTempFiles_` chose between two popup
	// strings there, but nothing ever displayed the result -- upstream `main` has the same dead
	// local -- so preserving behaviour means not adding a popup here.
	// The name and folder the save was STARTED with, not whatever the browser holds now: the save spans
	// many ticks, and reading the live values would record something typed while it ran.
	currentSong->name = existsRequest_.enteredText;
	currentSong->dirPath = existsRequest_.dirPath;

	// Both of these choose their own arm: they use the byte channel when it is running and hand the
	// work to the storage owner when it is not, so calling them from here does not block this tier.
	if (FlashStorage::defaultStartupSongMode == StartupSongMode::LASTSAVED) {
		runtimeFeatureSettings.writeSettingsToFile();
	}
	// While we're at it, save MIDI devices if there's anything new to save.
	MIDIDeviceManager::writeDevicesToFile();

	close();
}

bool SaveSongUI::runOneDocStep() {
	if (docOnOwner()) {
		// Blocking is legal here, and asking the channel would be worse than pointless: a request
		// issued from a context that will not yield can never be answered.
		++documentOwnerServedOps_;
		++documentSteps_;
		if (!docStepSynchronously()) {
			abandonDocument(Error::WRITE_FAIL);
			return false;
		}
		return true;
	}

	// Off the owner with the write channel off -- the document write is the only step that still comes here.
	// Hand it to the owner, which may block.
	if (ownerDocDone_) {
		ownerDocDone_ = false;
		ownerDocDispatched_ = false;
		if (!ownerDocOk_) {
			abandonDocument(Error::WRITE_FAIL);
			return false;
		}
		docStallTicks_ = 0;
		return true;
	}
	if (!ownerDocDispatched_) {
		if (!deluge::storage::Owner::run(&SaveSongUI::runDocStepOnOwner, this)) {
			// Refused, not failed: nothing was queued, so ask again next tick.
			if (!deferDocument()) {
				abandonDocument(Error::WRITE_FAIL);
			}
			return false;
		}
		ownerDocDispatched_ = true;
		docStallTicks_ = 0;
		return false;
	}
	if (!deferDocument()) {
		abandonDocument(Error::WRITE_FAIL);
	}
	return false;
}

void SaveSongUI::runDocStepOnOwner(void* self) {
	auto* ui = static_cast<SaveSongUI*>(self);
	++documentOwnerServedOps_;
	++documentSteps_;
	ui->ownerDocOk_ = ui->docStepSynchronously();
	ui->ownerDocDone_ = true;
}

bool SaveSongUI::docStepSynchronously() {
	switch (docPhase_) {
	case DocPhase::TEMPNAME: {
		switch (deluge::storage::save_action(StorageManager::fileExists(docWritePath_.c_str()), false)) {
		case deluge::storage::SaveAction::Write:
			docPhase_ = DocPhase::WRITE;
			return true;
		case deluge::storage::SaveAction::Occupied:
			docTempNumber_++;
			return true;
		case deluge::storage::SaveAction::Refuse:
			return false;
		}
		return false;
	}
	case DocPhase::WRITE: {
		StorageManager::buildPathToFile(docWritePath_.c_str());
		auto created = StorageManager::createFile(docWritePath_.c_str(), false);
		if (!created) {
			return false;
		}
		auto written = created->write(
		    std::span<const std::byte>(reinterpret_cast<const std::byte*>(stagedDocument_), stagedLength_));
		const bool ok = written.has_value() && *written == stagedLength_;
		(void)created->close();
		if (!ok) {
			return false;
		}
		docPhase_ = DocPhase::VERIFY;
		docReadBack_.begin(songReadBackExpectation());
		return true;
	}
	case DocPhase::VERIFY: {
		if (!deluge::storage::readBackSynchronously(docWritePath_.c_str(), songReadBackExpectation())) {
			return false;
		}
		docPhase_ = pendingFileAlreadyExisted_ ? DocPhase::UNLINK : DocPhase::FINISH;
		return true;
	}
	case DocPhase::UNLINK: {
		deluge_file_invalidate_cache();
		if (!deluge::io::unlink(pendingFilePath_).has_value()) {
			return false;
		}
		docPhase_ = DocPhase::RENAME;
		return true;
	}
	case DocPhase::RENAME: {
		deluge_file_invalidate_cache();
		if (!deluge::io::rename(docWritePath_, pendingFilePath_).has_value()) {
			return false;
		}
		docPhase_ = DocPhase::FINISH;
		return true;
	}
	case DocPhase::NONE:
	case DocPhase::SERIALIZE:
	case DocPhase::FINISH:
		// Never dispatched: SERIALIZE and FINISH touch no card, and NONE is not a step.
		return true;
	}
	return false;
}

deluge::storage::ReadBackExpectation SaveSongUI::songReadBackExpectation() const {
	// JSON has no closing string to compare: JsonSerializer::closeFileAfterWriting passed NULL for it.
	return {.staged = stagedDocument_,
	        .length = stagedLength_,
	        .head = writeJsonFlag ? kSongHeadJson : kSongHeadXml,
	        .tail = writeJsonFlag ? nullptr : kSongTailXml};
}

FileWriter& SaveSongUI::activeSongWriter() {
	if (writeJsonFlag) {
		return static_cast<FileWriter&>(smJsonSerializer);
	}
	return static_cast<FileWriter&>(smSerializer);
}

bool SaveSongUI::serializeSongToBuffer() {
	FileWriter& writer = activeSongWriter();

	// No prologue and no closing tag here. Song::writeToFile() calls GetSerializer().reset() itself,
	// writes the XML declaration, and closes with writeClosingTag("song") -- writing either here
	// would duplicate it. This is the difference from the staged PRESET saves, whose serialisers do
	// not write their own header because createXMLFile() used to do it as it opened the file.
	//
	// setMemoryBased() survives that internal reset(): reset() clears the buffer position, the byte
	// count and the failure flag, and nothing else.
	writer.setMemoryBased();
	currentSong->writeToFile();

	const int32_t written = writer.bytesWritten();
	if (written <= 0) {
		// An empty document is a FAILED save, not a small one.
		releaseSongWriter();
		return false;
	}
	stagedDocument_ = reinterpret_cast<const uint8_t*>(writer.getBufferPtr());
	stagedLength_ = static_cast<uint32_t>(written);
	return true;
}

void SaveSongUI::releaseSongWriter() {
	stagedDocument_ = nullptr;
	stagedLength_ = 0;
	// The two halves come from different bases: XMLSerializer inherits Serializer AND FileWriter, and
	// the memory-based surface is on the FileWriter side while reset() is virtual on the Serializer
	// side. GetSerializer() reaches only the latter, which is why activeSongWriter() exists at all.
	activeSongWriter().clearMemoryBased();
	GetSerializer().reset();
}

bool SaveSongUI::finishSaveAfterCollection() {
	// The card work this function used to do has moved into the
	// document machine, whose steps each choose their own arm; what is left is bookkeeping and runs
	// legally wherever it is called from.
	docPhase_ = DocPhase::SERIALIZE;
	ownerDocDispatched_ = false;
	ownerDocDone_ = false;
	docStallTicks_ = 0;
	return true;
}
