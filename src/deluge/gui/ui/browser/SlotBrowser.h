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

#ifndef SLOTBROWSER_H_
#define SLOTBROWSER_H_

#include "Browser.h"

class Instrument;

class SlotBrowser : public Browser {
public:
	SlotBrowser();

#if !HAVE_OLED
	void focusRegained();
	int horizontalEncoderAction(int offset);
#endif
	int getCurrentFilePath(String* path);

protected:
	int beginSlotSession(bool shouldDrawKeys = true, bool allowIfNoFolder = false);
	void processBackspace();
	//bool predictExtendedText();
	virtual void predictExtendedTextFromMemory() {
	}
	void convertToPrefixFormatIfPossible();
	void enterKeyPress();
	int getCurrentFilenameWithoutExtension(String* filename);

	static bool currentFileHasSuffixFormatNameImplied;

	Instrument*
	    currentInstrument; // Although this is only needed by the child class LoadInstrumentPresetUI, we cut a corner by including it here so our functions
	                       // can set it to NULL, which is needed.
	                       // This is the Instrument we're currently scrolled onto. Might not be actually loaded (yet)?
	                       // We do need this, separate from the current FileItem, because if user moves onto a folder,
	                       // the currentInstrument needs to remain the same.
};

#endif /* SLOTBROWSER_H_ */
