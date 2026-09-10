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

bool SaveSongUI::performSave(bool mayOverwrite) {
	noteSaveTier();

	// Diagnostic only, not a hard invariant (see docs/dev/sd_busy_audit.md, save_song_ui.cpp): this
	// used to FREEZE_WITH_ERROR("E316") here, back when sd_busy() was permanently false and the check
	// could never fire. No comment or invariant here ever explained *why* entering performSave() while
	// busy would be unsafe, and none was found -- sd_busy() being true at this exact instant is
	// orthogonal to any hazard at this call site. Log it rather than crash a beta build over what is
	// ordinary scheduling coincidence.
	//
	// This comment used to go on to say performSave is not owner-dispatched, so its FS calls run off
	// the worker fiber where the efatfs C-ABI rejects them outright, and that the resulting
	// "fileExists silently reads as absent" was a separate tracked gap. Both halves are now stale:
	// performSave IS dispatched onto the storage owner, and that gap was the conflation this rung
	// closed -- the two existence checks go through save_action(), which refuses rather than
	// treating an unanswerable card as an absent file.
	if (ALPHA_OR_BETA_VERSION && deluge::sync::sd_busy()) {
		D_PRINTLN("performSave: sd_busy() was true on entry (see sd_busy_audit.md)");
	}

	if (currentSong->hasAnyPendingNextOverdubs()) {
		display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_OVERDUBS_PENDING));
		return false;
	}

	display->displayLoadingAnimationText("Saving");

	std::string filePath = getCurrentFilePath();
	Error error = Error::NONE;
	if (error != Error::NONE) {
gotError:
		display->removeLoadingAnimation();
		display->displayError(error);
		return false;
	}

	// `fileAlreadyExisted` still drives the unlink-and-rename tail below, so it is derived from the
	// same check rather than deleted: Occupied and a confirmed overwrite both mean a file is there.
	bool fileAlreadyExisted = false;
	switch (deluge::storage::save_action(StorageManager::fileExists(filePath.c_str()), mayOverwrite)) {
	case deluge::storage::SaveAction::Refuse:
		// The card did not answer. Writing here would overwrite a file whose absence was never
		// confirmed -- see existence_policy.h. Write nothing.
		error = Error::SD_CARD;
		goto gotError;
	case deluge::storage::SaveAction::Occupied:
		fileAlreadyExisted = true;
		context_menu::overwriteFile.currentSaveUI = this;
		if (!context_menu::overwriteFile.setupAndCheckAvailability()) { // Always available.
			error = Error::UNSPECIFIED;
			goto gotError;
		}
		display->removeWorkingAnimation();
		openUI(&context_menu::overwriteFile);
		return true;
	case deluge::storage::SaveAction::Write:
		// Write covers both "nothing there" and "the user confirmed an overwrite"; only the latter
		// has a file to unlink afterwards.
		fileAlreadyExisted = mayOverwrite;
		break;
	}

	// We might want to copy some samples around - either because we're "collecting" them to a folder, or because they
	// were loaded in from a collected folder and we now need to put them in the main samples folder.

	// Create sample dir
	std::string newSongAlternatePath;

	error = audioFileManager.setupAlternateAudioFileDir(newSongAlternatePath, currentDir.c_str(), enteredText.c_str());
	if (error != Error::NONE) {
		goto gotError;
	}

	newSongAlternatePath.append("/");
	int32_t dirPathLengthNew = newSongAlternatePath.size();

	bool anyErrorMovingTempFiles = false;

	// Go through each AudioFile we have a record of in RAM.
	auto saveAudioFile = [&](AudioFile* audioFile) {
		// If this AudioFile is used in this Song...
		if (audioFile->isProjectReferenced()) {

			if (audioFile->type == AudioFileType::SAMPLE) {
				Sample& sample = *static_cast<Sample*>(audioFile);
				// If this is a recording which still exists at its temporary location, move the file
				if (!sample.tempFilePathForRecording.empty()) {
					StorageManager::buildPathToFile(audioFile->filePath.c_str());
					deluge_file_invalidate_cache();
					auto renamed = deluge::io::rename(sample.tempFilePathForRecording, audioFile->filePath);
					if (renamed.has_value()) {
						sample.tempFilePathForRecording.clear();
					}
					else {
						// We at least need to warn the user that although the main file save was (hopefully soon to be)
						// successful, something's gone wrong
						anyErrorMovingTempFiles = true;
						D_PRINTLN("rename failed. %s %s", sample.tempFilePathForRecording.c_str(),
						          audioFile->filePath.c_str());
					}
				}
			}

			// Or if file needs copying either to or from an "alt" location - either because we're doing a "collect
			// media" or importing from such a folder. Crucial obscure combination - we could be doing a "collect media"
			// *AND ALSO* have moved (or even failed to move!) a recorded file from its "temp" location above
			if (collectingSamples || !audioFile->loadedFromAlternatePath.empty()) {

				// If saving as *same* song name/slot, collecting samples, and it already came from alt location, no
				// need to do it again
				if (collectingSamples && !audioFile->loadedFromAlternatePath.empty()) {
					if (deluge::string::caselessEquals(currentDir, currentSong->dirPath)) {
						if (deluge::string::caselessEquals(enteredText, currentSong->name)) {
							return true;
						}
					}
				}

				char const* sourceFilePath;

				// Sort out source file path
				if (!audioFile->loadedFromAlternatePath.empty()) {
					// If we loaded the file from an alternate path originally, well we saved that exact path just so we
					// can recall it here!
					sourceFilePath = audioFile->loadedFromAlternatePath.c_str();
				}
				else {
					sourceFilePath =
					    (audioFile->type != AudioFileType::SAMPLE
					     || ((Sample*)audioFile)->tempFilePathForRecording.empty())
					        ? audioFile->filePath.c_str()
					        : ((Sample*)audioFile)
					              ->tempFilePathForRecording.c_str(); // It may still have a temp path if for some
					                                                  // reason we failed to move it, above
				}

				// Note: we can't just use the clusters to write back to the card, cos these might contain data that we
				// converted

				// this is just blind copying to move samples to/from the song folder. The serializer is being used for
				// the song file write so use the deserializer
				auto sourceOpened = deluge::io::File::open(sourceFilePath, DELUGE_FILE_READ);

				if (!sourceOpened) {
					D_PRINTLN("open fail %s", sourceFilePath);
					error = Error::UNSPECIFIED;
					display->removeLoadingAnimation();
					display->displayError(error);
					return false;
				}
				activeDeserializer->file = std::move(sourceOpened.value());

				char const* destFilePath;
				char const* normalFilePath; // Just briefly stores a thing below

				bool needToPretendLoadedAlternate = false;

				// Sort out destination file path
				if (collectingSamples) {

					// If this sample is a "recording", we need to append a random string on the end
					// NOTE: I guess this would do this multiple times if we kept re-saving... probably not the end of
					// the world?
					normalFilePath = audioFile->filePath.c_str();
					if (!memcasecmp(normalFilePath, "SAMPLES/RECORD/REC", 18)
					    || !memcasecmp(normalFilePath, "SAMPLES/RESAMPLE/REC", 20)
					    || !memcasecmp(normalFilePath, "SAMPLES/CLIPS/REC", 17)) {
						char const* slashAddr = strrchr(normalFilePath, '/');
						int32_t slashPos = (uintptr_t)slashAddr - (uintptr_t)normalFilePath;

						int32_t fileNamePos = slashPos + 1;

						if (audioFile->filePath.size() - fileNamePos == 12
						    && !strcasecmp(normalFilePath + fileNamePos + 8, ".WAV")) {

							// Generate random string, with _ at start and .WAV at end
							char buffer[11];
							buffer[0] = '_';
							buffer[6] = '.';
							buffer[7] = 'W';
							buffer[8] = 'A';
							buffer[9] = 'V';
							buffer[10] = 0;

							seedRandom();

							for (int32_t i = 1; i < 6; i++) {
								int32_t rand = random(35);
								if (rand < 10) {
									buffer[i] = '0' + rand;
								}
								else {
									buffer[i] = 'A' + (rand - 10);
								}
							}

							// Append that random string
							audioFile->filePath.resize(fileNamePos + 8), audioFile->filePath.append(buffer);

							// And because the AudioFile in memory is now associated with a file name which only exists
							// in the "alternative location", we need to mark it as if it was loaded from there, so any
							// future copying of that file will treat it correctly
							// - particularly if the user does another collect-media save over this one, meaning the
							// file should not be copied again.
							needToPretendLoadedAlternate =
							    true; // We don't have that alternate path yet, so just make a note to do it below.
						}
					}

					// Normally, the filePath will be in the SAMPLES folder, which our name-condensing system was
					// designed for...
					if (!memcasecmp(audioFile->filePath.c_str(), "SAMPLES/", 8)) {
						error = audioFileManager.setupAlternateAudioFilePath(newSongAlternatePath, dirPathLengthNew,
						                                                     audioFile->filePath);
						if (error != Error::NONE) {
							activeDeserializer->closeWriter();
							display->removeLoadingAnimation();
							display->displayError(error);
							return false;
						}
					}

					// Or, if it wasn't in the SAMPLES folder, e.g. if it was in a dedicated SYNTH folder, then we have
					// to just use the original filename, and hope it doesn't clash with anything.
					else {
						char const* fileName = getFileNameFromEndOfPath(audioFile->filePath.c_str());
						newSongAlternatePath.resize(dirPathLengthNew);
						newSongAlternatePath.append(fileName);
					}

					if (needToPretendLoadedAlternate) {
						audioFile->loadedFromAlternatePath = newSongAlternatePath;
					}

					destFilePath = newSongAlternatePath.c_str();
				}
				else {
					destFilePath = audioFile->filePath.c_str();
				}

				// Create file to write
				auto created = StorageManager::createFile(destFilePath, false);
				if (!created) {
					if (created.error() == Error::FILE_ALREADY_EXISTS) {
						// No problem - the audio file was already there from
						// before, so we don't need to copy it again now.
					}
					else {
						error = created.error();
						activeDeserializer->closeWriter();
						display->removeLoadingAnimation();
						display->displayError(error);
						return false;
					}
				}

				// Or if everything's fine and we're ready to write / copy...
				else {

					// Copy
					while (true) {
						auto readResult = activeDeserializer->file->read(std::span<std::byte>(
						    reinterpret_cast<std::byte*>(activeDeserializer->fileClusterBuffer), Cluster::size));
						if (!readResult) {
							D_PRINTLN("read fail");
							error = Error::UNSPECIFIED;
							activeDeserializer->closeWriter();
							display->removeLoadingAnimation();
							display->displayError(error);
							return false;
						}
						uint32_t bytesRead = static_cast<uint32_t>(readResult->size());
						if (!bytesRead) {
							break; // Stop, on rare case where file ended right at end of last cluster
						}

						auto written =
						    created.value().write({(std::byte*)activeDeserializer->fileClusterBuffer, bytesRead});
						if (!written || written.value() != bytesRead) {
							D_PRINTLN("write fail");
							error = Error::UNSPECIFIED;
							activeDeserializer->closeWriter();
							display->removeLoadingAnimation();
							display->displayError(error);
							return false;
						}

						if (bytesRead < Cluster::size) {
							break; // Stop - file clearly ended part-way through cluster
						}
					}
				}

				activeDeserializer->closeWriter(); // Close source file

				// Write has succeeded. We can mark it as existing in its normal main location (e.g. in the SAMPLES
				// folder). Unless we were collection media, in which case it won't be there - it'll be in the new
				// alternate location we put it in.
				if (!collectingSamples) {
					audioFile->loadedFromAlternatePath.clear();
				}
			}
		}
		return true;
	};

	// Decide everything first, then perform it a tick at a time. The list of audio files is sorted by
	// path and searched by bisection, and the transfers below rewrite paths -- so nothing is applied
	// until every transfer has finished.
	transfers_.clear();
	if (!planTransfers(transfers_, collectingSamples, newSongAlternatePath, dirPathLengthNew)) {
		error = Error::UNSPECIFIED;
		goto gotError;
	}

	// Everything the continuation needs once the transfers are done. They were locals; the save no
	// longer finishes inside this call, so they have to outlive it.
	pendingFilePath_ = filePath;
	pendingFileAlreadyExisted_ = fileAlreadyExisted;
	anyErrorMovingTempFiles_ = false;
	transferCursor_ = 0;
	renameIssued_ = false;
	copyIssued_ = false;
	renameSucceeded_ = true;
	collectStallTicks_ = 0;
	collectPhase_ = CollectPhase::TRANSFER;

	// The transfers are NOT started here, and that is the point. This function runs on the storage
	// owner, so a transfer attempted from it would take the owner arm and copy synchronously -- the
	// exact thing this rung exists to stop. The repeating save task picks the collection up on its
	// next pass, off the owner, which costs one tick and buys the whole relocation.
	//
	// Returning "dealt with" is the same answer this function already gives when it opens the
	// overwrite prompt instead of finishing.
	return true;
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
	const bool mayUseChannel = !onOwner && deluge_file_bytes_async_active();

	// A build without the byte channel has no way to ask for a transfer from here, and blocking on
	// this tier would stop the very thing the card needs running to answer. The whole entry goes to
	// the owner, which may block legally -- the same build-configuration fallback the song load and
	// the document save use, and it retires when the channel becomes unconditional.
	//
	// Without this the collection deferred until its stall bound and abandoned, so a build with the
	// channel compiled out could never collect a sample at all.
	if (!onOwner && !mayUseChannel) {
		if (ownerTransferDone_) {
			ownerTransferDone_ = false;
			ownerTransferDispatched_ = false;
			if (!ownerTransferOk_) {
				abandonCollection();
				return;
			}
			++transferCursor_;
			collectStallTicks_ = 0;
			return;
		}
		if (!ownerTransferDispatched_) {
			if (!deluge::storage::Owner::run(&SaveSongUI::runTransferOnOwner, this)) {
				if (!deferCollection()) {
					abandonCollection();
				}
				return;
			}
			ownerTransferDispatched_ = true;
			collectStallTicks_ = 0;
			return;
		}
		if (!deferCollection()) {
			abandonCollection();
		}
		return;
	}

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

void SaveSongUI::runTransferOnOwner(void* self) {
	auto* ui = static_cast<SaveSongUI*>(self);
	++transferOwnerServedOps_;
	ui->ownerTransferOk_ = ui->transferOneSynchronously();
	ui->ownerTransferDone_ = true;
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

bool SaveSongUI::docMayUseChannel() {
	return !docOnOwner() && deluge_file_bytes_async_active();
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
	docVerifyStage_ = DocVerify::LENGTH;
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
	docVerifyStage_ = DocVerify::LENGTH;
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

	if (docOnOwner() || !docMayUseChannel()) {
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
	++documentSteps_;
	docStallTicks_ = 0;
	docPhase_ = DocPhase::VERIFY;
	docVerifyStage_ = DocVerify::LENGTH;
}

void SaveSongUI::runVerifyPhase() {
	using deluge::storage::bytes::ByteIo;

	if (docOnOwner() || !docMayUseChannel()) {
		(void)runOneDocStep();
		return;
	}

	const char* head = writeJsonFlag ? kSongHeadJson : kSongHeadXml;
	const auto headLen = static_cast<uint32_t>(strlen(head));
	// JSON has no closing string: JsonSerializer::closeFileAfterWriting passes NULL for it, so there
	// is nothing to compare a tail against on that build.
	const uint32_t tailLen = writeJsonFlag ? 0 : static_cast<uint32_t>(strlen(kSongTailXml));

	++offOwnerDocumentTicks_;
	DelugeStatus status = DELUGE_OK;
	char buffer[64];

	if (docVerifyStage_ == DocVerify::LENGTH) {
		uint32_t size = 0;
		switch (deluge::storage::bytes::collectStat(docWritePath_.c_str(), &size, &status)) {
		case ByteIo::Unknown:
			if (deluge::storage::bytes::requestStat(docWritePath_.c_str()) == ByteIo::Failed) {
				abandonDocument(Error::WRITE_FAIL);
			}
			else if (!deferDocument()) {
				abandonDocument(Error::WRITE_FAIL);
			}
			return;
		case ByteIo::Pending:
			if (!deferDocument()) {
				abandonDocument(Error::WRITE_FAIL);
			}
			return;
		case ByteIo::Failed:
			abandonDocument(Error::WRITE_FAIL);
			return;
		case ByteIo::Ready:
			++documentChannelServedOps_;
			++documentSteps_;
			docStallTicks_ = 0;
			// A document whose length on the card differs from the length we built is a short write,
			// and a short song parses as a smaller one -- the quiet corruption this whole class of
			// bug exists to avoid.
			if (size != stagedLength_) {
				abandonDocument(Error::WRITE_FAIL);
				return;
			}
			docVerifyStage_ = DocVerify::HEAD;
			return;
		}
		return;
	}

	const bool doingHead = docVerifyStage_ == DocVerify::HEAD;
	const uint32_t offset = doingHead ? 0 : stagedLength_ - tailLen;
	const uint32_t want = doingHead ? headLen : tailLen;
	uint32_t got = 0;

	switch (deluge::storage::bytes::collectReadAt(docWritePath_.c_str(), buffer, sizeof(buffer), &got, &status)) {
	case ByteIo::Unknown:
		if (deluge::storage::bytes::requestReadAt(docWritePath_.c_str(), offset, want) == ByteIo::Failed) {
			abandonDocument(Error::WRITE_FAIL);
		}
		else if (!deferDocument()) {
			abandonDocument(Error::WRITE_FAIL);
		}
		return;
	case ByteIo::Pending:
		if (!deferDocument()) {
			abandonDocument(Error::WRITE_FAIL);
		}
		return;
	case ByteIo::Failed:
		abandonDocument(Error::WRITE_FAIL);
		return;
	case ByteIo::Ready:
		++documentChannelServedOps_;
		++documentSteps_;
		docStallTicks_ = 0;
		// Compared against BOTH the constant and the staged buffer: the constant says the document
		// is shaped like a song, and the buffer says the card holds what we actually built.
		if (got != want || memcmp(buffer, doingHead ? head : kSongTailXml, want) != 0
		    || memcmp(buffer, stagedDocument_ + offset, want) != 0) {
			abandonDocument(Error::WRITE_FAIL);
			return;
		}
		if (doingHead && tailLen > 0) {
			docVerifyStage_ = DocVerify::TAIL;
			return;
		}
		docVerifyStage_ = DocVerify::LENGTH;
		docPhase_ = pendingFileAlreadyExisted_ ? DocPhase::UNLINK : DocPhase::FINISH;
		return;
	}
}

void SaveSongUI::runUnlinkPhase() {
	using deluge::storage::bytes::ByteIo;

	if (docOnOwner() || !docMayUseChannel()) {
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

	if (docOnOwner() || !docMayUseChannel()) {
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
	docVerifyStage_ = DocVerify::LENGTH;

	display->removeWorkingAnimation();

	// Carried across from the old tail unchanged. `anyErrorMovingTempFiles_` chose between two popup
	// strings there, but nothing ever displayed the result -- upstream `main` has the same dead
	// local -- so preserving behaviour means not adding a popup here.
	currentSong->name = enteredText;
	currentSong->dirPath = currentDir;

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

	// Off the owner with no channel to ask. Hand the step to the owner, which may block.
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
		docVerifyStage_ = DocVerify::LENGTH;
		return true;
	}
	case DocPhase::VERIFY: {
		// All three comparisons in one blocking pass, which is what the streaming writer did after
		// closing the file it had just written.
		const char* head = writeJsonFlag ? kSongHeadJson : kSongHeadXml;
		const auto headLen = static_cast<uint32_t>(strlen(head));
		const uint32_t tailLen = writeJsonFlag ? 0 : static_cast<uint32_t>(strlen(kSongTailXml));

		auto opened = deluge::io::File::open(docWritePath_.c_str(), DELUGE_FILE_READ);
		if (!opened) {
			return false;
		}
		auto size = opened->size();
		if (!size || *size != stagedLength_) {
			(void)opened->close();
			return false;
		}
		char buffer[64];
		auto readHead = opened->read(std::span<std::byte>(reinterpret_cast<std::byte*>(buffer), headLen));
		if (!readHead || readHead->size() != headLen || memcmp(buffer, head, headLen) != 0
		    || memcmp(buffer, stagedDocument_, headLen) != 0) {
			(void)opened->close();
			return false;
		}
		if (tailLen > 0) {
			if (!opened->seek(stagedLength_ - tailLen)) {
				(void)opened->close();
				return false;
			}
			auto readTail = opened->read(std::span<std::byte>(reinterpret_cast<std::byte*>(buffer), tailLen));
			if (!readTail || readTail->size() != tailLen || memcmp(buffer, kSongTailXml, tailLen) != 0
			    || memcmp(buffer, stagedDocument_ + stagedLength_ - tailLen, tailLen) != 0) {
				(void)opened->close();
				return false;
			}
		}
		(void)opened->close();
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
	// No noteSaveTier() here any more. The card work this function used to do has moved into the
	// document machine, whose steps each choose their own arm; what is left is bookkeeping and runs
	// legally wherever it is called from.
	docPhase_ = DocPhase::SERIALIZE;
	docVerifyStage_ = DocVerify::LENGTH;
	ownerDocDispatched_ = false;
	ownerDocDone_ = false;
	docStallTicks_ = 0;
	return true;
}
