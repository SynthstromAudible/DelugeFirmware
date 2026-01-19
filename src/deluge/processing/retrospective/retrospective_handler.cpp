/*
 * Copyright © 2024 Synthstrom Audible Limited
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

#include "retrospective_handler.h"
#include "definitions_cxx.hpp"
#include "extern.h"
#include "gui/l10n/l10n.h"
#include "gui/ui/sample_marker_editor.h"
#include "gui/ui/sound_editor.h"
#include "gui/views/instrument_clip_view.h"
#include "hid/display/display.h"
#include "hid/led/pad_leds.h"
#include "memory/general_memory_allocator.h"
#include "model/clip/instrument_clip.h"
#include "model/instrument/kit.h"
#include "model/note/note_row.h"
#include "model/sample/sample.h"
#include "model/settings/runtime_feature_settings.h"
#include "model/song/song.h"
#include "modulation/params/param_manager.h"
#include "processing/sound/sound.h"
#include "processing/sound/sound_drum.h"
#include "processing/source.h"
#include "retrospective_buffer.h"
#include "storage/multi_range/multi_range.h"
#include "storage/multi_range/multisample_range.h"

/// Helper to create a new SoundDrum for a kit
static SoundDrum* createNewDrumForKit(Kit* kit) {
	// Create unique name for the drum
	String drumName;
	drumName.set("RETR");
	Error error = kit->makeDrumNameUnique(&drumName, 1);
	if (error != Error::NONE) {
		return nullptr;
	}

	// Allocate memory for the drum
	void* memory = GeneralMemoryAllocator::get().allocMaxSpeed(sizeof(SoundDrum));
	if (!memory) {
		return nullptr;
	}

	// Set up param manager
	ParamManagerForTimeline paramManager;
	error = paramManager.setupWithPatching();
	if (error != Error::NONE) {
		delugeDealloc(memory);
		return nullptr;
	}

	// Initialize and create the drum
	Sound::initParams(&paramManager);
	SoundDrum* newDrum = new (memory) SoundDrum();
	newDrum->setupAsSample(&paramManager);

	// Back up param manager and set name
	currentSong->backUpParamManager(newDrum, currentSong->getCurrentClip(), &paramManager, true);
	newDrum->name.set(&drumName);
	newDrum->nameIsDiscardable = true;

	return newDrum;
}

void handleRetrospectiveSave() {
	// Show feedback so user knows something is happening
	// If normalization is enabled, show that popup since it takes longer
	if (runtimeFeatureSettings.isOn(RuntimeFeatureSettingType::RetrospectiveSamplerNormalize)) {
		display->displayPopup(l10n::getView(l10n::String::STRING_FOR_RETRO_NORMALIZING));
	}
	else {
		display->displayPopup(l10n::getView(l10n::String::STRING_FOR_RETRO_SAVING));
	}

	// Save retrospective buffer to file
	String filePath;
	Error error = retrospectiveBuffer.saveToFile(&filePath);

	if (error == Error::NONE) {
		// Check if we're in a kit context
		bool loadedToPad = false;

		if (getCurrentOutputType() == OutputType::KIT) {
			Kit* kit = getCurrentKit();

			if (kit) {
				// Show loading popup early - covers drum creation and file loading
				display->displayPopup(l10n::getView(l10n::String::STRING_FOR_RETRO_LOADING));

				// Always create a new drum row when in a kit
				InstrumentClip* clip = getCurrentInstrumentClip();
				if (clip) {
					char modelStackMemory[MODEL_STACK_MAX_SIZE];
					ModelStackWithTimelineCounter* modelStack =
					    currentSong->setupModelStackWithCurrentClip(modelStackMemory);

					// Create a new SoundDrum
					SoundDrum* soundDrum = createNewDrumForKit(kit);
					if (soundDrum) {
						kit->addDrum(soundDrum);

						// Create a new note row at the end and directly assign our drum to it
						int32_t noteRowIndex = clip->noteRows.getNumElements();
						NoteRow* noteRow = clip->noteRows.insertNoteRowAtIndex(noteRowIndex);

						if (noteRow) {
							ModelStackWithNoteRow* modelStackWithNoteRow =
							    modelStack->addNoteRow(noteRowIndex, noteRow);
							noteRow->setDrum(soundDrum, kit, modelStackWithNoteRow);

							// Scroll view to show the new drum row at the bottom
							int32_t yDisplay = noteRowIndex - clip->yScroll;
							if (yDisplay < 0 || yDisplay >= kDisplayHeight) {
								clip->yScroll = noteRowIndex - (kDisplayHeight - 1);
								if (clip->yScroll < 0) {
									clip->yScroll = 0;
								}
							}
						}

						kit->beenEdited();
						instrumentClipView.setSelectedDrum(soundDrum, true);

						// Load the sample into the new drum
						Source* source = &soundDrum->sources[0];
						MultiRange* range = source->getOrCreateFirstRange();
						if (range) {
							AudioFileHolder* holder = range->getAudioFileHolder();
							if (holder) {
								holder->setAudioFile(nullptr);
								holder->filePath.set(&filePath);
								Error loadError = holder->loadFile(false, true, true, CLUSTER_LOAD_IMMEDIATELY);

								if (loadError == Error::NONE) {
									// Set appropriate repeat mode based on sample length
									Sample* sample = static_cast<Sample*>(holder->audioFile);
									if (sample) {
										source->repeatMode = (sample->getLengthInMSec() < 2002) ? SampleRepeatMode::ONCE
										                                                        : SampleRepeatMode::CUT;
									}
									loadedToPad = true;

									// Set up sound editor context for the waveform editor
									soundEditor.currentSound = soundDrum;
									soundEditor.currentSourceIndex = 0;
									soundEditor.currentSource = source;
									soundEditor.currentSampleControls = &source->sampleControls;
									soundEditor.currentMultiRange = static_cast<MultisampleRange*>(range);
									soundEditor.navigationDepth = 0;
									soundEditor.shouldGoUpOneLevelOnBegin = false;

									// Open waveform editor at START marker
									sampleMarkerEditor.markerType = MarkerType::START;
									display->setNextTransitionDirection(1);
									bool success = openUI(&sampleMarkerEditor);
									if (success) {
										PadLEDs::skipGreyoutFade();
										PadLEDs::sendOutSidebarColoursSoon();
									}

									// Request redraw of the clip view
									uiNeedsRendering(&instrumentClipView);
								}
							}
						}
					}
				}
			}
		}

		if (!loadedToPad) {
			// Extract just the filename from the path for display
			// Path format: "SAMPLES/RETRO/RETRXXXX.WAV"
			const char* fullPath = filePath.get();
			const char* filename = fullPath;
			// Find the last '/' to get just the filename
			for (const char* p = fullPath; *p; p++) {
				if (*p == '/') {
					filename = p + 1;
				}
			}
			display->displayPopup(filename);
		}
	}
	else {
		display->displayPopup("FAIL");
	}
}
