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

#include "gui/context_menu/delete_file.h"
#include "definitions_cxx.hpp"
#include "gui/context_menu/save_song_or_instrument.h"
#include "gui/l10n/l10n.h"
#include "gui/ui/browser/browser.h"
#include "gui/ui/ui.h"
#include "hid/display/display.h"
#include "io/file.hpp"
#include "storage/byte_channel.h"

namespace deluge::gui::context_menu {

DeleteFile deleteFile{};

char const* DeleteFile::getTitle() {
	using enum l10n::String;
	if (getUIUpOneLevel() == &context_menu::saveSongOrInstrument) {
		return l10n::get(STRING_FOR_ARE_YOU_SURE_QMARK);
	}
	return l10n::get(STRING_FOR_DELETE_QMARK);
}

std::span<char const*> DeleteFile::getOptions() {
	using enum l10n::String;

	static char const* options[] = {l10n::get(STRING_FOR_OK)};
	return {options, 1};
}

Browser* DeleteFile::browserUnderneath() {
	UI* ui = getUIUpOneLevel();
	if (ui == &context_menu::saveSongOrInstrument) {
		ui = getUIUpOneLevel(2);
	}
	return static_cast<Browser*>(ui);
}

bool DeleteFile::acceptCurrentOption() {
	namespace bytes = deluge::storage::bytes;

	Browser* browser = browserUnderneath();
	if (!browser->getCurrentFileItem()->maybeExistsOnCard) {
		// Nothing on the card to remove.
		closeMenus();
		return true;
	}
	pendingPath_ = browser->getCurrentFilePath();

	if (bytes::usingByteChannel()) {
		if (bytes::submitUnlink(pendingPath_.c_str(), &DeleteFile::unlinkDone, this)) {
			pending_ = true;
			return true;
		}
		// The path does not fit the channel. After boot, off the owner, there is no legal place to remove it inline.
		applyOutcome(Outcome::Failed);
		return true;
	}
	// On the storage owner, or before it has started: blocking on the card is legal here.
	if (!bytes::runInlineWhereLegal(&DeleteFile::unlinkInline, this)) {
		applyOutcome(Outcome::Failed);
	}
	return true;
}

void DeleteFile::unlinkInline(void* ctx) {
	auto* self = static_cast<DeleteFile*>(ctx);
	deluge::storage::bytes::noteIoTier(deluge::storage::bytes::IoTier::Browser);
	auto removed = deluge::io::unlink(self->pendingPath_);
	if (removed.has_value()) {
		self->applyOutcome(Outcome::Removed);
	}
	else if (removed.error() == deluge::io::Status::NOT_FOUND) {
		self->applyOutcome(Outcome::Missing);
	}
	else {
		self->applyOutcome(Outcome::Failed);
	}
}

void DeleteFile::unlinkDone(void* ctx, deluge::storage::bytes::SettingsOutcome outcome) {
	auto* self = static_cast<DeleteFile*>(ctx);
	pending_ = false;
	// Only this menu's own answer finishes it. Whatever else took it off the top of the stack took the popup and the
	// list with it; the card outcome stands.
	if (getCurrentUI() != self) {
		return;
	}
	switch (outcome) {
	case deluge::storage::bytes::SettingsOutcome::Written:
		self->applyOutcome(Outcome::Removed);
		return;
	case deluge::storage::bytes::SettingsOutcome::Absent:
		self->applyOutcome(Outcome::Missing);
		return;
	default:
		// `WriteFailed`: the card refused it, or the channel stalled.
		self->applyOutcome(Outcome::Failed);
		return;
	}
}

void DeleteFile::applyOutcome(Outcome outcome) {
	using enum l10n::String;

	Browser* browser = browserUnderneath();
	FileItem* item = browser->getCurrentFileItem();
	// The list changes only while the browser still has this delete's file selected.
	const bool stillSelected = item != nullptr && browser->getCurrentFilePath() == pendingPath_;

	switch (outcome) {
	case Outcome::Removed:
		++removed_;
		display->displayPopup(l10n::get(STRING_FOR_FILE_DELETED));
		if (stillSelected) {
			browser->currentFileDeleted();
		}
		break;
	case Outcome::Missing:
		// The file was not on the card in the first place.
		++missing_;
		if (stillSelected) {
			if (item->instrumentAlreadyInSong) {
				display->displayPopup(l10n::get(STRING_FOR_ERROR_PRESET_IN_USE));
			}
			else if (item->instrument) {
				// It has an instrument, it's not on the card, and it's not in use: remove it from the list.
				display->displayPopup(l10n::get(STRING_FOR_FILE_DELETED));
				browser->currentFileDeleted();
			}
		}
		break;
	case Outcome::Failed:
		++failed_;
		display->displayPopup(l10n::get(STRING_FOR_ERROR_DELETING_FILE));
		break;
	}
	closeMenus();
}

void DeleteFile::closeMenus() {
	close();
	if (getCurrentUI() == &context_menu::saveSongOrInstrument) {
		context_menu::saveSongOrInstrument.close();
	}
}

ActionResult DeleteFile::buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) {
	if (pending_) {
		// Ignored but NOT dealt with, as a save screen mid-save does: the global handlers behind this still run, none
		// of them can close this menu, and swallowing them would lose a SHIFT release.
		return ActionResult::NOT_DEALT_WITH;
	}
	return ContextMenuForSaving::buttonAction(b, on, inCardRoutine);
}

ActionResult DeleteFile::padAction(int32_t x, int32_t y, int32_t velocity) {
	if (pending_) {
		return ActionResult::DEALT_WITH;
	}
	return ContextMenuForSaving::padAction(x, y, velocity);
}

ActionResult DeleteFile::verticalEncoderAction(int32_t offset, bool inCardRoutine) {
	if (pending_) {
		return ActionResult::DEALT_WITH;
	}
	return ContextMenuForSaving::verticalEncoderAction(offset, inCardRoutine);
}

ActionResult DeleteFile::horizontalEncoderAction(int32_t offset) {
	if (pending_) {
		return ActionResult::DEALT_WITH;
	}
	return ContextMenuForSaving::horizontalEncoderAction(offset);
}

void DeleteFile::selectEncoderAction(int8_t offset) {
	if (pending_) {
		return;
	}
	ContextMenuForSaving::selectEncoderAction(offset);
}

ActionResult DeleteFile::exitUI() {
	// The long-press BACK timer reaches the current UI through here, not through buttonAction().
	if (pending_) {
		return ActionResult::DEALT_WITH;
	}
	return ContextMenuForSaving::exitUI();
}
} // namespace deluge::gui::context_menu
