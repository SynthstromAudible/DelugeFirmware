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
#include "model/sample/sample.h"
#include "model/settings/runtime_feature_settings.h"
#include "model/song/song.h"
#include "storage/audio/audio_file_manager.h"
#include "storage/flash_storage.h"
#include "storage/storage_manager.h"
#include "util/functions.h"
#include <string.h>

extern "C" {
#include "fatfs/ff.h"
}

using namespace deluge;
using namespace gui;

extern uint8_t currentlyAccessingCard;

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

	String searchFilename;
	searchFilename.set(&currentSong->name);
	if (!searchFilename.isEmpty()) {
		error = searchFilename.concatenate(".XML");
		if (error != Error::NONE) {
gotError:
			display->displayError(error);
			goto doReturnFalse;
		}
	}
	currentFolderIsEmpty = false;

	currentDir.set(&currentSong->dirPath);

	error = arrivedInNewFolder(0, searchFilename.get(), "SONGS");
	if (error != Error::NONE) {
		goto gotError;
	}

	// TODO: create folder if doesn't exist.

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
	return true;
}

void SaveSongUI::focusRegained() {
	collectingSamples = false;
	return SaveUI::focusRegained();
}

bool SaveSongUI::performSave(StorageManager& bdsm, bool mayOverwrite) {

	if (ALPHA_OR_BETA_VERSION && currentlyAccessingCard) {
		FREEZE_WITH_ERROR("E316");
	}

	if (currentSong->hasAnyPendingNextOverdubs()) {
		display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_OVERDUBS_PENDING));
		return false;
	}

	display->displayLoadingAnimationText("Saving");

	String filePath;
	Error error = getCurrentFilePath(&filePath);
	if (error != Error::NONE) {
gotError:
		display->removeLoadingAnimation();
		display->displayError(error);
		return false;
	}

	bool fileAlreadyExisted = bdsm.fileExists(filePath.get());

	if (!mayOverwrite && fileAlreadyExisted) {
		context_menu::overwriteFile.currentSaveUI = this;

		bool available = context_menu::overwriteFile.setupAndCheckAvailability();

		if (available) { // Always true.
			display->removeWorkingAnimation();
			display->setNextTransitionDirection(1);
			openUI(&context_menu::overwriteFile);
			return true;
		}
		else {
			error = Error::UNSPECIFIED;
			goto gotError;
		}
	}

	// We might want to copy some samples around - either because we're "collecting" them to a folder, or because they
	// were loaded in from a collected folder and we now need to put them in the main samples folder.

	// Create sample dir
	String newSongAlternatePath;

	String filenameWithoutExtension;
	error = getCurrentFilenameWithoutExtension(&filenameWithoutExtension);
	if (error != Error::NONE) {
		goto gotError;
	}

	error =
	    audioFileManager.setupAlternateAudioFileDir(&newSongAlternatePath, currentDir.get(), &filenameWithoutExtension);
	if (error != Error::NONE) {
		goto gotError;
	}
	error = newSongAlternatePath.concatenate("/");
	if (error != Error::NONE) {
		goto gotError;
	}
	int32_t dirPathLengthNew = newSongAlternatePath.getLength();

	bool anyErrorMovingTempFiles = false;

	// Go through each AudioFile we have a record of in RAM.
	for (int32_t i = 0; i < audioFileManager.audioFiles.getNumElements(); i++) {
		AudioFile* audioFile = (AudioFile*)audioFileManager.audioFiles.getElement(i);

		// If this AudioFile is used in this Song...
		if (audioFile->numReasonsToBeLoaded) {

			if (audioFile->type == AudioFileType::SAMPLE) {
				// If this is a recording which still exists at its temporary location, move the file
				if (!((Sample*)audioFile)->tempFilePathForRecording.isEmpty()) {
					FRESULT result =
					    f_rename(((Sample*)audioFile)->tempFilePathForRecording.get(), audioFile->filePath.get());
					if (result == FR_OK) {
						((Sample*)audioFile)->tempFilePathForRecording.clear();
					}
					else {
						// We at least need to warn the user that although the main file save was (hopefully soon to be)
						// successful, something's gone wrong
						anyErrorMovingTempFiles = true;
						/*
						D_PRINTLN("rename failed.  %d %s %s", result, ((Sample*)sample)->tempFilePathForRecording.get(),
						sample->filePath.get());
						*/
					}
				}
			}

			// Or if file needs copying either to or from an "alt" location - either because we're doing a "collect
			// media" or importing from such a folder. Crucial obscure combination - we could be doing a "collect media"
			// *AND ALSO* have moved (or even failed to move!) a recorded file from its "temp" location above
			if (collectingSamples || !audioFile->loadedFromAlternatePath.isEmpty()) {

				// If saving as *same* song name/slot, collecting samples, and it already came from alt location, no
				// need to do it again
				if (collectingSamples && !audioFile->loadedFromAlternatePath.isEmpty()) {
					if (currentDir.equalsCaseIrrespective(&currentSong->dirPath)) {
						if (enteredText.equalsCaseIrrespective(&currentSong->name)) {
							continue;
						}
					}
				}

				char const* sourceFilePath;

				// Sort out source file path
				if (!audioFile->loadedFromAlternatePath.isEmpty()) {
					// If we loaded the file from an alternate path originally, well we saved that exact path just so we
					// can recall it here!
					sourceFilePath = audioFile->loadedFromAlternatePath.get();
				}
				else {
					sourceFilePath =
					    (audioFile->type != AudioFileType::SAMPLE
					     || ((Sample*)audioFile)->tempFilePathForRecording.isEmpty())
					        ? audioFile->filePath.get()
					        : ((Sample*)audioFile)
					              ->tempFilePathForRecording.get(); // It may still have a temp path if for some reason
					                                                // we failed to move it, above
				}

				// Note: we can't just use the clusters to write back to the card, cos these might contain data that we
				// converted

				// Open file to read
				FRESULT result = f_open(&smSerializer.writeFIL, sourceFilePath, FA_READ);
				if (result != FR_OK) {
					D_PRINTLN("open fail %s", sourceFilePath);
					error = Error::UNSPECIFIED;
					goto gotError;
				}

				char const* destFilePath;
				char const* normalFilePath; // Just briefly stores a thing below

				bool needToPretendLoadedAlternate = false;

				// Sort out destination file path
				if (collectingSamples) {

					// If this sample is a "recording", we need to append a random string on the end
					// NOTE: I guess this would do this multiple times if we kept re-saving... probably not the end of
					// the world?
					normalFilePath = audioFile->filePath.get();
					if (!memcasecmp(normalFilePath, "SAMPLES/RECORD/REC", 18)
					    || !memcasecmp(normalFilePath, "SAMPLES/RESAMPLE/REC", 20)
					    || !memcasecmp(normalFilePath, "SAMPLES/CLIPS/REC", 17)) {
						char const* slashAddr = strrchr(normalFilePath, '/');
						int32_t slashPos = (uint32_t)slashAddr - (uint32_t)normalFilePath;

						int32_t fileNamePos = slashPos + 1;

						if (audioFile->filePath.getLength() - fileNamePos == 12
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
							audioFile->filePath.concatenateAtPos(buffer, fileNamePos + 8);

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
					if (!memcasecmp(audioFile->filePath.get(), "SAMPLES/", 8)) {
						error = audioFileManager.setupAlternateAudioFilePath(&newSongAlternatePath, dirPathLengthNew,
						                                                     &audioFile->filePath);
						if (error != Error::NONE) {
failAfterOpeningSourceFile:
							f_close(&smDeserializer.readFIL); // Close source file
							goto gotError;
						}
					}

					// Or, if it wasn't in the SAMPLES folder, e.g. if it was in a dedicated SYNTH folder, then we have
					// to just use the original filename, and hope it doesn't clash with anything.
					else {
						char const* fileName = getFileNameFromEndOfPath(audioFile->filePath.get());
						error = newSongAlternatePath.concatenateAtPos(fileName, dirPathLengthNew);
						if (error != Error::NONE) {
							goto failAfterOpeningSourceFile;
						}
					}

					if (needToPretendLoadedAlternate) {
						audioFile->loadedFromAlternatePath.set(&newSongAlternatePath);
					}

					destFilePath = newSongAlternatePath.get();
				}
				else {
					destFilePath = audioFile->filePath.get();
				}

				// Create file to write
				auto created = bdsm.createFile(destFilePath, false);
				if (!created) {
					if (created.error() == Error::FILE_ALREADY_EXISTS) {
						// No problem - the audio file was already there from
						// before, so we don't need to copy it again now.
					}
					else {
						error = created.error();
						goto failAfterOpeningSourceFile;
					}
				}

				// Or if everything's fine and we're ready to write / copy...
				else {

					// Copy
					while (true) {
						UINT bytesRead;
						result = f_read(&smDeserializer.readFIL, smDeserializer.fileClusterBuffer,
						                audioFileManager.clusterSize, &bytesRead);
						if (result) {
							D_PRINTLN("read fail");
fail3:
							error = Error::UNSPECIFIED;
							goto failAfterOpeningSourceFile;
						}
						if (!bytesRead) {
							break; // Stop, on rare case where file ended right at end of last cluster
						}

						auto written = created.value().write({(std::byte*)smDeserializer.fileClusterBuffer, bytesRead});
						if (!written || written.value() != bytesRead) {
							D_PRINTLN("write fail %d", result);
							goto fail3;
						}

						if (bytesRead < audioFileManager.clusterSize) {
							break; // Stop - file clearly ended part-way through cluster
						}
					}
				}

				f_close(&smDeserializer.readFIL); // Close source file

				// Write has succeeded. We can mark it as existing in its normal main location (e.g. in the SAMPLES
				// folder). Unless we were collection media, in which case it won't be there - it'll be in the new
				// alternate location we put it in.
				if (!collectingSamples) {
					audioFile->loadedFromAlternatePath.clear();
				}
			}
		}
	}

	String filePathDuringWrite;

	// If we're overwriting an existing file, we'll write to a temp file first. Find one that doesn't already exist
	if (fileAlreadyExisted) {

		int32_t tempFileNumber = 0;

		while (true) {
			error = filePathDuringWrite.set("SONGS/TEMP");
			if (error != Error::NONE) {
				goto gotError;
			}
			error = filePathDuringWrite.concatenateInt(tempFileNumber, 4);
			if (error != Error::NONE) {
				goto gotError;
			}
			error = filePathDuringWrite.concatenate(".XML");
			if (error != Error::NONE) {
				goto gotError;
			}

			if (!bdsm.fileExists(filePathDuringWrite.get())) {
				break;
			}

			tempFileNumber++;
		}
	}
	else {
		filePathDuringWrite.set(&filePath);
	}

	D_PRINTLN("creating:  %s", filePathDuringWrite.get());

	// Write the actual song file
	error = bdsm.createXMLFile(filePathDuringWrite.get(), smSerializer, false, false);
	if (error != Error::NONE) {
		goto gotError;
	}

	// (Sept 2019) - it seems a crash sometimes occurs sometime after this point. A 0-byte file gets created. Could be
	// for either overwriting or not.

	currentSong->writeToFile(bdsm);

	error = GetSerializer().closeFileAfterWriting(filePathDuringWrite.get(),
	                                           "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<song\n", "\n</song>\n");
	if (error != Error::NONE) {
		goto gotError;
	}

	// If "overwriting an existing file"...
	if (fileAlreadyExisted) {

		// Delete the old file
		FRESULT result = f_unlink(filePath.get());
		if (result != FR_OK) {
cardError:
			error = fresultToDelugeErrorCode(result);
			goto gotError;
		}

		// Rename the new file
		result = f_rename(filePathDuringWrite.get(), filePath.get());
		if (result != FR_OK) {
			goto cardError;
		}
	}

	display->removeWorkingAnimation();
	char const* message = anyErrorMovingTempFiles
	                          ? (deluge::l10n::get(deluge::l10n::String::STRING_FOR_ERROR_MOVING_TEMP_FILES))
	                          : (deluge::l10n::get(deluge::l10n::String::STRING_FOR_SONG_SAVED));
	// Update all of these
	currentSong->name.set(&enteredText);
	currentSong->dirPath.set(&currentDir);

	if (FlashStorage::defaultStartupSongMode == StartupSongMode::LASTSAVED) {
		runtimeFeatureSettings.writeSettingsToFile(bdsm);
	}
	// While we're at it, save MIDI devices if there's anything new to save.
	MIDIDeviceManager::writeDevicesToFile(bdsm);

	close();
	return true;
}
