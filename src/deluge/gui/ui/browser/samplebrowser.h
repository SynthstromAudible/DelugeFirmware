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

#ifndef FILEBROWSER_H
#define FILEBROWSER_H

#include "Browser.h"

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
	bool getGreyoutRowsAndCols(uint32_t* cols, uint32_t* rows);
	bool opened();
	void focusRegained();
	int buttonAction(int x, int y, bool on, bool inCardRoutine);
	int verticalEncoderAction(int offset, bool inCardRoutine);
	int horizontalEncoderAction(int offset);
	int padAction(int x, int y, int velocity);
	bool canSeeViewUnderneath();
	int claimAudioFileForInstrument(bool makeWaveTableWorkAtAllCosts = false);
	int claimAudioFileForAudioClip();
	void scrollFinished();
	bool importFolderAsKit();
	bool importFolderAsMultisamples();
	int timerCallback();
	bool claimCurrentFile(int mayDoPitchDetection = 1, int mayDoSingleCycle = 1,
	                      int mayDoWaveTable = 1); // 0 means no. 1 means auto. 2 means yes definitely
	bool renderMainPads(uint32_t whichRows, uint8_t image[][displayWidth + sideBarWidth][3],
	                    uint8_t occupancyMask[][displayWidth + sideBarWidth], bool drawUndefinedArea = true);
	void exitAndNeverDeleteDrum();

	String lastFilePathLoaded;

protected:
	void enterKeyPress();
	void exitAction();
	int backButtonAction();
	void folderContentsReady(int entryDirection);
	void currentFileChanged(int movementDirection);

private:
	void displayCurrentFilename();
	void previewIfPossible(int movementDirection = 1);
	void audioFileIsNowSet();
	bool canImportWholeKit();
	bool loadAllSamplesInFolder(bool detectPitch, int* getNumSamples, Sample*** getSortArea,
	                            bool* getDoingSingleCycle = NULL, int* getNumCharsInPrefix = NULL);
	int getCurrentFilePath(String* path);
	void drawKeysOverWaveform();
	void autoDetectSideChainSending(SoundDrum* drum, Source* source, char const* fileName);
	void possiblySetUpBlinking();

	bool currentlyShowingSamplePreview;

	bool qwertyCurrentlyDrawnOnscreen; // This will linger as true even when qwertyVisible has been set to false
};

extern SampleBrowser sampleBrowser;

#endif // FILEBROWSER_H
