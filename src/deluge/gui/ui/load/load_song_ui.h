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

#ifndef LOADSONGUI_H
#define LOADSONGUI_H

#include "gui/ui/load/load_ui.h"

class LoadSongUI final : public LoadUI {
public:
	LoadSongUI();
	int buttonAction(int x, int y, bool on, bool inCardRoutine);
	int timerCallback();
	int verticalEncoderAction(int offset, bool inCardRoutine);
	void graphicsRoutine() {}
	void scrollFinished();
	int padAction(int x, int y, int velocity);
	bool opened();
	void selectEncoderAction(int8_t offset);
	void performLoad();
	void displayLoopsRemainingPopup();

	bool deletedPartsOfOldSong;

protected:
	void displayText(bool blinkImmediately = false);
	void enterKeyPress();
	void folderContentsReady(int entryDirection);
	void currentFileChanged(int movementDirection);
	void exitAction();

private:
	void drawSongPreview(bool toStore = true);
#if HAVE_OLED
	void displayArmedPopup();
#endif

	uint8_t squaresScrolled;
	int8_t scrollDirection;
	bool scrollingToNothing;
	bool scrollingIntoSlot;
	//int findNextFile(int offset);
	void exitThisUI();
	void exitActionWithError();
};
extern LoadSongUI loadSongUI;

#endif // LOADSONGUI_H
