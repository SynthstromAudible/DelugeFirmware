/*
 * Copyright © 2015-2023 Synthstrom Audible Limited
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

#pragma once

#include "definitions_cxx.hpp"
#include "gui/ui/browser/browser.h"
#include "hid/button.h"

extern "C" {

#include "fatfs/ff.h"

FRESULT f_readdir_get_filepointer(DIR* dp,      /* Pointer to the open directory object */
                                  FILINFO* fno, /* Pointer to file information to return */
                                  FilePointer* filePointer);
}

class SoundDrum;
class NumericLayerScrollingText;
class Source;
class Sample;

class SampleBrowser final : public Browser {
public:
	SampleBrowser();
	bool getGreyoutColsAndRows(uint32_t* cols, uint32_t* rows);
	bool opened();
	void focusRegained();
	ActionResult buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine);
	ActionResult verticalEncoderAction(int32_t offset, bool inCardRoutine);
	ActionResult horizontalEncoderAction(int32_t offset);
	ActionResult padAction(int32_t x, int32_t y, int32_t velocity);
	bool canSeeViewUnderneath();
	Error claimAudioFileForInstrument(bool makeWaveTableWorkAtAllCosts = false);
	Error claimAudioFileForAudioClip();
	void scrollFinished();
	bool importFolderAsKit();
	bool importFolderAsMultisamples();
	ActionResult timerCallback();
	bool claimCurrentFile(int32_t mayDoPitchDetection = 1, int32_t mayDoSingleCycle = 1, int32_t mayDoWaveTable = 1,
	                      bool loadWithoutExiting = false); // 0 means no. 1 means auto. 2 means yes definitely
	bool renderMainPads(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
	                    uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], bool drawUndefinedArea = true);
	void exitAndNeverDeleteDrum();

	const char* getName() { return "sample_browser"; }
	String lastFilePathLoaded;

protected:
	void enterKeyPress();
	void exitAction();
	ActionResult backButtonAction();
	void folderContentsReady(int32_t entryDirection);
	void currentFileChanged(int32_t movementDirection);

private:
	void displayCurrentFilename();
	void previewIfPossible(int32_t movementDirection = 1);
	void audioFileIsNowSet();
	bool canImportWholeKit();
	bool loadAllSamplesInFolder(bool detectPitch, int32_t* getNumSamples, Sample*** getSortArea,
	                            bool* getDoingSingleCycle = NULL, int32_t* getNumCharsInPrefix = NULL);
	Error getCurrentFilePath(String* path);
	void drawKeysOverWaveform();
	void autoDetectSideChainSending(SoundDrum* drum, Source* source, char const* fileName);
	void possiblySetUpBlinking();

	bool autoLoadEnabled;

	bool currentlyShowingSamplePreview;

	bool qwertyCurrentlyDrawnOnscreen; // This will linger as true even when qwertyVisible has been set to false
};

extern SampleBrowser sampleBrowser;
