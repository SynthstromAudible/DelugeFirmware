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

#pragma once

#include "definitions_cxx.hpp"
#include "gui/ui/browser/slot_browser.h"
#include "hid/button.h"
#include "storage/readback_check.h"
#include "storage/save_dispatch.h"

class SaveUI : public SlotBrowser {
public:
	SaveUI();
	bool opened() override;

	void focusRegained() override;
	bool renderSidebar(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth] = nullptr,
	                   uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth] = nullptr) override {
		return true;
	}
	bool canSeeViewUnderneath() final { return false; }
	ActionResult timerCallback() override;
	/// @name Input while a save is running
	/// A save spans many ticks with the UI live, and each of these could take the save screen away
	/// mid-save -- after which the save's own close() ran against a UI stack it was no longer on. They
	/// are refused while any save is in flight, and they are final so no save screen can bypass that.
	/// @{
	ActionResult buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) final;
	ActionResult exitUI() final;
	ActionResult padAction(int32_t x, int32_t y, int32_t velocity) final;
	ActionResult verticalEncoderAction(int32_t offset, bool inCardRoutine) final;
	ActionResult horizontalEncoderAction(int32_t offset) final;
	void selectEncoderAction(int8_t offset) final;
	/// @}

	/// @brief Whether any save -- a staged document or the song save -- is still running.
	[[nodiscard]] static bool anySaveInFlight();

protected:
	// void displayText(bool blinkImmediately) final;
	void enterKeyPress() final;
	/// @brief A save screen's own button handling, reached only when no save is in flight.
	///
	/// Override THIS, not buttonAction(): buttonAction() is final so that the in-flight refusal cannot be
	/// skipped by a screen that handles BACK itself before forwarding.
	virtual ActionResult saveScreenButtonAction(deluge::hid::Button b, bool on, bool inCardRoutine);
	static bool currentFolderIsEmpty;

	/// The save round-trip gate drives the REAL ENTER gesture rather than the save machinery underneath
	/// it, so it needs the protected entry. Mirrors BrowserListingAbProbe's friendship in browser.h.
	friend class SaveRoundtripProbe;
	/// The song-load channel gate saves a song to load back, and drives the same real gesture for the
	/// same reason: a save made by calling into the save machinery directly would not be the save a user makes.
	friend class SongLoadChannelProbe;
	/// @brief End an unstaged save's dispatch: release the single-flight guard, then run the completion.
	///
	/// The guard is released FIRST because a completion may legitimately start another save --
	/// confirming the overwrite prompt is one -- and it must not be refused by this save's own guard.
	void endUnstagedDispatch(const deluge::storage::SaveRequest& request, bool succeeded);

	/// The document-save channel gate drives the same real gesture, for the same reason.
	friend class DocumentSaveChannelProbe;
	/// The song-save channel gate drives the same real gesture, for the same reason.
	friend class SongSaveChannelProbe;

public:
	/// @brief Start one save.
	///
	/// @note Save does NOT coalesce. A second save arriving while one is in flight is REFUSED, not
	///       queued and not superseded: LatestWins silently drops the middle of three rapid
	///       requests, which for a repaint is fine and for a save is a lost file.
	///
	/// @param request The snapshot; see SaveRequest.
	/// @return true if the save was started, or ended at once having been dealt with; false only if any
	///         save was already in flight -- a staged document, or a song save still collecting or writing
	///         its document -- in which case nothing was written.
	[[nodiscard]] bool dispatchSave(const deluge::storage::SaveRequest& request);

	/// @brief Start a save that is not a staged document -- the song save.
	///
	/// Runs on the interaction tier and must not block: anything that needs the card resolves on later
	/// ticks and ends the dispatch with endUnstagedDispatch().
	/// @param request The dispatched save.
	/// @return false to end the dispatch as failed immediately.
	[[nodiscard]] virtual bool beginUnstagedSave(const deluge::storage::SaveRequest& /*request*/) { return false; }

	/// @brief Runs one save's caller-specific follow-up, on the interaction tier.
	/// @param request   The snapshot the save ran on.
	/// @param succeeded Whether the save succeeded or was otherwise dealt with (the overwrite prompt opening).
	void runSaveCompletion(const deluge::storage::SaveRequest& request, bool succeeded);

	/// @brief `collectingSamples` for the snapshot. SaveSongUI owns the flag; the base builds the
	///        request, so it needs a way to read it without knowing the subclass.
	[[nodiscard]] virtual bool collectingSamplesForDispatch() const { return false; }

	/// @brief Serialise this save's document into @p into.
	///
	/// Runs on the INTERACTION tier and must touch no storage whatsoever -- building a document is
	/// pure computation, and that is the asymmetry that lets a save leave the owner without the
	/// segmentation a load needs.
	virtual bool serializeForSave(XMLSerializer& into) { return false; }

	/// @brief Whether this UI's save is a document that can be built in memory first.
	///
	/// False for SaveSongUI, whose save copies samples around the card and is not a document write.
	/// It keeps the owner-side path unchanged, which is what keeps it out of this conversion.
	[[nodiscard]] virtual bool savesStagedDocument() const { return false; }

	/// @brief The closing bytes this save's document must end with, or nullptr for no read-back.
	///
	/// Non-null only for the saves the streaming writer used to verify by reopening the file:
	/// building the document in memory skipped that check, and this is how it comes back. A save that
	/// never verified returns nullptr and gains nothing it did not have.
	[[nodiscard]] virtual const char* verifyTailForSave() const { return nullptr; }

	/// @brief Run this save's checks BEFORE its document is built, and refuse if they fail.
	///
	/// Where a staged save's preconditions live -- a preset may not take a slot another instrument in
	/// the song already holds. Staging moved every one of these saves off the function that used to run them, which was
	/// the only place such checks ran, and for a while they ran nowhere.
	/// @param request The dispatched save.
	/// @return false to refuse; the save then ends as failed and writes nothing.
	[[nodiscard]] virtual bool beforeStagedWrite(const deluge::storage::SaveRequest& /*request*/) { return true; }

	/// @brief What this save does once its document is genuinely on the card.
	///
	/// Closing the screen, confirming, and giving the saved object its new name. Runs only after a
	/// completed write -- never after a refused one, and never after the overwrite prompt opens, which
	/// also ends a staged save "successfully" having written nothing.
	/// @param request The dispatched save.
	virtual void afterStagedSave(const deluge::storage::SaveRequest& /*request*/) {}

	/// @brief Staged saves that wrote their document and, where they verify, passed the read-back.
	///
	/// NOT the number of times a staged save ended "successfully": a save that opens the overwrite
	/// prompt also ends that way, having written nothing.
	[[nodiscard]] static int32_t stagedSavesCompleted() { return stagedSavesCompleted_; }
	/// @brief Staged saves whose written document passed the read-back.
	[[nodiscard]] static int32_t stagedSavesVerified() { return stagedSavesVerified_; }
	/// @brief Read-back operations the byte CHANNEL served, and read-backs the storage OWNER ran.
	[[nodiscard]] static int32_t verifyChannelServedOps() { return verifyChannelServedOps_; }
	[[nodiscard]] static int32_t verifyOwnerServedOps() { return verifyOwnerServedOps_; }

	/// @brief Drive a staged save one step. Registered as a repeating task in deluge.cpp.
	static void saveRoutine();

	/// @brief Whether a staged save is mid-flight. For gates; NOT the same as the UI being open.
	[[nodiscard]] static bool saveInFlight() { return saveInFlight_; }

	/// @brief Whole-document writes the CHANNEL has served, and staged steps that ran off the owner.
	[[nodiscard]] static int32_t writeChannelServedOps() { return writeChannelServedOps_; }
	[[nodiscard]] static int32_t offOwnerSaveTicks() { return offOwnerSaveTicks_; }

private:
	/// @brief Which part of a staged save runs next.
	enum class SavePhase : uint8_t {
		NONE,      ///< No staged save in flight.
		SERIALIZE, ///< Building the document in memory, on the interaction tier.
		WRITE,     ///< One whole-document write: through the channel, or inline on the owner.
		VERIFY,    ///< Reading the written document back: length, head and tail.
		FINISH,    ///< Running the caller's completion.
	};

	void runSerializePhase();
	void runWritePhase();
	void runFinishPhase();
	/// @brief Turn one write's outcome into a prompt, an error, or a success.
	void handleWriteOutcome(bool alreadyExists, bool ok);
	/// @brief Store the staged document with a blocking write. Legal ONLY on the storage owner.
	void writeStagedSynchronously(bool& alreadyExists, bool& ok);
	/// @brief Drive the read-back of the written document. Named apart from SaveSongUI's own verify.
	void runStagedVerifyPhase();
	/// @brief End the save on the read-back's verdict.
	void finishStagedVerify(bool passed);
	/// @brief What the written document must match.
	[[nodiscard]] deluge::storage::ReadBackExpectation stagedReadBackExpectation() const;
	/// @brief Release the staged document and end the save. `succeeded` reaches the completion.
	void endStagedSave(bool succeeded);
	/// @brief Hand the shared serialiser back to file-backed use.
	static void releaseStagedWriter();
	/// @brief Defer one more tick, or give up if this save has stopped making progress.
	static bool deferStagedSave();

	static inline int32_t writeChannelServedOps_{};
	static inline int32_t offOwnerSaveTicks_{};
	static inline SavePhase savePhase_{SavePhase::NONE};
	/// The UI whose staged save is running. The phase state is static because saves are
	/// single-flight, so this says which instance the static state belongs to.
	static inline SaveUI* phaseOwner_{nullptr};
	static inline bool writeIssued_{};
	static inline deluge::storage::ReadBackCheck stagedReadBack_{};
	static inline int32_t stagedSavesCompleted_{};
	/// Whether the in-flight staged save genuinely put its document on the card. Gates afterStagedSave().
	static inline bool stagedWroteDocument_{};
	static inline int32_t stagedSavesVerified_{};
	static inline int32_t verifyChannelServedOps_{};
	static inline int32_t verifyOwnerServedOps_{};
	/// The prolog runSerializePhase() writes, and so the head every staged document must start with.
	static constexpr const char* kStagedDocumentHead = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
	static inline bool stagedSucceeded_{};
	static inline int32_t stallTicks_{};
	/// The in-flight save, if any. Single-flight by refusal: see dispatchSave().
	static inline bool saveInFlight_{};
	static inline deluge::storage::SaveRequest inFlightRequest_{};
};
