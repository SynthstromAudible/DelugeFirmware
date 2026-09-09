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

#include "gui/ui/save/save_ui.h"
#include "definitions_cxx.hpp"
#include "gui/context_menu/overwrite_file.h"
#include "gui/context_menu/save_song_or_instrument.h"
#include "gui/ui/ui.h"
#include "hid/buttons.h"
#include "hid/display/display.h"
#include "hid/led/indicator_leds.h"
#include "hid/led/pad_leds.h"
#include "libdeluge/file_bytes.h"
#include "libdeluge/file_write.h"
#include "storage/file_item.h"
#include "storage/owner.h"
#include "storage/storage_manager.h"
#include <optional>
#include <span>

using namespace deluge;

bool SaveUI::currentFolderIsEmpty;

SaveUI::SaveUI() {
	mayDefaultToBrandNewNameOnEntry = true;
}

bool SaveUI::opened() {
	Error error = beginSlotSession(true, true);
	if (error != Error::NONE) {
		display->displayError(error);
		return false;
	}

	PadLEDs::clearSideBar();
	return true;
}

void SaveUI::focusRegained() {
	indicator_leds::blinkLed(IndicatorLED::SAVE);
	return SlotBrowser::focusRegained();
}

/*
// TODO: in the future, there may be a case to be made for moving this to LoadOrSaveUI.
// Check the very similar variations in LoadSongUI and LoadInstrumentPresetUI
void SaveUI::displayText(bool blinkImmediately) {

    if (enteredText.isEmpty() && !currentFolderIsEmpty) {
        display->setTextAsSlot(currentSlot, currentSubSlot, currentFileExists, true, numberEditPos);
        indicator_leds::ledBlinkTimeout(0, true, !blinkImmediately);
    }

    else {
        QwertyUI::displayText(blinkImmediately);
    }
}
*/

void SaveUI::enterKeyPress() {

	FileItem* currentFileItem = getCurrentFileItem();

	// If it's a directory...
	if (currentFileItem && currentFileItem->isFolder) {
		// goIntoFolder() dispatches onto the storage owner; failure is handled by the base
		// Browser::onListingFailed() (displayError + close()) once the listing completes.
		goIntoFolder(currentFileItem->filename.c_str());
	}

	else if (enteredText.empty()) {} // Previously had &&currentFolderIsEmpty ... why?

	else {
		// Reads and writes the card, so it runs on the storage owner. Nothing followed this call:
		// performSave() closes the UI itself on success and displays its own error otherwise, so
		// runSaveCompletion()'s EnterKey arm has no tail to run.
		(void)dispatchSave({
		    .consumer = deluge::storage::SaveConsumer::EnterKey,
		    .mayOverwrite = false,
		    .collectingSamples = collectingSamplesForDispatch(),
		    .filePath = getCurrentFilePath(),
		    .enteredText = enteredText,
		    .dirPath = currentDir,
		});
	}
}

ActionResult SaveUI::buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) {
	using namespace deluge::hid::button;

	FileItem* currentFileItem = getCurrentFileItem();

	// Save button
	if (b == SAVE && !Buttons::isShiftButtonPressed()) {
		return mainButtonAction(on);
	}

	// Pressing on select encoder button will go through here
	else {
		return SlotBrowser::buttonAction(b, on, inCardRoutine);
	}

	return ActionResult::DEALT_WITH;
}

ActionResult SaveUI::timerCallback() {
	if (currentUIMode == UI_MODE_HOLDING_BUTTON_POTENTIAL_LONG_PRESS) {

		bool available = gui::context_menu::saveSongOrInstrument.setupAndCheckAvailability();

		if (available) {
			currentUIMode = UI_MODE_NONE;
			openUI(&gui::context_menu::saveSongOrInstrument);
		}
		else {
			exitUIMode(UI_MODE_HOLDING_BUTTON_POTENTIAL_LONG_PRESS);
		}

		return ActionResult::DEALT_WITH;
	}
	else {
		return SlotBrowser::timerCallback();
	}
}

void SaveUI::noteSaveTier() {
	// Boot is the one legal exception: it writes before any interaction tier exists. Owner::started()
	// is the same monotonic latch the TIR1-TIR4 reader checks use.
	if (!deluge::storage::Owner::on_owner() && deluge::storage::Owner::started()) {
		// Counted as well as frozen: the count is what let the gate report "3 saves on the
		// interaction tier" BEFORE the conversion, which is the evidence that the conversion did
		// something. The freeze is what keeps it from coming back.
		interactionTierSaves_++;
		if (ALPHA_OR_BETA_VERSION) {
			FREEZE_WITH_ERROR("TIR5");
		}
	}
}

bool SaveUI::dispatchSave(const deluge::storage::SaveRequest& request) {
	// Single-flight by REFUSAL, not by superseding (see dispatchSave()'s doc). The caller treats
	// false exactly as it treats a full owner queue: nothing was written, and nothing was promised.
	if (saveInFlight_) {
		return false;
	}
	saveInFlight_ = true;
	inFlightRequest_ = request;

	// A document save builds itself in memory first and then stores it in one operation, so it does
	// not go to the owner to begin with. Everything else -- the song save, whose save copies samples
	// around the card -- keeps the path it has always had.
	if (savesStagedDocument()) {
		phaseOwner_ = this;
		savePhase_ = SavePhase::SERIALIZE;
		writeIssued_ = false;
		stagedSucceeded_ = false;
		stallTicks_ = 0;
		// Attempt it once here rather than waiting for the next saveRoutine() pass, for the reason
		// beginLoad() does: work dispatched during boot must not sit waiting for a repeating task
		// that is not running yet.
		saveRoutine();
		return true;
	}

	if (!deluge::storage::Owner::run_or_inline(&SaveUI::runSaveOp, this)) {
		saveInFlight_ = false; // the op never ran; release, or every later save is refused forever
		return false;
	}
	return true;
}

/// The staged document is built in `smSerializer`, the SHARED serialiser, and not in a local one.
///
/// Not a preference. `SoundDrum::writeToFileAsInstrument` and `Instrument::writeToFile` take no
/// serialiser at all -- they reach `GetSerializer()`, which hard-codes that global -- so a local
/// writer would receive the prolog and nothing else. Borrowing the global is what makes those two
/// serialisable without a refactor through the model layer.
///
/// The borrow is why every exit calls releaseStagedWriter(): left in memory mode, the shared
/// serialiser silently stops reaching the card for everyone after this save.
namespace {
/// How many consecutive ticks one staged save tolerates without progress. A bound, not a timeout: an
/// unbounded retry is not patience, it is a wedge that presents as the device having hung.
constexpr int32_t kMaxSaveStallTicks = 600;
} // namespace

void SaveUI::saveRoutine() {
	if (phaseOwner_ == nullptr) {
		return;
	}
	switch (savePhase_) {
	case SavePhase::NONE:
		return;
	case SavePhase::SERIALIZE:
		phaseOwner_->runSerializePhase();
		return;
	case SavePhase::WRITE:
		phaseOwner_->runWritePhase();
		return;
	case SavePhase::FINISH:
		phaseOwner_->runFinishPhase();
		return;
	}
}

bool SaveUI::deferStagedSave() {
	if (++stallTicks_ > kMaxSaveStallTicks) {
		return false;
	}
	return true;
}

void SaveUI::runSerializePhase() {
	// No card contact at all. This is the half of a save that never needed the owner.
	smSerializer.reset();
	smSerializer.setMemoryBased();
	// The prolog, which createXMLFile() used to write into the writer as it opened the file. Building
	// the document without it would save every preset headerless.
	smSerializer.write("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
	if (!serializeForSave(smSerializer)) {
		endStagedSave(false);
		return;
	}
	if (smSerializer.bytesWritten() <= 0) {
		// An empty document is a failed save, not a small one.
		endStagedSave(false);
		return;
	}
	savePhase_ = SavePhase::WRITE;
	stallTicks_ = 0;
}

void SaveUI::releaseStagedWriter() {
	// Hand the shared serialiser back. Anything after this save that writes a file through it -- the
	// song save above all -- needs it reaching the card again.
	smSerializer.clearMemoryBased();
	smSerializer.reset();
}

void SaveUI::runWritePhase() {
	const auto* bytes = reinterpret_cast<const uint8_t*>(smSerializer.getBufferPtr());
	const auto len = static_cast<uint32_t>(smSerializer.bytesWritten());
	const char* path = inFlightRequest_.filePath.c_str();
	const bool mayOverwrite = inFlightRequest_.mayOverwrite;

	const bool onOwner = deluge::storage::Owner::on_owner() || !deluge::storage::Owner::started();
	// BOTH channels: the write goes through one and this arm's siblings size themselves with the
	// other, and an arm that needs two and is given one defers forever.
	const bool mayUseChannel = !onOwner && deluge_file_write_async_active() && deluge_file_bytes_async_active();

	if (!onOwner && !mayUseChannel) {
		// A build without the channel. Writing from here would block the very tier the card needs
		// running to answer, so hand the whole write to the owner, which may block legally.
		if (!deluge::storage::Owner::run(&SaveUI::runSaveOp, this)) {
			if (!deferStagedSave()) {
				endStagedSave(false);
			}
			return;
		}
		// runSaveOp ran performSave(), which owns the whole legacy path including its completion.
		savePhase_ = SavePhase::NONE;
		phaseOwner_ = nullptr;
		releaseStagedWriter();
		return;
	}

	if (onOwner) {
		// Blocking is legal here, and the driver could not run anyway: a request issued from a
		// context that will not yield can never be answered.
		auto created = StorageManager::createFile(path, mayOverwrite);
		if (!created) {
			handleWriteOutcome(created.error() == Error::FILE_ALREADY_EXISTS, false);
			return;
		}
		auto written = created->write(std::span<const std::byte>(reinterpret_cast<const std::byte*>(bytes), len));
		const bool ok = written.has_value() && *written == len;
		(void)created->close();
		handleWriteOutcome(false, ok);
		return;
	}

	if (!writeIssued_) {
		if (!deluge_file_write_request_write_file(path, bytes, len, mayOverwrite)) {
			// Refused, not failed: nothing was queued, so ask again next tick.
			if (!deferStagedSave()) {
				endStagedSave(false);
			}
			return;
		}
		writeIssued_ = true;
		stallTicks_ = 0;
		return;
	}

	uint32_t writtenLen = 0;
	uint8_t exists = 0;
	const uint8_t collected = deluge_file_write_collect_write_file(path, &writtenLen, &exists);
	if (collected == DELUGE_FILE_WRITE_PENDING) {
		if (!deferStagedSave()) {
			endStagedSave(false);
		}
		return;
	}
	if (collected == DELUGE_FILE_WRITE_UNKNOWN) {
		// Never keyed, so nothing is coming. Re-issue rather than wait forever.
		writeIssued_ = false;
		if (!deferStagedSave()) {
			endStagedSave(false);
		}
		return;
	}
	writeIssued_ = false;
	if (collected == DELUGE_FILE_WRITE_FAILED) {
		handleWriteOutcome(exists == 1, false);
		return;
	}
	++writeChannelServedOps_;
	handleWriteOutcome(false, writtenLen == len);
}

void SaveUI::handleWriteOutcome(bool alreadyExists, bool ok) {
	if (alreadyExists) {
		// The name is taken and the user did not ask to replace it. Ask them, exactly as the
		// owner-side path did -- and report the save as "dealt with", which is what a prompt means.
		deluge::gui::context_menu::overwriteFile.currentSaveUI = this;
		if (!deluge::gui::context_menu::overwriteFile.setupAndCheckAvailability()) {
			endStagedSave(false);
			return;
		}
		display->removeWorkingAnimation();
		openUI(&deluge::gui::context_menu::overwriteFile);
		endStagedSave(true);
		return;
	}
	if (!ok) {
		display->displayError(Error::WRITE_FAIL);
		endStagedSave(false);
		return;
	}
	endStagedSave(true);
}

void SaveUI::runFinishPhase() {
	const deluge::storage::SaveRequest request = inFlightRequest_;
	const bool succeeded = stagedSucceeded_;
	SaveUI* const ui = phaseOwner_;

	savePhase_ = SavePhase::NONE;
	phaseOwner_ = nullptr;
	// Released BEFORE the completion, exactly as runSaveOp() releases it: a completion may
	// legitimately start another save (confirming the overwrite prompt is one), and it must not be
	// refused by this save's own guard.
	saveInFlight_ = false;

	ui->runSaveCompletion(request, succeeded);
}

void SaveUI::endStagedSave(bool succeeded) {
	// The shared serialiser is handed back here and not one tick later: every path out of a save
	// passes through this function, and leaving it borrowed breaks the next save that wants a file.
	releaseStagedWriter();
	writeIssued_ = false;
	stallTicks_ = 0;
	stagedSucceeded_ = succeeded;
	savePhase_ = SavePhase::FINISH;
	runFinishPhase();
}

void SaveUI::runSaveOp(void* self) {
	auto* ui = static_cast<SaveUI*>(self);
	// By value: the completion still needs the request after the guard below is released, and a
	// later dispatch may overwrite the slot a reference would point into.
	const deluge::storage::SaveRequest request = inFlightRequest_;

	const bool succeeded = ui->performSave(request.mayOverwrite);

	// Release BEFORE the completion: a completion may legitimately start another save (confirming
	// the overwrite prompt is one), and it must not be refused by this op's own guard.
	saveInFlight_ = false;

	ui->runSaveCompletion(request, succeeded);
}

// Runs the follow-up for whichever caller dispatched this save. Every arm runs ON THE OWNER, which
// matches the established pattern: runScrollLoadOp() calls displayError() and runCommitOp() calls
// close(), both on the owner.
void SaveUI::runSaveCompletion(const deluge::storage::SaveRequest& request, bool succeeded) {
	using deluge::storage::SaveConsumer;
	switch (request.consumer) {
	case SaveConsumer::EnterKey:
		// Nothing followed the call at enterKeyPress(): performSave() closed the UI on success and
		// displayed its own error otherwise.
		break;
	case SaveConsumer::OverwriteAccept:
	case SaveConsumer::OverwritePad:
		// The synchronous path returned false on failure, which made context_menu.cpp's SELECT_ENC
		// handler take `goto getOut` -- that label is just close(). The bool now means "accepted"
		// and cannot carry the outcome, so the dismissal happens here instead.
		if (!succeeded) {
			deluge::gui::context_menu::overwriteFile.close();
		}
		break;
	case SaveConsumer::SaveSongOrInstrumentAccept:
		if (!succeeded) {
			deluge::gui::context_menu::saveSongOrInstrument.close();
		}
		break;
	}
}
