/*
 * Copyright © 2026 Sean Ditny
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
#include "extern.h"
#include "gui/menu_item/menu_item.h"
#include "gui/menu_item/note/clipboard_icons.h"
#include "gui/views/note_clipboard.h"
#include "hid/display/oled.h"

namespace deluge::gui::menu_item::note {
class Clipboard final : public MenuItem {
public:
	using Operation = ClipboardOperation;
	Clipboard(l10n::String name, Operation operation) : MenuItem(name), operation_(operation) {}
	MenuItem* selectButtonPress() override {
		if (sdRoutineLock) {
			return NO_NAVIGATION;
		}
		switch (operation_) {
		case Operation::CUT:
			noteClipboard.copySelection(true);
			break;
		case Operation::COPY:
			noteClipboard.copySelection(false);
			break;
		case Operation::PASTE_FIRMLY:
			noteClipboard.paste(true);
			break;
		case Operation::PASTE_GENTLY:
			noteClipboard.paste(false);
			break;
		case Operation::DELETE:
			noteClipboard.deleteSelection();
			break;
		}
		return NO_NAVIGATION;
	}
	MenuPermission checkPermissionToBeginSession(ModControllableAudio* modControllable, int32_t whichThing,
	                                             MultiRange** currentRange) override {
		if (operation_ == Operation::PASTE_FIRMLY || operation_ == Operation::PASTE_GENTLY) {
			noteClipboard.setPasteMode(operation_ == Operation::PASTE_FIRMLY);
		}
		return MenuItem::checkPermissionToBeginSession(modControllable, whichThing, currentRange);
	}
	void getColumnLabel(StringBuf& label) override {
		if (operation_ == Operation::PASTE_FIRMLY || operation_ == Operation::PASTE_GENTLY) {
			label.append(l10n::get(operation_ == Operation::PASTE_FIRMLY ? l10n::String::STRING_FOR_FIRMLY
			                                                             : l10n::String::STRING_FOR_GENTLY));
		}
		else {
			MenuItem::getColumnLabel(label);
		}
	}
	bool shouldEnterSubmenu() override { return false; }
	bool showNotification() const override { return false; }
	void renderInHorizontalMenu(const SlotPosition& slot) override {
		bool noteVisible = (operation_ == Operation::PASTE_FIRMLY || operation_ == Operation::PASTE_GENTLY)
		                   && noteClipboard.clipboardNoteVisible();
		drawClipboardIcon(hid::display::OLED::main, slot.start_x + (slot.width - 18) / 2, slot.start_y - 1, operation_,
		                  noteVisible ? noteClipboard.iconNoteCount() : 0);
	}

private:
	Operation operation_;
};
} // namespace deluge::gui::menu_item::note
