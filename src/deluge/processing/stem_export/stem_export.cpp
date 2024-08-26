/*
 * Copyright (c) 2024 Sean Ditny
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
#include "gui/views/session_view.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "hid/led/indicator_leds.h"
#include "model/clip/clip.h"
#include "model/clip/instrument_clip.h"
#include "model/instrument/non_audio_instrument.h"
#include "model/note/note_row.h"
#include "model/song/song.h"
#include "playback/mode/arrangement.h"
#include "playback/mode/session.h"
#include "playback/playback_handler.h"
#include "processing/engines/audio_engine.h"
#include "scheduler_api.h"
#include "storage/audio/audio_file_manager.h"
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
	currentStemExportType = StemExportType::CLIP;
	processStarted = false;
	stopRecording = false;

	highestUsedStemFolderNumber = -1;
	wavFileNameForStemExportSet = false;

	numStemsExported = 0;
	totalNumStemsToExport = 0;

	loopLengthToStopStemExport = 0;
	loopEndPointInSamplesForAudioFile = 0;

	allowNormalization = false;
	allowNormalizationForDrums = true;
	exportToSilence = true;
	includeSongFX = false;
	includeKitFX = false;
	renderOffline = true;
	exportMixdown = false;

	timePlaybackStopped = 0xFFFFFFFF;
	timeThereWasLastSomeActivity = 0xFFFFFFFF;

	lastFolderNameForStemExport.clear();
}

/// starts stem export process which includes setting up UI mode, timer, and preparing
/// instruments / clips / kit rows for exporting
void StemExport::startStemExportProcess(StemExportType stemExportType) {
	// in case playback is active when you start stem export, stop it.
	stopPlayback();

	currentStemExportType = stemExportType;
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
	indicator_leds::blinkLed(IndicatorLED::BACK);

	// so that we can reset vertical scroll position
	int32_t elementsProcessed = 0;

	// export stems
	if (stemExportType == StemExportType::CLIP) {
		elementsProcessed = exportClipStems(stemExportType);
	}
	else if (stemExportType == StemExportType::TRACK) {
		elementsProcessed = exportInstrumentStems(stemExportType);
	}
	else if (stemExportType == StemExportType::DRUM) {
		elementsProcessed = exportDrumStems(stemExportType);
	}
	else if (stemExportType == StemExportType::MIXDOWN) {
		elementsProcessed = exportMixdownStem(stemExportType);
	}

	// if process wasn't cancelled, then we got here because we finished
	// exporting all the stems, so let's finish up
	if (isUIModeActive(UI_MODE_STEM_EXPORT)) {
		finishStemExportProcess(stemExportType, elementsProcessed);
	}
	else {
		processStarted = false;
		updateScrollPosition(stemExportType, elementsProcessed);
	}

	// turn off recording if it's still on
	if (playbackHandler.recording != RecordingMode::OFF) {
		playbackHandler.recording = RecordingMode::OFF;
		playbackHandler.setLedStates();
	}

	// re-render UI because view scroll positions and mute statuses will have been updated
	uiNeedsRendering(getCurrentUI());
	if (display->haveOLED()) {
		renderUIsForOled();
	}
	else {
		if (!rootUIIsClipMinderScreen()) {
			sessionView.redrawNumericDisplay();
		}
		// here is the right place to call InstrumentClipMinder::redrawNumericDisplay()
	}
}

/// Stop stem export process
void StemExport::stopStemExportProcess() {
	exitUIMode(UI_MODE_STEM_EXPORT);
	stopPlayback();
	highestUsedStemFolderNumber++;
	display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_STOP_EXPORT_STEMS), 6);
	indicator_leds::setLedState(IndicatorLED::BACK, false);
}

/// Simulate pressing record and play in order to trigger resampling of out output that ends when loop ends
void StemExport::startOutputRecordingUntilLoopEndAndSilence() {
	timePlaybackStopped = 0xFFFFFFFF;
	timeThereWasLastSomeActivity = 0xFFFFFFFF;
	playbackHandler.playButtonPressed(kInternalButtonPressLatency);
	if (playbackHandler.isEitherClockActive()) {
		// default - record the MIX (before SongFX)
		AudioInputChannel channel = AudioInputChannel::MIX;
		// if we want to record stems with SongFX
		if (includeSongFX) {
			// special input channel for offline rendering
			if (renderOffline) {
				channel = AudioInputChannel::OFFLINE_OUTPUT;
			}
			// record output for live rendering
			else {
				channel = AudioInputChannel::OUTPUT;
			}
		}
		bool normalization =
		    currentStemExportType == StemExportType::DRUM ? allowNormalizationForDrums : allowNormalization;
		audioRecorder.beginOutputRecording(AudioRecordingFolder::STEMS, channel, writeLoopEndPos(), normalization);
		if (audioRecorder.recordingSource > AudioInputChannel::NONE) {
			stopRecording = true;
		}
	}
}

/// simulate pressing play
void StemExport::stopPlayback() {
	if (playbackHandler.isEitherClockActive()) {
		playbackHandler.playButtonPressed(kInternalButtonPressLatency);
	}
}

/// simulate pressing record
void StemExport::stopOutputRecording() {
	// if playback has stopped and we're currently recording, check if we can stop recording
	if (!playbackHandler.isEitherClockActive() && playbackHandler.recording == RecordingMode::OFF) {
		// if silence is found and you are currently resampling, stop recording soon
		// if not exporting to silence, stop recording soon
		// if you cancelled stem export and exited out of UI mode, stop recording soon
		if (!isUIModeActive(UI_MODE_STEM_EXPORT) || !exportToSilence || (exportToSilence && checkForSilence())) {
			audioRecorder.endRecordingSoon();
			stopRecording = false;
		}
	}
}

/// if we're exporting clip stems in song or inside a clip (e.g. not arrangement tracks)
/// we want to export up to length of the longest sequence in the clip (clip or note row loop length)
/// when we reach longest loop length, we stop playback and allow recording to continue until silence
bool StemExport::checkForLoopEnd() {
	if (processStarted && currentStemExportType != StemExportType::TRACK) {
		int32_t currentPos =
		    playbackHandler.lastSwungTickActioned + playbackHandler.getNumSwungTicksInSinceLastActionedSwungTick();

		/* For debugging in case this stops working
		    DEF_STACK_STRING_BUF(popupMsg, 40);
		    popupMsg.append("Current Pos: ");
		    popupMsg.appendInt(currentPos);
		    popupMsg.append("/n");
		    popupMsg.append("Length: ");
		    popupMsg.appendInt(loopLength);
		    display->displayPopup(popupMsg.c_str());
		*/

		if (currentPos == loopLengthToStopStemExport) {
			playbackHandler.endPlayback();
			return true;
		}
	}
	return false;
}

/// we want to check for 12 seconds of silence so we can stop recording
/// if we don't find silence after 60 seconds, stop recording
bool StemExport::checkForSilence() {
	// if this is the first time we are checking for silence, it means we just stopped playback
	// so save this time so we can keep track of how long we've been checking for silence
	if (timePlaybackStopped == 0xFFFFFFFF) {
		timePlaybackStopped = AudioEngine::audioSampleTimer;
		timeThereWasLastSomeActivity = AudioEngine::audioSampleTimer;
	}

	// have we been checking for silence for 60 seconds or longer? then stop recording
	if ((uint32_t)(AudioEngine::audioSampleTimer - timePlaybackStopped) >= (kSampleRate * 60)) {
		return true;
	}

	// get current level to check for silence
	float approxRMSLevel = std::max(AudioEngine::approxRMSLevel.l, AudioEngine::approxRMSLevel.r);
	if (approxRMSLevel < 9) {
		// has 12 seconds of silence elapsed since we first detected silence? then stop recording
		if ((uint32_t)(AudioEngine::audioSampleTimer - timeThereWasLastSomeActivity) >= (kSampleRate * 12)) {
			return true;
		}
	}
	else {
		// if we're here, then the track is not silent yet
		// save last time we detected activity
		timeThereWasLastSomeActivity = AudioEngine::audioSampleTimer;
	}
	return false;
}

/// disarms and prepares all the instruments so that they can be exported
int32_t StemExport::disarmAllInstrumentsForStemExport(StemExportType stemExportType) {
	// when we begin stem export, we haven't exported any instruments yet, so initialize these variables
	numStemsExported = 0;
	totalNumStemsToExport = 0;
	// when we trigger stem export, we don't know how many instruments there are yet
	// so get the number and store it so we only need to ping getNumElements once
	int32_t totalNumOutputs = currentSong->getNumOutputs();

	if (totalNumOutputs != 0) {
		// iterate through all the instruments to disable all the recording relevant flags
		for (int32_t idxOutput = 0; idxOutput < totalNumOutputs; ++idxOutput) {
			Output* output = currentSong->getOutputFromIndex(idxOutput);
			if (output != nullptr) {
				/* export output stem if all these conditions are met:
				    1) the output is not muted in arranger
				    2) the output is not empty (it has clips with notes in them)
				    3) the output type is not MIDI or CV
				*/
				OutputType outputType = output->type;
				if (!output->mutedInArrangementMode && !output->isEmpty(false) && outputType != OutputType::MIDI_OUT
				    && outputType != OutputType::CV) {
					output->exportStem = true;
					totalNumStemsToExport++;
				}
				else {
					output->exportStem = false;
				}
				// if we're not exporting the mixdown,
				// then we want to mute all the tracks as we'll be exporting them individually
				// except for the MIDI transpose track, which must remain enabled for proper transposition
				if (stemExportType != StemExportType::MIXDOWN) {
					output->mutedInArrangementModeBeforeStemExport = output->mutedInArrangementMode;
					output->mutedInArrangementMode =
					    (output->type != OutputType::MIDI_OUT
					     || ((NonAudioInstrument*)output)->getChannel() != MIDI_CHANNEL_TRANSPOSE);
				}
				output->recordingInArrangement = false;
				output->armedForRecording = false;
				output->soloingInArrangementMode = false;
			}
		}
	}

	// if exporting mixdown, just exporting one stem
	if ((stemExportType == StemExportType::MIXDOWN) && totalNumStemsToExport) {
		totalNumStemsToExport = 1;
	}
	return totalNumOutputs;
}

/// set instrument mutes back to their previous state (before exporting stems)
void StemExport::restoreAllInstrumentMutes(int32_t totalNumOutputs) {
	if (totalNumOutputs != 0) {
		// iterate through all the instruments to restore previous mute states
		for (int32_t idxOutput = 0; idxOutput < totalNumOutputs; ++idxOutput) {
			Output* output = currentSong->getOutputFromIndex(idxOutput);
			if (output != nullptr) {
				output->mutedInArrangementMode = output->mutedInArrangementModeBeforeStemExport;
			}
		}
	}
}

/// iterates through all instruments, arming one instrument at a time for recording
/// simulates the button combo action of pressing record + play twice to enable resample
/// and stop recording at the end of the arrangement
int32_t StemExport::exportInstrumentStems(StemExportType stemExportType) {
	// prepare all the instruments for stem export
	int32_t totalNumOutputs = disarmAllInstrumentsForStemExport(stemExportType);

	if (totalNumOutputs != 0 && totalNumStemsToExport != 0) {
		// now we're going to iterate through all instruments to find the ones that should be exported
		for (int32_t idxOutput = totalNumOutputs - 1; idxOutput >= 0; --idxOutput) {
			Output* output = currentSong->getOutputFromIndex(idxOutput);
			if (output != nullptr) {
				bool started = startCurrentStemExport(stemExportType, output, output->mutedInArrangementMode, idxOutput,
				                                      output->exportStem);

				if (!started) {
					// skip this stem and move to the next one
					continue;
				}

				// wait until recording is done and playback is turned off
				yield([]() {
					if (stemExport.stopRecording) {
						stemExport.stopOutputRecording();
					}
					return !(playbackHandler.recording != RecordingMode::OFF
					         || audioRecorder.recordingSource > AudioInputChannel::NONE
					         || playbackHandler.isEitherClockActive());
				});

				finishCurrentStemExport(stemExportType, output->mutedInArrangementMode);
			}
			// in the event that stem exporting is cancelled while iterating through clips
			// break out of the loop
			if (!isUIModeActive(UI_MODE_STEM_EXPORT)) {
				break;
			}
		}
	}

	// set instrument mutes back to their previous state (before exporting stems)
	restoreAllInstrumentMutes(totalNumOutputs);

	return totalNumOutputs;
}

/// iterates through all instruments, checking if there's any that should be exported (unmuted)
/// then exports them all as a single stem
/// simulates the button combo action of pressing record + play twice to enable resample
/// and stop recording at the end of the arrangement
int32_t StemExport::exportMixdownStem(StemExportType stemExportType) {
	// prepare all the instruments for stem export
	int32_t totalNumOutputs = disarmAllInstrumentsForStemExport(stemExportType);

	if (totalNumOutputs != 0 && totalNumStemsToExport != 0) {
		// set wav file name for stem to be exported
		setWavFileNameForStemExport(stemExportType, nullptr, 0);

		// start resampling which ends when end of arrangement is reached and audio is silent
		startOutputRecordingUntilLoopEndAndSilence();

		// we haven't exported the arrangement yet
		// so display progress
		displayStemExportProgress(stemExportType);

		// wait until recording is done and playback is turned off
		yield([]() {
			if (stemExport.stopRecording) {
				stemExport.stopOutputRecording();
			}
			return !(playbackHandler.recording != RecordingMode::OFF
			         || audioRecorder.recordingSource > AudioInputChannel::NONE
			         || playbackHandler.isEitherClockActive());
		});

		// update number of stems exported
		numStemsExported++;
	}

	return totalNumOutputs;
}

/// disarms and prepares all the clips so that they can be exported
int32_t StemExport::disarmAllClipsForStemExport() {
	// when we begin stem export, we haven't exported any clips yet, so initialize these variables
	numStemsExported = 0;
	totalNumStemsToExport = 0;
	currentSong->xScroll[NAVIGATION_CLIP] = 0;

	// when we trigger stem export, we don't know how many clips there are yet
	// so get the number and store it so we only need to ping getNumElements once
	int32_t totalNumClips = currentSong->sessionClips.getNumElements();

	if (totalNumClips != 0) {
		// iterate through all clips to disable all the recording relevant flags
		for (int32_t idxClip = 0; idxClip < totalNumClips; ++idxClip) {
			Clip* clip = currentSong->sessionClips.getClipAtIndex(idxClip);
			if (clip != nullptr) {
				/* export clip stem if all these conditions are met:
				    1) the clip is not empty (it has notes in it)
				    2) the output type is not MIDI or CV
				*/
				OutputType outputType = clip->output->type;
				if (!clip->isEmpty(false) && outputType != OutputType::MIDI_OUT && outputType != OutputType::CV) {
					clip->exportStem = true;
					totalNumStemsToExport++;
				}
				else {
					clip->exportStem = false;
				}
				clip->activeIfNoSoloBeforeStemExport = clip->activeIfNoSolo;
				clip->activeIfNoSolo = false;
				clip->armState = ArmState::OFF;
				clip->armedForRecording = false;
				clip->soloingInSessionMode = false;
			}
		}
	}
	return totalNumClips;
}

/// set clip mutes back to their previous state (before exporting stems)
void StemExport::restoreAllClipMutes(int32_t totalNumClips) {
	if (totalNumClips != 0) {
		// iterate through all clips to restore previous mute states
		for (int32_t idxClip = 0; idxClip < totalNumClips; ++idxClip) {
			Clip* clip = currentSong->sessionClips.getClipAtIndex(idxClip);
			if (clip != nullptr) {
				clip->activeIfNoSolo = clip->activeIfNoSoloBeforeStemExport;
			}
		}
	}
}

/// for clip export, gets length of longest note row that isn't empty
/// we will use this length to record that clip until longest note row is fully recorded
void StemExport::getLoopLengthOfLongestNotEmptyNoteRow(Clip* clip) {
	loopLengthToStopStemExport = clip->loopLength;

	if (clip->type == ClipType::INSTRUMENT) {
		InstrumentClip* instrumentClip = (InstrumentClip*)clip;
		int32_t totalNumNoteRows = instrumentClip->noteRows.getNumElements();
		if (totalNumNoteRows != 0) {
			// iterate through all the note rows to find the longest one
			for (int32_t idxNoteRow = 0; idxNoteRow < totalNumNoteRows; ++idxNoteRow) {
				NoteRow* thisNoteRow = instrumentClip->noteRows.getElement(idxNoteRow);
				if ((thisNoteRow != nullptr) && (thisNoteRow->loopLengthIfIndependent > loopLengthToStopStemExport)
				    && !thisNoteRow->hasNoNotes()) {
					loopLengthToStopStemExport = thisNoteRow->loopLengthIfIndependent;
				}
			}
		}
	}
}

/// converts clip / drum loop length into samples so that clip / drum end position can be written to clip / drum stem
void StemExport::getLoopEndPointInSamplesForAudioFile(int32_t loopLength) {
	loopEndPointInSamplesForAudioFile = loopLength * playbackHandler.getTimePerInternalTick();
}

/// determines whether or not you should write loop end position in samples to the stem file
/// we're only writing loop end marker to clip and drum stems
bool StemExport::writeLoopEndPos() {
	if (processStarted
	    && (currentStemExportType == StemExportType::CLIP || currentStemExportType == StemExportType::DRUM)) {
		return true;
	}
	return false;
}

/// iterates through all clips, arming one clip at a time for recording
/// simulates the button combo action of pressing record + play twice to enable resample
/// and stop recording at the end of the clip's loop length
int32_t StemExport::exportClipStems(StemExportType stemExportType) {
	// prepare all the clips for stem export
	int32_t totalNumClips = disarmAllClipsForStemExport();

	if (totalNumClips != 0 && totalNumStemsToExport != 0) {
		// now we're going to iterate through all clips to find the ones that should be exported
		for (int32_t idxClip = totalNumClips - 1; idxClip >= 0; --idxClip) {
			Clip* clip = currentSong->sessionClips.getClipAtIndex(idxClip);
			if (clip != nullptr) {
				getLoopLengthOfLongestNotEmptyNoteRow(clip);
				getLoopEndPointInSamplesForAudioFile(clip->loopLength);

				bool started = startCurrentStemExport(stemExportType, clip->output, clip->activeIfNoSolo, idxClip,
				                                      clip->exportStem);

				if (!started) {
					// skip this stem and move to the next one
					continue;
				}

				// wait until recording is done and playback is turned off
				yield([]() {
					// if you haven't found silence yet and playback has stopped
					// check for silence so you can stop recording
					if (stemExport.stopRecording) {
						stemExport.stopOutputRecording();
					}
					return !(playbackHandler.recording != RecordingMode::OFF
					         || audioRecorder.recordingSource > AudioInputChannel::NONE
					         || playbackHandler.isEitherClockActive());
				});

				finishCurrentStemExport(stemExportType, clip->activeIfNoSolo);
			}
			// in the event that stem exporting is cancelled while iterating through clips
			// break out of the loop
			if (!isUIModeActive(UI_MODE_STEM_EXPORT)) {
				break;
			}
		}
	}

	// set clip mutes back to their previous state (before exporting stems)
	restoreAllClipMutes(totalNumClips);

	return totalNumClips;
}

/// disarms and prepares all the drums so that they can be exported
int32_t StemExport::disarmAllDrumsForStemExport() {
	// when we begin stem export, we haven't exported any drums yet, so initialize these variables
	numStemsExported = 0;
	totalNumStemsToExport = 0;
	currentSong->xScroll[NAVIGATION_CLIP] = 0;

	InstrumentClip* clip = getCurrentInstrumentClip();
	OutputType outputType = clip->output->type;

	// when we trigger stem export, we don't know how many drums there are yet
	// so get the number and store it so we only need to ping getNumElements once
	int32_t totalNumNoteRows = clip->noteRows.getNumElements();

	if (totalNumNoteRows != 0) {
		// iterate through all the drums to disable all the recording relevant flags
		for (int32_t idxNoteRow = 0; idxNoteRow < totalNumNoteRows; ++idxNoteRow) {
			NoteRow* thisNoteRow = clip->noteRows.getElement(idxNoteRow);
			if (thisNoteRow != nullptr) {
				/* export drum stem if all these conditions are met:
				    1) the note row is not muted
				    2) the note row is not empty (it has notes)
				    3) it has a drum assigned to it
				    4) the drum assigned to it is a sound drum
				*/
				if (thisNoteRow->drum != nullptr && thisNoteRow->drum->type == DrumType::SOUND && !thisNoteRow->muted
				    && !thisNoteRow->hasNoNotes()) {
					thisNoteRow->exportStem = true;
					totalNumStemsToExport++;
				}
				else {
					thisNoteRow->exportStem = false;
				}
				thisNoteRow->mutedBeforeStemExport = thisNoteRow->muted;
				thisNoteRow->muted = true;
			}
		}
	}

	return totalNumNoteRows;
}

/// set drum mutes back to their previous state (before exporting stems)
void StemExport::restoreAllDrumMutes(int32_t totalNumNoteRows) {
	// iterate through all the drums to restore previous mute states
	InstrumentClip* clip = getCurrentInstrumentClip();
	for (int32_t idxNoteRow = 0; idxNoteRow < totalNumNoteRows; ++idxNoteRow) {
		NoteRow* thisNoteRow = clip->noteRows.getElement(idxNoteRow);
		if (thisNoteRow != nullptr) {
			thisNoteRow->muted = thisNoteRow->mutedBeforeStemExport;
		}
	}
}

/// iterates through all drums, arming one drum at a time for recording
/// simulates the button combo action of pressing record + play twice to enable resample
/// and stop recording at the end of the arrangement
int32_t StemExport::exportDrumStems(StemExportType stemExportType) {
	// need to disarm all the other clips so that we can export just this kit clip
	int32_t totalNumClips = disarmAllClipsForStemExport();
	// prepare all the drums for stem export
	int32_t totalNumNoteRows = disarmAllDrumsForStemExport();

	if (totalNumNoteRows != 0) {
		// now we're going to iterate through all drums to find the ones that should be exported
		InstrumentClip* clip = getCurrentInstrumentClip();
		Output* output = clip->output;
		OutputType outputType = output->type;
		for (int32_t idxNoteRow = totalNumNoteRows - 1; idxNoteRow >= 0; --idxNoteRow) {
			NoteRow* thisNoteRow = clip->noteRows.getElement(idxNoteRow);
			if (thisNoteRow != nullptr) {
				clip->activeIfNoSolo = true; // unmute clip

				// set the loop length that the drum stem export should be stopped at
				if (thisNoteRow->loopLengthIfIndependent != 0) {
					loopLengthToStopStemExport = thisNoteRow->loopLengthIfIndependent;
				}
				else {
					loopLengthToStopStemExport = clip->loopLength;
				}
				getLoopEndPointInSamplesForAudioFile(loopLengthToStopStemExport);

				bool started = startCurrentStemExport(stemExportType, output, thisNoteRow->muted, idxNoteRow,
				                                      thisNoteRow->exportStem, (SoundDrum*)thisNoteRow->drum);

				if (!started) {
					// skip this stem and move to the next one
					continue;
				}

				// wait until recording is done and playback is turned off
				yield([]() {
					// if you haven't found silence yet and playback has stopped
					// check for silence so you can stop recording
					if (stemExport.stopRecording) {
						stemExport.stopOutputRecording();
					}
					return !(playbackHandler.recording != RecordingMode::OFF
					         || audioRecorder.recordingSource > AudioInputChannel::NONE
					         || playbackHandler.isEitherClockActive());
				});

				finishCurrentStemExport(stemExportType, thisNoteRow->muted);
			}
			// in the event that stem exporting is cancelled while iterating through drums
			// break out of the loop
			if (!isUIModeActive(UI_MODE_STEM_EXPORT)) {
				break;
			}
		}
	}

	// set drum mutes back to their previous state (before exporting stems)
	restoreAllDrumMutes(totalNumNoteRows);
	// set clip mutes back to their previous state (before exporting stems)
	restoreAllClipMutes(totalNumClips);

	return totalNumNoteRows;
}

bool StemExport::startCurrentStemExport(StemExportType stemExportType, Output* output, bool& muteState,
                                        int32_t indexNumber, bool exportStem, SoundDrum* drum) {
	updateScrollPosition(stemExportType, indexNumber + 1);

	// exclude empty clips / outputs, muted outputs (arranger), MIDI and CV outputs
	if (!exportStem) {
		return false;
	}

	if (stemExportType == StemExportType::CLIP) {
		// unmute clip for recording
		muteState = true; // clip->activeIfNoSolo
	}
	else if (stemExportType == StemExportType::TRACK || stemExportType == StemExportType::DRUM) {
		// unmute output / drum for recording
		muteState = false; // output->mutedInArrangementMode or noteRow->muted
	}

	// re-render song view since we scrolled and updated mutes
	uiNeedsRendering(getCurrentUI());

	// set wav file name for stem to be exported
	setWavFileNameForStemExport(stemExportType, output, indexNumber, drum);

	// start resampling which ends when end of track / clip is reached and audio is silent
	startOutputRecordingUntilLoopEndAndSilence();

	// we haven't exported all the track / clips yet
	// so display the number of tracks / clips we've exported so far
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
	else if (stemExportType == StemExportType::TRACK || stemExportType == StemExportType::DRUM) {
		// mute output / drum for recording
		muteState = true; // output->mutedInArrangementMode or noteRow->muted
	}

	// update number of stems exported
	numStemsExported++;
}

// if we know how many stems to export, we can check if we've already exported all the
// stems and are therefore done and should exit out of the stem export UI mode
void StemExport::finishStemExportProcess(StemExportType stemExportType, int32_t elementsProcessed) {
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
	updateScrollPosition(stemExportType, elementsProcessed);

	processStarted = false;

	indicator_leds::setLedState(IndicatorLED::BACK, false);

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
	else if (stemExportType == StemExportType::TRACK || stemExportType == StemExportType::MIXDOWN) {
		// reset arranger view scrolling so we're back at the top left of the arrangement
		currentSong->xScroll[NAVIGATION_ARRANGEMENT] = 0;
		currentSong->arrangementYScroll = indexNumber - kDisplayHeight;
		arrangerView.repopulateOutputsOnScreen(false);
	}
	else if (stemExportType == StemExportType::DRUM) {
		// reset clip view scrolling so we're back at the top left of the kit
		currentSong->xScroll[NAVIGATION_CLIP] = 0;
		getCurrentInstrumentClip()->yScroll = indexNumber - kDisplayHeight;
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
	hid::display::OLED::clearMainImage();
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
	else if (stemExportType == StemExportType::DRUM) {
		exportStatus.append(" drums");
	}
	deluge::hid::display::OLED::drawPermanentPopupLookingText(exportStatus.c_str());
	deluge::hid::display::OLED::markChanged();
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

// creates the full file path for stem exporting including the stem folder structure and wav file name
Error StemExport::getUnusedStemRecordingFilePath(String* filePath, AudioRecordingFolder folder) {
	const auto folderID = util::to_underlying(folder);

	Error error = StorageManager::initSD();
	if (error != Error::NONE) {
		return error;
	}

	error = getUnusedStemRecordingFolderPath(filePath, folder);
	if (error != Error::NONE) {
		return error;
	}

	// wavFileName is uniquely set for each stem export
	// when this flag is true, there is a valid wavFileName that has been set for stem exporting
	if (wavFileNameForStemExportSet) {
		// reset flag to false to ensure that next stem exported is valid
		wavFileNameForStemExportSet = false;

		error = filePath->concatenate(wavFileNameForStemExport.get());
		if (error != Error::NONE) {
			return error;
		}
	}
	// otherwise we default to regular /REC#####.WAV naming convention
	else {
		error = filePath->concatenate("/REC");
		if (error != Error::NONE) {
			return error;
		}
		audioFileManager.highestUsedAudioRecordingNumber[folderID]++;
		error = filePath->concatenateInt(audioFileManager.highestUsedAudioRecordingNumber[folderID], 5);
		if (error != Error::NONE) {
			return error;
		}
		error = filePath->concatenate(".WAV");
		if (error != Error::NONE) {
			return error;
		}
	}

	return Error::NONE;
}

/// gets folder path in SAMPLES/EXPORTS to write stems to
/// within the STEMS folder, it will try to create a folder with the name of the SONG
/// if it cannot create a folder with the SONG name because it already exists, it will continue creating folder path
/// if it cannot create a folder and the folder does not already exist, then function will return an error
/// after SAMPLES/EXPORTS/*SONG NAME*/ is created, it will try to create a folder for the type of export.
/// if it cannot create a folder because it already exists, it will append an incremental number to the end of the
/// folder name and try to create a folder with that new name. thus we will end up with a folder path of
/// SAMPLES/EXPORTS/*SONG NAME*/TRACKS##/ or SAMPLES/EXPORTS/*SONG NAME*/CLIPS##/ or SAMPLES/EXPORTS/*SONG
/// NAME*/DRUMS##/ this function gets called every time a stem recording is being written to a file to avoid unecessary
/// file system calls, it will save the last song and sub-folder name saved to a String including the last
/// incremental folder number and use that to obtain the filePath for the next stem export job (e.g. if you are
/// exporting the same song more and stem export type than once)
Error StemExport::getUnusedStemRecordingFolderPath(String* filePath, AudioRecordingFolder folder) {

	const auto folderID = util::to_underlying(folder);

	Error error = StorageManager::initSD();
	if (error != Error::NONE) {
		return error;
	}

	String tempPath;

	// set tempPath = SAMPLES/EXPORTS
	error = tempPath.set(audioRecordingFolderNames[folderID]);
	if (error != Error::NONE) {
		return error;
	}

	// try to create the STEMS folder if it doesn't exist
	FRESULT result = f_mkdir(tempPath.get());
	// if we couldn't create folder and it doesn't exist, return error
	if (result != FR_OK && result != FR_EXIST) {
		return fresultToDelugeErrorCode(result);
	}

	// tempPath = SAMPLES/EXPORTS/
	error = tempPath.concatenate("/");
	if (error != Error::NONE) {
		return error;
	}

	// tempPath = SAMPLES/EXPORTS/*INSERT SONG NAME*
	if (currentSong->name.isEmpty()) { // if you have saved song yet
		error = tempPath.concatenate("UNSAVED");
	}
	else {
		error = tempPath.concatenate(currentSong->name.get());
	}
	if (error != Error::NONE) {
		return error;
	}

	// try to create folder
	result = f_mkdir(tempPath.get());
	// if we couldn't create folder and it doesn't exist, return error
	if (result != FR_OK && result != FR_EXIST) {
		return fresultToDelugeErrorCode(result);
	}

	switch (currentStemExportType) {
	case StemExportType::CLIP:
		// tempPath =  SAMPLES/EXPORTS/*INSERT SONG NAME*/CLIPS
		error = tempPath.concatenate("/CLIPS");
		break;
	case StemExportType::DRUM:
		// tempPath =  SAMPLES/EXPORTS/*INSERT SONG NAME*/DRUMS
		error = tempPath.concatenate("/DRUMS");
		break;
	case StemExportType::MIXDOWN:
		[[fallthrough]];
	case StemExportType::TRACK:
		// tempPath =  SAMPLES/EXPORTS/*INSERT SONG NAME*/CLIPS
		error = tempPath.concatenate("/TRACKS");
		break;
	default:
		break;
	}

	String folderNameToCompare;
	error = folderNameToCompare.set(tempPath.get());
	if (error != Error::NONE) {
		return error;
	}

	// did we just export this same folder?
	// if yes, no need to find folder number to append (we have it)
	if (strcmp(folderNameToCompare.get(), lastFolderNameForStemExport.get())) {
		// if we're here we didn't just export this song
		String tempPathForSearch;

		// tempPathForSearch =  SAMPLES/EXPORTS/*INSERT SONG NAME*/TRACKS
		// or tempPathForSearch =  SAMPLES/EXPORTS/*INSERT SONG NAME*/CLIPS
		// or tempPathForSearch =  SAMPLES/EXPORTS/*INSERT SONG NAME*/DRUMS
		error = tempPathForSearch.set(tempPath.get());
		if (error != Error::NONE) {
			return error;
		}

		// we don't have a folder number yet, set it to -1 so when we increment below first potential
		// folder number appended is 00 (-1 + 1)
		highestUsedStemFolderNumber = -1;

		// here we loop until we are able to successfully create a folder
		while (true) {
			// try to create folder
			result = f_mkdir(tempPathForSearch.get());
			// successful, exit out of loop
			if (result == FR_OK) {
				break;
			}
			// not successful
			else {
				// increment folder number so we can append it to the folder name
				highestUsedStemFolderNumber++;

				// tempPathForSearch =  SAMPLES/EXPORTS/*INSERT SONG NAME*/TRACKS
				// or tempPathForSearch =  SAMPLES/EXPORTS/*INSERT SONG NAME*/CLIPS
				// or tempPathForSearch =  SAMPLES/EXPORTS/*INSERT SONG NAME*/DRUMS
				error = tempPathForSearch.set(tempPath.get());
				if (error != Error::NONE) {
					return error;
				}

				// tempPathForSearch =  SAMPLES/EXPORTS/*INSERT SONG NAME*/TRACKS-
				// or tempPathForSearch =  SAMPLES/EXPORTS/*INSERT SONG NAME*/CLIPS-
				// or tempPathForSearch =  SAMPLES/EXPORTS/*INSERT SONG NAME*/DRUMS-
				error = tempPathForSearch.concatenate("-");
				if (error != Error::NONE) {
					return error;
				}

				// tempPathForSearch =  SAMPLES/EXPORTS/*INSERT SONG NAME*/TRACKS-##
				// or tempPathForSearch =  SAMPLES/EXPORTS/*INSERT SONG NAME*/CLIPS-##
				// or tempPathForSearch =  SAMPLES/EXPORTS/*INSERT SONG NAME*/DRUMS-##
				error = tempPathForSearch.concatenateInt(highestUsedStemFolderNumber, 2);
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
			// tempPath =  SAMPLES/EXPORTS/*INSERT SONG NAME*/TRACKS-
			// or tempPath =  SAMPLES/EXPORTS/*INSERT SONG NAME*/CLIPS-
			// or tempPath =  SAMPLES/EXPORTS/*INSERT SONG NAME*/DRUMS-
			error = tempPath.concatenate("-");
			if (error != Error::NONE) {
				return error;
			}

			// tempPath =  SAMPLES/EXPORTS/*INSERT SONG NAME*/TRACKS-##
			// or tempPath =  SAMPLES/EXPORTS/*INSERT SONG NAME*/CLIPS-##
			// or tempPath =  SAMPLES/EXPORTS/*INSERT SONG NAME*/DRUMS-##
			error = tempPath.concatenateInt(highestUsedStemFolderNumber, 2);
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

	// save current folder name as last folder name exported
	error = lastFolderNameForStemExport.set(folderNameToCompare.get());
	if (error != Error::NONE) {
		return error;
	}

	return Error::NONE;
}

/// based on Stem Export Type, will set a WAV file name in the format of:
/// /OutputType_StemExportType_OutputName_IndexNumber.WAV
/// example: /SYNTH_CLIP_BASS SYNTH_TEMPO_ROOT NOTE-SCALE_00000.WAV
/// example: /SYNTH_TRACK_BASS SYNTH_TEMPO_ROOT NOTE-SCALE_00000.WAV
/// example: /MIXDOWN_TEMPO_ROOT NOTE-SCALE.WAV
/// example: /KIT_DRUM_808 KIT_SNARE_ROOT NOTE_SCALE_00000.WAV
/// this wavFileName is then concatenate to the filePath name to export the WAV file
void StemExport::setWavFileNameForStemExport(StemExportType stemExportType, Output* output, int32_t fileNumber,
                                             SoundDrum* drum) {
	// wavFileNameForStemExport = "/"
	Error error = wavFileNameForStemExport.set("/");
	if (error != Error::NONE) {
		return;
	}

	const char* outputType;
	const char* exportType;

	if (stemExportType == StemExportType::MIXDOWN) {
		// wavFileNameForStemExport = "/MIXDOWN
		exportType = "MIXDOWN";
	}
	else {
		// wavFileNameForStemExport = "/OutputType
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
	}

	const char* outputName;

	if (stemExportType != StemExportType::MIXDOWN) {
		// wavFileNameForStemExport = "/OutputType_StemExportType_
		if (stemExportType == StemExportType::CLIP) {
			exportType = "CLIP";
		}
		else if (stemExportType == StemExportType::TRACK) {
			exportType = "TRACK";
		}
		else if (stemExportType == StemExportType::DRUM) {
			exportType = "DRUM";
		}

		// wavFileNameForStemExport = /OutputType_StemExportType_OutputName
		outputName = output->name.get();
	}

	// get song tempo
	int32_t tempo = std::round(playbackHandler.calculateBPMForDisplay());

	// get song root note
	char noteName[5];
	int32_t isNatural = 1; // gets modified inside noteCodeToString to be 0 if sharp.
	noteCodeToString(currentSong->key.rootNote, noteName, &isNatural);

	// get song scale
	const char* scaleName = getScaleName(currentSong->getCurrentScale());

	char fileName[300];

	// wavFileNameForStemExport = /StemExportType_tempo_noteName-scaleName.WAV
	if (stemExportType == StemExportType::MIXDOWN) {
		sprintf(fileName, "%s_%dBPM_%s-%s.WAV", exportType, tempo, noteName, scaleName);
	}
	// wavFileNameForStemExport = /OutputType_StemExportType_OutputName_DrumName_tempo_noteName-scaleName_###.WAV
	else if (stemExportType == StemExportType::DRUM) {
		sprintf(fileName, "%s_%s_%s_%s_%dBPM_%s-%s_%03d.WAV", outputType, exportType, outputName, drum->name.get(),
		        tempo, noteName, scaleName, fileNumber);
	}
	// wavFileNameForStemExport = /OutputType_StemExportType_OutputName_tempo_noteName-scaleName_###.WAV
	else {
		sprintf(fileName, "%s_%s_%s_%dBPM_%s-%s_%03d.WAV", outputType, exportType, outputName, tempo, noteName,
		        scaleName, fileNumber);
	}

	error = wavFileNameForStemExport.concatenate(fileName);
	if (error != Error::NONE) {
		return;
	}

	// set this flag to true so that the wavFileName set above is used when exporting
	wavFileNameForStemExportSet = true;
}

/// used to check if we should exit out of context menu when recording ends
/// or if we should display progress pop-up
bool StemExport::inContextMenu() {
	if (getCurrentUI()->getUIType() == UIType::CONTEXT_MENU) {
		return true;
	}
	return false;
}
