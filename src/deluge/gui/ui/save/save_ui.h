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
#include "io/stall_bound.h"
#include "storage/readback_check.h"
#include "storage/save_dispatch.h"
#include "storage/temp_swap.h"

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
	ActionResult buttonAction(deluge::hid::Button b, bool on) final;
	ActionResult exitUI() final;
	ActionResult padAction(int32_t x, int32_t y, int32_t velocity) final;
	ActionResult verticalEncoderAction(int32_t offset) final;
	ActionResult horizontalEncoderAction(int32_t offset) final;
	void selectEncoderAction(int8_t offset) final;
	/// @}

	/// @brief Whether any save -- a staged document or the song save -- is still running.
	[[nodiscard]] static bool anySaveInFlight();

protected:
	/// @brief Note that the machine being run ends this run waiting: on the channel's DELUGE_WAKE_STORAGE, and at the
	///        latest at @p deadlineUs (a stall bound's deadline, or a write-from's re-check).
	static void waitUntil(uint64_t deadlineUs);
	/// Set by @ref waitUntil during the machine's step; a machine still in flight that did not set it runs again at
	/// once.
	static inline bool waitedThisRun_{};

	// void displayText(bool blinkImmediately) final;
	void enterKeyPress() final;
	/// @brief A save screen's own button handling, reached only when no save is in flight.
	///
	/// Override THIS, not buttonAction(): buttonAction() is final so that the in-flight refusal cannot be
	/// skipped by a screen that handles BACK itself before forwarding.
	virtual ActionResult saveScreenButtonAction(deluge::hid::Button b, bool on);
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
	/// pure computation, and that is the asymmetry that lets a save store its document in one
	/// operation without the segmentation a load needs.
	virtual bool serializeForSave(XMLSerializer& into) { return false; }

	/// @brief Whether this UI's save is a document that can be built in memory first.
	///
	/// False for SaveSongUI, whose save copies samples around the card and is not a document write.
	/// It takes the unstaged path instead: beginUnstagedSave() and SaveSongUI's own machines.
	[[nodiscard]] virtual bool savesStagedDocument() const { return false; }

	/// @brief The closing bytes this save's document must end with, or nullptr for no read-back.
	///
	/// Non-null only for saves whose document is verified by checking that it ends with these bytes;
	/// building the document in memory leaves no file to reopen, so this is how that check runs
	/// instead. A save that is not verified returns nullptr and gains nothing it did not have.
	[[nodiscard]] virtual const char* verifyTailForSave() const { return nullptr; }

	/// @brief Run this save's checks BEFORE its document is built, and refuse if they fail.
	///
	/// Where a staged save's preconditions live -- a preset may not take a slot another instrument in
	/// the song already holds.
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
	/// @brief Read-back operations the byte CHANNEL served, and read-backs the INLINE arm ran before boot finished.
	[[nodiscard]] static int32_t verifyChannelServedOps() { return verifyChannelServedOps_; }
	[[nodiscard]] static int32_t verifyInlineServedOps() { return verifyInlineServedOps_; }

	/// @brief Drive a staged save one step, and the song save's collection and document machines. Run by
	///        @ref saveJob.
	static void saveRoutine();

	/// @brief The job that runs @ref saveRoutine, set by `registerJobs`.
	///
	/// It has no gap and listens for `DELUGE_WAKE_STORAGE`: a save's start requests it, and each run asks for the next
	/// one it needs -- at once when a machine can move again, or at the stall bound's deadline of the wait it is on.
	static inline deluge::runtime::JobId saveJob{deluge::runtime::kNoJob};

	/// @brief Whether a staged save is mid-flight. For gates; NOT the same as the UI being open.
	[[nodiscard]] static bool saveInFlight() { return saveInFlight_; }

	/// @brief Whole-document writes the CHANNEL has served, and ticks a staged step went through a channel.
	[[nodiscard]] static int32_t writeChannelServedOps() { return writeChannelServedOps_; }
	[[nodiscard]] static int32_t channelSaveTicks() { return channelSaveTicks_; }
	/// @brief Where the in-flight staged save writes its document, or the last one wrote it: the target, or its
	///        temp file when the save replaces an existing document.
	[[nodiscard]] static const std::string& stagedWritePath() { return stagedWritePath_; }
	/// @brief Byte-channel answers the temp-file swaps have collected: the target's delete and the rename.
	[[nodiscard]] static int32_t swapChannelServedOps() { return swapChannelServedOps_; }

private:
	/// @brief Which part of a staged save runs next.
	enum class SavePhase : uint8_t {
		NONE,      ///< No staged save in flight.
		SERIALIZE, ///< Building the document in memory, on the interaction tier.
		WRITE,     ///< One whole-document write: through the channel, or inline before boot finishes.
		VERIFY,    ///< Reading the written document back: length, head and tail.
		SWAP,      ///< Replacing the target with the verified temp file: delete it, then rename. Overwrites only.
		FINISH,    ///< Running the caller's completion.
	};

	/// @brief Run the staged save's current phase.
	static void runStagedStep();
	void runSerializePhase();
	void runWritePhase();
	void runFinishPhase();
	/// @brief Drive the swap of a temp save's verified document onto its target.
	void runSwapPhase();
	/// @brief A written, and where this save checks, verified document: swap it into place, or finish.
	void completeStagedWrite();
	/// @brief Count the document as saved and end the save successfully.
	void finishStagedDocument();
	/// @brief Turn one write's outcome into a prompt, an error, or a success.
	void handleWriteOutcome(bool alreadyExists, bool ok);
	/// @brief Store the staged document with a blocking write. Legal ONLY before boot finishes (the inline arm).
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
	/// @brief Defer one more caller tick, or give up once the byte channel has finished nothing for the stall bound.
	static bool deferStagedSave();

	static inline int32_t writeChannelServedOps_{};
	static inline int32_t channelSaveTicks_{};
	static inline SavePhase savePhase_{SavePhase::NONE};
	/// The UI whose staged save is running. The phase state is static because saves are
	/// single-flight, so this says which instance the static state belongs to.
	static inline SaveUI* phaseOwner_{nullptr};
	static inline bool writeIssued_{};
	static inline deluge::storage::ReadBackCheck stagedReadBack_{};
	/// Where this staged save writes: the target, or `tempPathFor(target)` when it replaces an existing document.
	static inline std::string stagedWritePath_{};
	/// Whether this staged save writes a temp file and swaps it into place. Set from `mayOverwrite` at dispatch.
	static inline bool stagedToTemp_{};
	/// Whether the swap has begun. From then on a failure keeps the temp file, which may be the only good copy.
	static inline bool swapStarted_{};
	static inline deluge::storage::TempSwap stagedSwap_{};
	static inline int32_t swapChannelServedOps_{};
	static inline int32_t stagedSavesCompleted_{};
	/// Whether the in-flight staged save genuinely put its document on the card. Gates afterStagedSave().
	static inline bool stagedWroteDocument_{};
	static inline int32_t stagedSavesVerified_{};
	static inline int32_t verifyChannelServedOps_{};
	static inline int32_t verifyInlineServedOps_{};
	/// The prolog runSerializePhase() writes, and so the head every staged document must start with.
	static constexpr const char* kStagedDocumentHead = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
	static inline bool stagedSucceeded_{};
	static inline deluge::io::StallBound stallBound_{};
	/// The in-flight save, if any. Single-flight by refusal: see dispatchSave().
	static inline bool saveInFlight_{};
	static inline deluge::storage::SaveRequest inFlightRequest_{};
};
