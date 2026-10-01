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
#include "gui/ui/save/save_song_ui.h"
#include "gui/ui/ui.h"
#include "hid/buttons.h"
#include "hid/display/display.h"
#include "hid/led/indicator_leds.h"
#include "hid/led/pad_leds.h"
#include "libdeluge/file_bytes.h"
#include "storage/file_item.h"
#include "storage/storage_manager.h"
#include "storage/temp_swap.h"
#include "util/boot.h"
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
		// goIntoFolder() begins a listing that graphicsRoutine() runs over later ticks; failure is handled by
		// the base Browser::onListingFailed() (displayError + close()) once the listing completes.
		goIntoFolder(currentFileItem->filename.c_str());
	}

	else if (enteredText.empty()) {} // Previously had &&currentFolderIsEmpty ... why?

	else {
		// Nothing followed this call: the save closes the UI itself on success and displays its own error otherwise, so
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

bool SaveUI::anySaveInFlight() {
	return saveInFlight_ || saveSongUI.collectInFlight() || saveSongUI.docInFlight();
}

ActionResult SaveUI::buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) {
	if (anySaveInFlight()) {
		// The save screen ignores the press, but it is NOT dealt with: the global handlers behind this still
		// run. None of them can close this screen, and swallowing them would lose a SHIFT release -- leaving
		// shift held after the save -- and take PLAY and RECORD away for as long as a save runs.
		return ActionResult::NOT_DEALT_WITH;
	}
	return saveScreenButtonAction(b, on, inCardRoutine);
}

ActionResult SaveUI::saveScreenButtonAction(deluge::hid::Button b, bool on, bool inCardRoutine) {
	using namespace deluge::hid::button;

	// Save button
	if (b == SAVE && !Buttons::isShiftButtonPressed()) {
		return mainButtonAction(on);
	}

	// Pressing on select encoder button will go through here
	return SlotBrowser::buttonAction(b, on, inCardRoutine);
}

ActionResult SaveUI::exitUI() {
	// The long-press BACK timer reaches the screen through here, not through buttonAction().
	if (anySaveInFlight()) {
		return ActionResult::DEALT_WITH;
	}
	return SlotBrowser::exitUI();
}

ActionResult SaveUI::padAction(int32_t x, int32_t y, int32_t velocity) {
	if (anySaveInFlight()) {
		return ActionResult::DEALT_WITH;
	}
	return SlotBrowser::padAction(x, y, velocity);
}

ActionResult SaveUI::verticalEncoderAction(int32_t offset, bool inCardRoutine) {
	if (anySaveInFlight()) {
		return ActionResult::DEALT_WITH;
	}
	return SlotBrowser::verticalEncoderAction(offset, inCardRoutine);
}

ActionResult SaveUI::horizontalEncoderAction(int32_t offset) {
	if (anySaveInFlight()) {
		return ActionResult::DEALT_WITH;
	}
	return SlotBrowser::horizontalEncoderAction(offset);
}

void SaveUI::selectEncoderAction(int8_t offset) {
	if (anySaveInFlight()) {
		return;
	}
	SlotBrowser::selectEncoderAction(offset);
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

bool SaveUI::dispatchSave(const deluge::storage::SaveRequest& request) {
	// Single-flight by REFUSAL, not by superseding (see dispatchSave()'s doc), and across EVERY save. A song save
	// releases saveInFlight_ once its name is known to be free, and its collection and document machines run on
	// for many ticks after that: a preset save started then would serialise into the shared serialiser under the
	// song's staged document. The caller treats false as a refusal: nothing was written, and nothing was
	// promised.
	if (anySaveInFlight()) {
		return false;
	}
	saveInFlight_ = true;
	inFlightRequest_ = request;

	// A document save builds itself in memory first and then stores it in one operation, through the
	// staged phases saveRoutine() drives. Everything else -- the song save, whose save copies samples
	// around the card -- takes the unstaged path below.
	if (savesStagedDocument()) {
		if (!beforeStagedWrite(request)) {
			// Refused before anything was built or written, exactly as an unstaged save refused at its start is
			// did: release the guard, and let the caller's completion close whatever it had opened.
			saveInFlight_ = false;
			runSaveCompletion(request, false);
			return true;
		}
		stagedWroteDocument_ = false;
		// A confirmed overwrite writes a temp file beside its target and swaps it in only once it is known good.
		stagedToTemp_ = request.mayOverwrite;
		swapStarted_ = false;
		stagedWritePath_.clear();
		phaseOwner_ = this;
		savePhase_ = SavePhase::SERIALIZE;
		writeIssued_ = false;
		stagedSucceeded_ = false;
		stallBound_.reset();
		// Attempt it once here rather than waiting for the next saveRoutine() pass, for the reason
		// beginLoad() does: work dispatched during boot must not sit waiting for a repeating task
		// that is not running yet.
		saveRoutine();
		return true;
	}

	// Every save that is not a staged document -- the song save -- starts here, on the interaction
	// tier, and resolves over later ticks. None of it blocks this tier after boot.
	if (!beginUnstagedSave(request)) {
		endUnstagedDispatch(request, false);
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
void SaveUI::waitUntil(uint64_t deadlineUs) {
	waitedThisRun_ = true;
	deluge::runtime::appLoop.requestAt(saveJob, deadlineUs);
}

void SaveUI::saveRoutine() {
	// A machine still in flight after its step either waits (see waitUntil) or has more to do at once.
	bool moreNow = false;
	// The song save's sample collection runs on its own machine, for the reason its enum is separate:
	// its flow is not a staged document write and sharing one would make both harder to follow.
	if (saveSongUI.collectInFlight()) {
		waitedThisRun_ = false;
		saveSongUI.collectRoutine();
		moreNow |= saveSongUI.collectInFlight() && !waitedThisRun_;
	}
	// The song's document write runs on a machine of its own, entered when the collection finishes.
	if (saveSongUI.docInFlight()) {
		waitedThisRun_ = false;
		saveSongUI.docRoutine();
		moreNow |= saveSongUI.docInFlight() && !waitedThisRun_;
	}
	if (phaseOwner_ != nullptr && savePhase_ != SavePhase::NONE) {
		waitedThisRun_ = false;
		runStagedStep();
		moreNow |= phaseOwner_ != nullptr && savePhase_ != SavePhase::NONE && !waitedThisRun_;
	}
	if (moreNow) {
		deluge::runtime::appLoop.request(saveJob);
	}
}

void SaveUI::runStagedStep() {
	switch (savePhase_) {
	case SavePhase::NONE:
		return;
	case SavePhase::SERIALIZE:
		phaseOwner_->runSerializePhase();
		return;
	case SavePhase::WRITE:
		phaseOwner_->runWritePhase();
		return;
	case SavePhase::VERIFY:
		phaseOwner_->runStagedVerifyPhase();
		return;
	case SavePhase::SWAP:
		phaseOwner_->runSwapPhase();
		return;
	case SavePhase::FINISH:
		phaseOwner_->runFinishPhase();
		return;
	}
}

bool SaveUI::deferStagedSave() {
	// Every step of a staged save waits on the byte channel, so only a span in which it finished nothing counts:
	// a save held off by a long copy waits for it, and a wedged channel still fails the save.
	if (stallBound_.tick(deluge_file_bytes_progress())) {
		return false;
	}
	waitUntil(stallBound_.deadlineUs());
	return true;
}

void SaveUI::runSerializePhase() {
	// No card contact at all. This is the half of a save that needs no storage.
	smSerializer.reset();
	smSerializer.setMemoryBased();
	// The prolog: createXMLFile() writes this automatically when it opens a file on the card. Building
	// the document in memory instead means it has to be written here explicitly, or every preset saves headerless.
	smSerializer.write(kStagedDocumentHead);
	if (!serializeForSave(smSerializer)) {
		endStagedSave(false);
		return;
	}
	if (smSerializer.bytesWritten() <= 0) {
		// An empty document is a failed save, not a small one.
		endStagedSave(false);
		return;
	}
	// Where the document goes. A confirmed overwrite writes beside its target, so the document it replaces survives
	// a failed write or read-back.
	stagedWritePath_ =
	    stagedToTemp_ ? deluge::storage::tempPathFor(inFlightRequest_.filePath) : inFlightRequest_.filePath;
	if (stagedWritePath_.empty()) {
		// No temp name fits the channels' path bound, and rewriting the target in place is what the temp file avoids.
		display->displayError(Error::WRITE_FAIL);
		endStagedSave(false);
		return;
	}
	savePhase_ = SavePhase::WRITE;
	stallBound_.reset();
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
	const char* path = stagedWritePath_.c_str();
	// A temp save overwrites only its own leftover from an earlier failed save; a direct save never overwrites.
	const bool mayOverwrite = inFlightRequest_.mayOverwrite;

	const bool booting = !deluge::boot::finished();

	if (booting) {
		// Blocking is legal here, and the driver could not run anyway: a request issued from a
		// context that will not yield can never be answered.
		bool alreadyExists = false;
		bool ok = false;
		writeStagedSynchronously(alreadyExists, ok);
		handleWriteOutcome(alreadyExists, ok);
		return;
	}

	// Past this point the write goes through the channel, from the interaction tier. This counter is
	// what a gate reads to confirm the write went through the channel.
	++channelSaveTicks_;

	if (!writeIssued_) {
		if (!deluge_file_bytes_request_write_from(path, bytes, len, mayOverwrite)) {
			// Refused, not failed: nothing was queued, so ask again next tick.
			if (!deferStagedSave()) {
				endStagedSave(false);
			}
			return;
		}
		writeIssued_ = true;
		stallBound_.reset();
		// Only its answer ends this wait: see the PENDING branch below.
		waitUntil(deluge::runtime::kNever);
		return;
	}

	uint32_t writtenLen = 0;
	DelugeStatus error = DELUGE_OK;
	const DelugeChannelStatus collected = deluge_file_bytes_collect_write_from(path, &writtenLen, &error);
	if (collected == DELUGE_CHANNEL_PENDING) {
		// Waited out however long the card takes, and not counted against the stall bound: the write is copying
		// from the staged document and cannot be cancelled, so giving up would release the serialiser under it and
		// remove a temp file it still has open. Only its answer brings the job back.
		waitUntil(deluge::runtime::kNever);
		return;
	}
	if (collected == DELUGE_CHANNEL_UNKNOWN) {
		// Never keyed, so nothing is coming. Re-issue rather than wait forever -- at once, since no answer will wake
		// it.
		writeIssued_ = false;
		if (!deferStagedSave()) {
			endStagedSave(false);
			return;
		}
		deluge::runtime::appLoop.request(saveJob);
		return;
	}
	writeIssued_ = false;
	if (collected == DELUGE_CHANNEL_FAILED) {
		handleWriteOutcome(error == DELUGE_ERR_EXISTS, false);
		return;
	}
	++writeChannelServedOps_;
	handleWriteOutcome(false, writtenLen == len);
}

void SaveUI::writeStagedSynchronously(bool& alreadyExists, bool& ok) {
	alreadyExists = false;
	ok = false;
	const auto* bytes = reinterpret_cast<const uint8_t*>(smSerializer.getBufferPtr());
	const auto len = static_cast<uint32_t>(smSerializer.bytesWritten());
	auto created = StorageManager::createFile(stagedWritePath_.c_str(), inFlightRequest_.mayOverwrite);
	if (!created) {
		alreadyExists = created.error() == Error::FILE_ALREADY_EXISTS;
		return;
	}
	auto written = created->write(std::span<const std::byte>(reinterpret_cast<const std::byte*>(bytes), len));
	ok = written.has_value() && *written == len;
	(void)created->close();
}

deluge::storage::ReadBackExpectation SaveUI::stagedReadBackExpectation() const {
	return {.staged = reinterpret_cast<const uint8_t*>(smSerializer.getBufferPtr()),
	        .length = static_cast<uint32_t>(smSerializer.bytesWritten()),
	        .head = kStagedDocumentHead,
	        .tail = verifyTailForSave()};
}

void SaveUI::runStagedVerifyPhase() {
	using deluge::storage::ReadBackTick;

	const char* path = stagedWritePath_.c_str();
	const bool booting = !deluge::boot::finished();

	if (booting) {
		++verifyInlineServedOps_;
		finishStagedVerify(deluge::storage::readBackSynchronously(path, stagedReadBackExpectation()));
		return;
	}

	const ReadBackTick tick = stagedReadBack_.step(path);
	// Every answer the channel gave counts, including the one that failed the check.
	verifyChannelServedOps_ += stagedReadBack_.takeServedOps();
	switch (tick) {
	case ReadBackTick::Waiting:
		if (!deferStagedSave()) {
			endStagedSave(false);
		}
		return;
	case ReadBackTick::Failed:
		finishStagedVerify(false);
		return;
	case ReadBackTick::Advanced:
		stallBound_.reset();
		return;
	case ReadBackTick::Passed:
		stallBound_.reset();
		finishStagedVerify(true);
		return;
	}
}

void SaveUI::finishStagedVerify(bool passed) {
	if (!passed) {
		// The written file failed its check. A direct save wrote a free name, and this stops it being reported as a
		// saved document; a temp save's target is untouched, and endStagedSave() discards the temp file.
		display->displayError(Error::WRITE_FAIL);
		endStagedSave(false);
		return;
	}
	++stagedSavesVerified_;
	completeStagedWrite();
}

void SaveUI::handleWriteOutcome(bool alreadyExists, bool ok) {
	if (alreadyExists) {
		// The name is taken and the user did not ask to replace it. Ask them -- and report the save
		// as "dealt with", which is what a prompt means.
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
	if (verifyTailForSave() != nullptr) {
		// Before endStagedSave(), which releases the shared serialiser and with it the staged bytes this
		// check compares against.
		stagedReadBack_.begin(stagedReadBackExpectation());
		stallBound_.reset();
		savePhase_ = SavePhase::VERIFY;
		return;
	}
	// A save that never verifies. Counted in finishStagedDocument() and not in endStagedSave(true), which the
	// overwrite prompt branch above also calls having written nothing.
	completeStagedWrite();
}

void SaveUI::completeStagedWrite() {
	if (!stagedToTemp_) {
		finishStagedDocument();
		return;
	}
	// The temp file is written and, where this save checks, verified. Only now is the target touched.
	stagedSwap_.begin(inFlightRequest_.filePath, stagedWritePath_);
	stallBound_.reset();
	savePhase_ = SavePhase::SWAP;
}

void SaveUI::finishStagedDocument() {
	++stagedSavesCompleted_;
	stagedWroteDocument_ = true;
	endStagedSave(true);
}

void SaveUI::runSwapPhase() {
	using deluge::storage::SwapTick;

	// From here a failure keeps the temp file: once the target is deleted it is the only good copy.
	swapStarted_ = true;
	const bool booting = !deluge::boot::finished();
	if (booting) {
		if (!deluge::storage::swapSynchronously(inFlightRequest_.filePath, stagedWritePath_)) {
			display->displayError(Error::SD_CARD);
			endStagedSave(false);
			return;
		}
		finishStagedDocument();
		return;
	}

	++channelSaveTicks_;
	const SwapTick tick = stagedSwap_.step();
	swapChannelServedOps_ += stagedSwap_.takeServedOps();
	switch (tick) {
	case SwapTick::Waiting:
		if (!deferStagedSave()) {
			display->displayError(Error::SD_CARD);
			endStagedSave(false);
		}
		return;
	case SwapTick::Advanced:
		stallBound_.reset();
		return;
	case SwapTick::Done:
		stallBound_.reset();
		finishStagedDocument();
		return;
	case SwapTick::Failed:
		display->displayError(Error::SD_CARD);
		endStagedSave(false);
		return;
	}
}

void SaveUI::runFinishPhase() {
	const deluge::storage::SaveRequest request = inFlightRequest_;
	const bool succeeded = stagedSucceeded_;
	SaveUI* const ui = phaseOwner_;

	savePhase_ = SavePhase::NONE;
	phaseOwner_ = nullptr;
	// Released BEFORE the completion, exactly as endUnstagedDispatch() releases it: a completion may
	// legitimately start another save (confirming the overwrite prompt is one), and it must not be
	// refused by this save's own guard.
	saveInFlight_ = false;

	// Only a save whose document genuinely landed does its follow-up. `succeeded` is not that signal:
	// opening the overwrite prompt ends a staged save with it set, having written nothing at all.
	if (stagedWroteDocument_) {
		stagedWroteDocument_ = false;
		ui->afterStagedSave(request);
	}

	ui->runSaveCompletion(request, succeeded);
}

void SaveUI::endStagedSave(bool succeeded) {
	if (!succeeded && stagedToTemp_ && !swapStarted_ && !stagedWritePath_.empty()) {
		// Failed before the swap: the target is untouched, and the temp file is at best a copy that failed its check.
		// Best effort; a leftover is hidden from every browser.
		deluge::storage::discardTemp(stagedWritePath_);
	}
	// The shared serialiser is handed back here and not one tick later: every path out of a save
	// passes through this function, and leaving it borrowed breaks the next save that wants a file.
	releaseStagedWriter();
	writeIssued_ = false;
	stallBound_.reset();
	stagedSucceeded_ = succeeded;
	savePhase_ = SavePhase::FINISH;
	runFinishPhase();
}

void SaveUI::endUnstagedDispatch(const deluge::storage::SaveRequest& request, bool succeeded) {
	// By value: the caller may pass a reference into state a completion is about to overwrite.
	const deluge::storage::SaveRequest ended = request;
	// Release BEFORE the completion: a completion may legitimately start another save (confirming
	// the overwrite prompt is one), and it must not be refused by this save's own guard.
	saveInFlight_ = false;
	runSaveCompletion(ended, succeeded);
}

// Runs the follow-up for whichever caller dispatched this save. Every arm runs on the interaction
// tier, from dispatchSave() or saveRoutine(), where closing a context menu is legal.
void SaveUI::runSaveCompletion(const deluge::storage::SaveRequest& request, bool succeeded) {
	using deluge::storage::SaveConsumer;
	switch (request.consumer) {
	case SaveConsumer::EnterKey:
		// Nothing followed the call at enterKeyPress(): a finished save closes the UI itself, and a
		// failed one displays its own error.
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
