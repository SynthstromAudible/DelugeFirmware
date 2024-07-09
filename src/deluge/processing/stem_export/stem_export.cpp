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

#include "processing/stem_export/stem_export.h"
#include "definitions_cxx.hpp"
#include "extern.h"
#include "gui/context_menu/stem_export/cancel_stem_export.h"
#include "gui/context_menu/stem_export/done_stem_export.h"
#include "gui/l10n/l10n.h"
#include "gui/ui/audio_recorder.h"
#include "gui/views/arranger_view.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "hid/led/indicator_leds.h"
#include "model/clip/clip.h"
#include "model/song/song.h"
#include "playback/mode/arrangement.h"
#include "playback/mode/session.h"
#include "playback/playback_handler.h"
#include "storage/audio/audio_file_manager.h"
#include "task_scheduler.h"
#include <new>
#include <string.h>

extern "C" {
#include "RZA1/gpio/gpio.h"
#include "RZA1/uart/sio_char.h"
}

using namespace deluge;
using namespace gui;

StemExport stemExport{};

StemExport::StemExport() {
	processStarted = false;

	highestUsedStemFolderNumber = -1;
	wavFileNameForStemExportSet = false;

	numStemsExported = 0;
	totalNumStemsToExport = 0;
}

/// starts stem export process which includes setting up UI mode, timer, and preparing
/// instruments / clips for exporting
void StemExport::startStemExportProcess(StemExportType stemExportType) {
	processStarted = true;

	// exit save UI mode and turn off save button LED
	exitUIMode(UI_MODE_HOLDING_SAVE_BUTTON);
	indicator_leds::setLedState(IndicatorLED::SAVE, false);

	// sets up the recording mode
	playbackHandler.recordButtonPressed();

	// enter stem export UI mode to prevent other actions from taking place while exporting stems
	// restart file numbering for stem export
	audioFileManager.highestUsedAudioRecordingNumber[util::to_underlying(AudioRecordingFolder::STEMS)] = -1;
	enterUIMode(UI_MODE_STEM_EXPORT);

	// export stems
	if (stemExportType == StemExportType::CLIP) {
		exportClipStems(stemExportType);
	}
	else if (stemExportType == StemExportType::TRACK) {
		exportInstrumentStems(stemExportType);
	}

	// if process wasn't cancelled, then we got here because we finished
	// exporting all the stems, so let's finish up
	if (processStarted) {
		finishStemExportProcess(stemExportType);
	}
	else {
		updateScrollPosition(stemExportType, totalNumStemsToExport);
	}

	// re-render UI because view scroll positions and mute statuses will have been updated
	uiNeedsRendering(getCurrentUI());
}

/// Stop stem export process
void StemExport::stopStemExportProcess() {
	exitUIMode(UI_MODE_STEM_EXPORT);
	stopOutputRecordingAndPlayback();
	display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_STOP_EXPORT_STEMS), 6);
	processStarted = false;
}

/// Simulate pressing record and play in order to trigger resampling of out output that ends when loop ends
void StemExport::startOutputRecordingUntilLoopEnd() {
	playbackHandler.playButtonPressed(kInternalButtonPressLatency);
	if (playbackHandler.isEitherClockActive()) {
		audioRecorder.beginOutputRecording();
		if (audioRecorder.recordingSource > AudioInputChannel::NONE) {
			currentPlaybackMode->stopOutputRecordingAtLoopEnd();
		}
	}
}

/// simulate pressing record and then play to stop output-recording and playback immediately
void StemExport::stopOutputRecordingAndPlayback() {
	if (audioRecorder.isCurrentlyResampling()) {
		audioRecorder.endRecordingSoon(kInternalButtonPressLatency);
	}
	if (playbackHandler.isEitherClockActive()) {
		playbackHandler.playButtonPressed(kInternalButtonPressLatency);
	}
	highestUsedStemFolderNumber++;
}

/// disarms and prepares all the instruments so that they can be exported
void StemExport::disarmAllInstrumentsForStemExport() {
	// when we begin stem export, we haven't exported any instruments yet, so initialize these variables
	numStemsExported = 0;

	// when we trigger stem export, we don't know how many instruments there are yet
	// so get the number and store it so we only need to ping getNumElements once
	totalNumStemsToExport = currentSong->getNumOutputs();

	if (totalNumStemsToExport) {
		// iterate through all the instruments to disable all the recording relevant flags
		for (int32_t idxOutput = 0; idxOutput < totalNumStemsToExport; ++idxOutput) {
			Output* output = currentSong->getOutputFromIndex(idxOutput);
			if (output) {
				output->mutedInArrangementModeBeforeStemExport = output->mutedInArrangementMode;
				output->mutedInArrangementMode = true;
				output->recordingInArrangement = false;
				output->armedForRecording = false;
				output->soloingInArrangementMode = false;
			}
		}
	}
}

void StemExport::restoreAllInstrumentMutes() {
	if (totalNumStemsToExport) {
		// iterate through all the instruments to restore previous mute states
		for (int32_t idxOutput = 0; idxOutput < totalNumStemsToExport; ++idxOutput) {
			Output* output = currentSong->getOutputFromIndex(idxOutput);
			if (output) {
				output->mutedInArrangementMode = output->mutedInArrangementModeBeforeStemExport;
			}
		}
	}
}

/// iterates through all instruments, arming one instrument at a time for recording
/// simulates the button combo action of pressing record + play twice to enable resample
/// and stop recording at the end of the arrangement
void StemExport::exportInstrumentStems(StemExportType stemExportType) {
	// prepare all the instruments for stem export
	disarmAllInstrumentsForStemExport();

	if (totalNumStemsToExport) {
		// now we're going to iterate through all instruments to find the first instruments that hasn't been
		// exported yet
		for (int32_t idxOutput = totalNumStemsToExport - 1; idxOutput >= 0; --idxOutput) {
			Output* output = currentSong->getOutputFromIndex(idxOutput);
			if (output) {
				bool started =
				    startCurrentStemExport(stemExportType, output, output->type, output->mutedInArrangementMode,
				                           idxOutput, output->isEmpty(false));

				if (!started) {
					// skip this stem and move to the next one
					continue;
				}

				// wait until recording is done and playback is turned off
				yield([]() {
					return !(audioRecorder.recordingSource > AudioInputChannel::NONE
					         || playbackHandler.isEitherClockActive());
				});

				finishCurrentStemExport(stemExportType, output->mutedInArrangementMode);
			}
			// in the event that stem exporting is cancelled while iterating through clips
			// break out of the loop
			if (!processStarted) {
				break;
			}
		}
	}

	// set instrument mutes back to their previous state (before exporting stems)
	restoreAllInstrumentMutes();
}

/// disarms and prepares all the clips so that they can be exported
void StemExport::disarmAllClipsForStemExport() {
	// when we begin stem export, we haven't exported any clips yet, so initialize these variables
	numStemsExported = 0;
	currentSong->xScroll[NAVIGATION_CLIP] = 0;

	// when we trigger stem export, we don't know how many clips there are yet
	// so get the number and store it so we only need to ping getNumElements once
	totalNumStemsToExport = currentSong->sessionClips.getNumElements();

	if (totalNumStemsToExport) {
		// iterate through all clips to disable all the recording relevant flags
		for (int32_t idxClip = 0; idxClip < totalNumStemsToExport; ++idxClip) {
			Clip* clip = currentSong->sessionClips.getClipAtIndex(idxClip);
			if (clip) {
				clip->activeIfNoSoloBeforeStemExport = clip->activeIfNoSolo;
				clip->activeIfNoSolo = false;
				clip->armState = ArmState::OFF;
				clip->armedForRecording = false;
				clip->soloingInSessionMode = false;
			}
		}
	}
}

void StemExport::restoreAllClipMutes() {
	if (totalNumStemsToExport) {
		// iterate through all clips to restore previous mute states
		for (int32_t idxClip = 0; idxClip < totalNumStemsToExport; ++idxClip) {
			Clip* clip = currentSong->sessionClips.getClipAtIndex(idxClip);
			if (clip) {
				clip->activeIfNoSolo = clip->activeIfNoSoloBeforeStemExport;
			}
		}
	}
}

/// iterates through all clips, arming one clip at a time for recording
/// simulates the button combo action of pressing record + play twice to enable resample
/// and stop recording at the end of the clip's loop length
void StemExport::exportClipStems(StemExportType stemExportType) {
	// prepare all the clips for stem export
	disarmAllClipsForStemExport();

	if (totalNumStemsToExport) {
		// now we're going to iterate through all clips to find the first clip that hasn't been exported yet
		for (int32_t idxClip = totalNumStemsToExport - 1; idxClip >= 0; --idxClip) {
			Clip* clip = currentSong->sessionClips.getClipAtIndex(idxClip);
			if (clip) {
				bool started = startCurrentStemExport(stemExportType, clip->output, clip->output->type,
				                                      clip->activeIfNoSolo, idxClip, clip->isEmpty(false));

				if (!started) {
					// skip this stem and move to the next one
					continue;
				}

				// wait until recording is done and playback is turned off
				yield([]() {
					return !(audioRecorder.recordingSource > AudioInputChannel::NONE
					         || playbackHandler.isEitherClockActive());
				});

				finishCurrentStemExport(stemExportType, clip->activeIfNoSolo);
			}
			// in the event that stem exporting is cancelled while iterating through clips
			// break out of the loop
			if (!processStarted) {
				break;
			}
		}
	}

	// set clip mutes back to their previous state (before exporting stems)
	restoreAllClipMutes();
}

bool StemExport::startCurrentStemExport(StemExportType stemExportType, Output* output, OutputType outputType,
                                        bool& muteState, int32_t indexNumber, bool isEmpty) {
	updateScrollPosition(stemExportType, indexNumber + 1);

	// exclude empty clips, MIDI and CV clips
	if (isEmpty || outputType == OutputType::MIDI_OUT || outputType == OutputType::CV) {
		// updated number of stems exported (even though we didn't actually export anything)
		// so that we know we processed this stem
		numStemsExported++;

		return false;
	}

	if (stemExportType == StemExportType::CLIP) {
		// unmute clip for recording
		muteState = true; // clip->activeIfNoSolo
	}
	else if (stemExportType == StemExportType::TRACK) {
		// unmute output for recording
		muteState = false; // output->mutedInArrangementMode
	}

	// re-render song view since we scrolled and updated mutes
	uiNeedsRendering(getCurrentUI());

	// set wav file name for stem to be exported
	setWavFileNameForStemExport(stemExportType, output, indexNumber);

	// start resampling which ends when end of clip is reached
	stemExport.startOutputRecordingUntilLoopEnd();

	// we haven't exported all the clips yet
	// so display the number of clips we've exported so far
	displayStemExportProgress(stemExportType);

	return true;
}

/// mute clip or output after recording it so that it's not recorded next time
/// update recording mode if it needs to be updated
/// increment number of stems exported so progress can be displayed
void StemExport::finishCurrentStemExport(StemExportType stemExportType, bool& muteState) {
	if (stemExportType == StemExportType::CLIP) {
		// mute clip for recording
		muteState = false; // clip->activeIfNoSolo
	}
	else if (stemExportType == StemExportType::TRACK) {
		// mute output for recording
		muteState = true; // output->mutedInArrangementMode
	}

	// update number of instruments exported
	numStemsExported++;
}

// if we know how many stems to export, we can check if we've already exported all the
// stems and are therefore done and should exit out of the stem export UI mode
void StemExport::finishStemExportProcess(StemExportType stemExportType) {
	// the only other UI we could be in is the context menu, so let's get out of that
	if (inContextMenu()) {
		display->setNextTransitionDirection(-1);
		getCurrentUI()->close();
	}

	// display stem export completed context menu
	bool available = context_menu::doneStemExport.setupAndCheckAvailability();
	if (available) {
		display->setNextTransitionDirection(1);
		openUI(&context_menu::doneStemExport);
	}

	// exit out of the stem export UI mode
	exitUIMode(UI_MODE_STEM_EXPORT);

	// update folder number in case this same song is exported again
	highestUsedStemFolderNumber++;

	// reset scroll position
	updateScrollPosition(stemExportType, totalNumStemsToExport);

	// turn off recording if it's still on
	if (playbackHandler.recording != RecordingMode::OFF) {
		playbackHandler.recording = RecordingMode::OFF;
		playbackHandler.setLedStates();
	}

	processStarted = false;

	return;
}

/// resets scroll position so that you can see the current clip or first clip
/// in the top row of the grid
void StemExport::updateScrollPosition(StemExportType stemExportType, int32_t indexNumber) {
	if (stemExportType == StemExportType::CLIP) {
		// if we're in song row view, we'll reset the y scroll so we're back at the top
		if (currentSong->sessionLayout == SessionLayoutType::SessionLayoutTypeRows) {
			currentSong->songViewYScroll = indexNumber - kDisplayHeight;
		}
	}
	else if (stemExportType == StemExportType::TRACK) {
		// reset arranger view scrolling so we're back at the top left of the arrangement
		currentSong->xScroll[NAVIGATION_ARRANGEMENT] = 0;
		currentSong->arrangementYScroll = indexNumber - kDisplayHeight;
		arrangerView.repopulateOutputsOnScreen(false);
	}
}

/// display how many stems we've exported so far
void StemExport::displayStemExportProgress(StemExportType stemExportType) {
	if (display->haveOLED()) {
		displayStemExportProgressOLED(stemExportType);
	}
	else {
		displayStemExportProgress7SEG();
	}
}

void StemExport::displayStemExportProgressOLED(StemExportType stemExportType) {
	// if we're in the context menu for cancelling stem export, we don't want to show pop-ups
	if (inContextMenu()) {
		return;
	}
	DEF_STACK_STRING_BUF(exportStatus, 50);
	exportStatus.append("Exported ");
	exportStatus.appendInt(numStemsExported);
	exportStatus.append(" of ");
	exportStatus.appendInt(totalNumStemsToExport);
	if (stemExportType == StemExportType::CLIP) {
		exportStatus.append(" clips");
	}
	else if (stemExportType == StemExportType::TRACK) {
		exportStatus.append(" instruments");
	}
	deluge::hid::display::OLED::drawPermanentPopupLookingText(exportStatus.c_str());
}

void StemExport::displayStemExportProgress7SEG() {
	// if we're in the context menu for cancelling stem export, we don't want to show pop-ups
	if (inContextMenu()) {
		return;
	}
	DEF_STACK_STRING_BUF(exportStatus, 50);
	exportStatus.appendInt(totalNumStemsToExport - numStemsExported);
	display->setText(exportStatus.c_str(), true, 255, false);
}

/// based on Stem Export Type, will set a WAV file name in the format of:
/// /OutputType_StemExportType_OutputName_IndexNumber.WAV
/// example: /SYNTH_CLIP_BASS SYNTH_00000.WAV
/// example: /SYNTH_TRACK_BASS SYNTH_00000.WAV
/// this wavFileName is then concatenate to the filePath name to export the WAV file
void StemExport::setWavFileNameForStemExport(StemExportType stemExportType, Output* output, int32_t fileNumber) {
	// wavFileNameForStemExport = "/"
	Error error = wavFileNameForStemExport.set("/");
	if (error != Error::NONE) {
		return;
	}

	// wavFileNameForStemExport = "/OutputType
	const char* outputType;
	switch (output->type) {
	case OutputType::AUDIO:
		outputType = "AUDIO";
		break;
	case OutputType::SYNTH:
		outputType = "SYNTH";
		break;
	case OutputType::KIT:
		outputType = "KIT";
		break;
	default:
		break;
	}
	error = wavFileNameForStemExport.concatenate(outputType);
	if (error != Error::NONE) {
		return;
	}

	// wavFileNameForStemExport = "/OutputType_
	error = wavFileNameForStemExport.concatenate("_");
	if (error != Error::NONE) {
		return;
	}

	// wavFileNameForStemExport = "/OutputType_StemExportType_
	if (stemExportType == StemExportType::CLIP) {
		error = wavFileNameForStemExport.concatenate("CLIP_");
		if (error != Error::NONE) {
			return;
		}
	}
	else if (stemExportType == StemExportType::TRACK) {
		error = wavFileNameForStemExport.concatenate("TRACK_");
		if (error != Error::NONE) {
			return;
		}
	}

	// wavFileNameForStemExport = "/OutputType_StemExportType_OutputName
	error = wavFileNameForStemExport.concatenate(output->name.get());
	if (error != Error::NONE) {
		return;
	}

	// wavFileNameForStemExport = "/OutputType_StemExportType_OutputName_
	error = wavFileNameForStemExport.concatenate("_");
	if (error != Error::NONE) {
		return;
	}

	// wavFileNameForStemExport = "/OutputType_StemExportType_OutputName_#####
	error = wavFileNameForStemExport.concatenateInt(fileNumber, 5);
	if (error != Error::NONE) {
		return;
	}

	// wavFileNameForStemExport = "/OutputType_StemExportType_OutputName_#####.WAV
	error = wavFileNameForStemExport.concatenate(".WAV");
	if (error != Error::NONE) {
		return;
	}

	// set this flag to true so that the wavFileName set above is used when exporting
	wavFileNameForStemExportSet = true;
}

/// gets folder path in SAMPLES/STEMS to write stems to
/// within the STEMS folder, it will try to create a folder with the name of the SONG
/// if it cannot create a folder with the SONG name because it already exists, it will append
/// an incremental number to the end of the SONG name and try to create a folder with that new name
/// this function gets called every time a stem recording is being written to a file
/// to avoid unecessary file system calls, it will save the last song name saved to a String
/// including the last incremental folder number and use that to obtain the filePath for the next
/// stem export job (e.g. if you are exporting the same song more than once)
Error StemExport::getUnusedStemRecordingFilePath(String* filePath, AudioRecordingFolder folder) {

	const auto folderID = util::to_underlying(folder);

	Error error = storageManager.initSD();
	if (error != Error::NONE) {
		return error;
	}

	String tempPath;

	// set tempPath = SAMPLES/STEMS
	error = tempPath.set(audioRecordingFolderNames[folderID]);
	if (error != Error::NONE) {
		return error;
	}

	// try to create the STEMS folder if it doesn't exist
	FRESULT result = f_mkdir(tempPath.get());
	if (result != FR_OK && result != FR_EXIST) {
		return Error::FOLDER_DOESNT_EXIST;
	}

	// tempPath = SAMPLES/STEMS/
	error = tempPath.concatenate("/");
	if (error != Error::NONE) {
		return error;
	}

	// tempPath = SAMPLES/STEMS/*INSERT SONG NAME*
	if (currentSong->name.isEmpty()) { // if you have saved song yet
		error = tempPath.concatenate("UNSAVED");
	}
	else {
		error = tempPath.concatenate(currentSong->name.get());
	}
	if (error != Error::NONE) {
		return error;
	}

	// did we just export this song? if yes, no need to find folder number to append (we have it)
	if (strcmp(currentSong->name.get(), lastSongNameForStemExport.get())) {
		// if we're here we didn't just export this song
		String tempPathForSearch;

		// tempPathForSearch =  SAMPLES/STEMS/*INSERT SONG NAME*
		error = tempPathForSearch.set(tempPath.get());
		if (error != Error::NONE) {
			return error;
		}

		// we don't have a folder number yet, set it to -1 so when we increment below first potential
		// folder number appended is 00000 (-1 + 1)
		highestUsedStemFolderNumber = -1;

		// here we loop until we are able to successfully create a folder
		while (true) {
			// try to create folder
			FRESULT result = f_mkdir(tempPathForSearch.get());
			// successful, exit out of loop
			if (result == FR_OK) {
				break;
			}
			// not successful
			else {
				// increment folder number so we can append it to the SONG name
				highestUsedStemFolderNumber++;

				// tempPathForSearch =  SAMPLES/STEMS/*INSERT SONG NAME*
				error = tempPathForSearch.set(tempPath.get());
				if (error != Error::NONE) {
					return error;
				}

				// tempPathForSearch =  SAMPLES/STEMS/*INSERT SONG NAME*-
				error = tempPathForSearch.concatenate("-");
				if (error != Error::NONE) {
					return error;
				}

				// tempPathForSearch =  SAMPLES/STEMS/*INSERT SONG NAME*-#####
				error = tempPathForSearch.concatenateInt(highestUsedStemFolderNumber, 5);
				if (error != Error::NONE) {
					return error;
				}
			}
		}

		// copy folder path created above into the filePath so it can be used by the caller
		error = filePath->set(tempPathForSearch.get());
		if (error != Error::NONE) {
			return error;
		}
	}
	else {
		// if folder number is -1, it means this is the first time we're running the stem export
		// process for this song and the folder didn't previously exist so no number is being appended to it

		// if folder number is not -1, it means this is the second we're running the stem export process
		// for this song, so we need to append a folder number to the SONG name
		if (highestUsedStemFolderNumber != -1) {
			// tempPath =  SAMPLES/STEMS/*INSERT SONG NAME*-
			error = tempPath.concatenate("-");
			if (error != Error::NONE) {
				return error;
			}

			// tempPath =  SAMPLES/STEMS/*INSERT SONG NAME*-#####
			error = tempPath.concatenateInt(highestUsedStemFolderNumber, 5);
			if (error != Error::NONE) {
				return error;
			}
		}

		// copy folder path created above into the filePath so it can be used by the caller
		error = filePath->set(tempPath.get());
		if (error != Error::NONE) {
			return error;
		}
	}

	// save current song name as last song name exported
	error = lastSongNameForStemExport.set(currentSong->name.get());
	if (error != Error::NONE) {
		return error;
	}

	return Error::NONE;
}

/// used to check if we should exit out of context menu when recording ends
/// or if we should display progress pop-up
bool StemExport::inContextMenu() {
	if (getCurrentUI()->getUIType() == UIType::CONTEXT_MENU) {
		return true;
	}
	return false;
}
