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

#pragma once

#include "gui/ui/load/load_ui.h"
#include "hid/button.h"

class LoadSongUI final : public LoadUI {
public:
	LoadSongUI();
	ActionResult buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) override;
	ActionResult timerCallback() override;
	ActionResult verticalEncoderAction(int32_t offset, bool inCardRoutine) override;
	void graphicsRoutine() override {}
	void scrollFinished() override;
	ActionResult padAction(int32_t x, int32_t y, int32_t velocity) override;
	bool opened() override;
	void selectEncoderAction(int8_t offset) override;
	void queueLoadNextSongIfAvailable(int8_t offset);
	void performLoad();
	void displayLoopsRemainingPopup();
	bool isLoadingSong();

	bool deletedPartsOfOldSong;

	// ui
	UIType getUIType() override { return UIType::LOAD_SONG; }

protected:
	void displayText(bool blinkImmediately = false) override;
	void enterKeyPress() override;
	void folderContentsReady(int32_t entryDirection) override;
	void currentFileChanged(int32_t movementDirection) override;
	void exitAction() override;

private:
	void drawSongPreview(bool toStore = true);
	void displayArmedPopup();

	bool performingLoad;
	bool scrollingIntoSlot;
	bool qwertyCurrentlyDrawnOnscreen;
	void doQueueLoadNextSongIfAvailable(int8_t offset);
	// int32_t findNextFile(int32_t offset);
	void exitThisUI();
	void exitActionWithError();
	void performLoadFixedSM();
};
extern LoadSongUI loadSongUI;

extern char loopsRemainingText[];
