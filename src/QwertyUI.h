/*
 * Copyright © 2018-2023 Synthstrom Audible Limited
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

#ifndef QWERTYUI_H_
#define QWERTYUI_H_

#include "r_typedefs.h"
#include "UI.h"
#include "DString.h"

class QwertyUI : public UI {
public:
	QwertyUI();
	int padAction(int x, int y, int velocity);
	int horizontalEncoderAction(int offset);
	int timerCallback();
	bool renderMainPads(uint32_t whichRows, uint8_t image[][displayWidth + sideBarWidth][3] = NULL,
	                    uint8_t occupancyMask[][displayWidth + sideBarWidth] = NULL, bool drawUndefinedArea = true) {
		return true;
	}

	static bool predictionInterrupted;
	static String enteredText;

protected:
	bool opened();
	virtual bool predictExtendedText() { return true; } // Returns whether we're allowed that new character.
	void drawKeys();
	virtual void processBackspace(); // May be called in card routine
	virtual void enterKeyPress() = 0;

	// This may be called in card routine so long as either !currentFileExists (which is always the case in a processBackspace()),
	// or we are not LoadSongUI
#if HAVE_OLED
	char const* title;
	virtual void displayText(bool blinkImmediately = false) = 0;
	void drawTextForOLEDEditing(int textStartX, int xPixelMax, int yPixel, int maxChars,
	                            uint8_t image[][OLED_MAIN_WIDTH_PIXELS]);
#else
	virtual void displayText(bool blinkImmediately = false);
#endif
	static int16_t enteredTextEditPos;
	static int scrollPosHorizontal;

private:
};

#endif /* QWERTYUI_H_ */
