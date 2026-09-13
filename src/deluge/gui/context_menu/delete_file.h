/*
 * Copyright © 2019-2023 Synthstrom Audible Limited
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

#include "gui/context_menu/context_menu.h"
#include "storage/byte_channel.h"

#include <cstdint>
#include <string>

class Browser;

namespace deluge::gui::context_menu {
/// @brief The browser's "Delete?" menu: removes the selected file from the card.
///
/// After boot, off the storage owner, the removal goes through the byte channel, and the menu stays open, refusing
/// input, until the channel answers. Where blocking on the card is legal it removes the file inline. Both arms apply
/// their outcome -- removed, already missing, or failed -- through one function.
class DeleteFile final : public ContextMenuForSaving {
public:
	DeleteFile() = default;

	std::span<char const*> getOptions() override;
	bool acceptCurrentOption() override;

	char const* getTitle() override;

	/// @name Input while a delete is running
	/// Each of these could take the menu away before the channel answers. They are refused while a delete is pending,
	/// as a save screen refuses input mid-save.
	/// @{
	ActionResult buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) override;
	ActionResult padAction(int32_t x, int32_t y, int32_t velocity) override;
	ActionResult verticalEncoderAction(int32_t offset, bool inCardRoutine) override;
	ActionResult horizontalEncoderAction(int32_t offset) override;
	void selectEncoderAction(int8_t offset) override;
	ActionResult exitUI() override;
	/// @}

	/// @name How deletes ended
	/// Counted when an outcome is applied, by either arm. Read by the path-operations gate.
	/// @{
	/// @brief Deletes that removed the file.
	[[nodiscard]] static int32_t deletesRemoved() { return removed_; }
	/// @brief Deletes that found the file already gone.
	[[nodiscard]] static int32_t deletesMissing() { return missing_; }
	/// @brief Deletes the card refused, or the byte channel gave up on.
	[[nodiscard]] static int32_t deletesFailed() { return failed_; }
	/// @brief Whether a delete is with the byte channel and has not yet been answered.
	[[nodiscard]] static bool deletePending() { return pending_; }
	/// @}

private:
	/// @brief What the card did with a delete.
	enum class Outcome : uint8_t { Removed, Missing, Failed };

	/// @brief The browser this menu was opened over, looking past the save menu when that sits between them.
	Browser* browserUnderneath();
	/// @brief Show the delete's popup, change the list, and close the menu, as a delete always has.
	/// @param outcome What the card did. The list changes only while the browser still has @ref pendingPath_ selected.
	void applyOutcome(Outcome outcome);
	/// @brief Close this menu, and the save menu when that is what it was opened from.
	void closeMenus();
	/// @brief The removal where blocking on the card is legal, for `bytes::runInlineWhereLegal`.
	/// @param ctx This menu.
	static void unlinkInline(void* ctx);
	/// @brief `bytes::DoneFn` for the channel's removal.
	/// @param ctx     This menu.
	/// @param outcome `Written`, `Absent`, or `WriteFailed` -- the card refused it, or the channel stalled.
	static void unlinkDone(void* ctx, deluge::storage::bytes::SettingsOutcome outcome);

	/// The file the delete in progress, or the last one, was aimed at.
	std::string pendingPath_;

	static inline int32_t removed_ = 0;
	static inline int32_t missing_ = 0;
	static inline int32_t failed_ = 0;
	static inline bool pending_ = false;
};

extern DeleteFile deleteFile;
} // namespace deluge::gui::context_menu
