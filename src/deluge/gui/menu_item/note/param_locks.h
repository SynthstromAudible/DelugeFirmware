/*
 * Copyright © 2026 Synthstrom Audible Limited
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

#include "gui/menu_item/integer.h"
#include "gui/menu_item/menu_item.h"
#include "model/note/note_locks.h"
#include "util/containers.h"

namespace deluge::gui::menu_item::note {

/// Note editor > Param locks > (a lock): the lock's value, in the units the param's own menu uses. Edits are kept
/// here and written to the targeted notes once: when you leave the item, or before the note selection changes. A
/// pending edit is never replaced by a re-read (the automation display re-reads the open item constantly while
/// playing). SHIFT + SAVE deletes the lock.
class ParamLockValue final : public Integer {
public:
	using Integer::Integer;
	void setParam(int32_t param) { param_ = param; }
	[[nodiscard]] int32_t getMinValue() const override { return -64; }
	[[nodiscard]] int32_t getMaxValue() const override { return 64; }
	[[nodiscard]] std::string_view getTitle() const override;
	void readCurrentValue() override;
	void readValueAgain() override;
	void selectEncoderAction(int32_t offset) override;
	void endSession() override;
	/// Writes a pending edit to the notes (once). Safe to call more than once.
	void commit();
	void deletePress() override;

protected:
	int32_t getDisplayValue() override;
	void writeCurrentValue() override { dirty_ = true; }
	void drawValue() override;

private:
	int32_t param_{0};
	bool dirty_{false};
};

/// Note editor > Param locks: the targeted (left-most) note's locks, as "name value". SELECT on a lock edits it,
/// SHIFT + SAVE deletes it, and the last entry clears them all.
class ParamLocks final : public MenuItem {
public:
	using MenuItem::MenuItem;
	void beginSession(MenuItem* navigatedBackwardFrom = nullptr) override;
	void readValueAgain() override;
	void selectEncoderAction(int32_t offset) override;
	MenuItem* selectButtonPress() override;
	bool shouldEnterSubmenu() override;
	bool isSubmenu() override { return true; }
	void getColumnLabel(StringBuf& label) override {
		label.append(l10n::get(l10n::String::STRING_FOR_NOTE_EDITOR_PARAM_LOCKS_SHORT));
	}
	void renderInHorizontalMenu(const SlotPosition& slot) override;
	void drawPixelsForOled() override;
	void deletePress() override;

private:
	void readLocks();
	void drawValue();
	[[nodiscard]] int32_t numEntries() const { return locks_.size() + 1; } // + "Clear all" / "None"

	deluge::note_locks::LockSet locks_;
	deluge::vector<std::string_view> options_;
	int32_t currentValue_{0};
	int32_t scrollPos_{0};
};

} // namespace deluge::gui::menu_item::note
