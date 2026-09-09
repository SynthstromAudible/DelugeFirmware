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
#include "storage/save_dispatch.h"

class SaveUI : public SlotBrowser {
public:
	SaveUI();
	bool opened() override;

	virtual bool performSave(bool mayOverwrite = false) = 0; // Returns true if success, or if otherwise dealt with
	                                                         // (e.g. "overwrite" context menu brought up)
	void focusRegained() override;
	bool renderSidebar(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth] = nullptr,
	                   uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth] = nullptr) override {
		return true;
	}
	bool canSeeViewUnderneath() final { return false; }
	ActionResult timerCallback() override;
	ActionResult buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) override;

protected:
	// void displayText(bool blinkImmediately) final;
	void enterKeyPress() final;
	static bool currentFolderIsEmpty;

	/// The save round-trip gate drives the REAL ENTER gesture rather than performSave() underneath
	/// it, so it needs the protected entry. Mirrors BrowserListingAbProbe's friendship in browser.h.
	friend class SaveRoundtripProbe;
	/// The song-load channel gate saves a song to load back, and drives the same real gesture for the
	/// same reason: a save made by calling performSave() directly would not be the save a user makes.
	friend class SongLoadChannelProbe;

public:
	/// @brief Dispatch one save onto the storage owner.
	///
	/// @note Save does NOT coalesce. A second save arriving while one is in flight is REFUSED, not
	///       queued and not superseded: LatestWins silently drops the middle of three rapid
	///       requests, which for a repaint is fine and for a save is a lost file.
	///
	/// @param request The snapshot; see SaveRequest.
	/// @return true if the save was dispatched (or ran inline); false if one was already in flight
	///         or the owner queue was full, in which case nothing was written.
	[[nodiscard]] bool dispatchSave(const deluge::storage::SaveRequest& request);

	/// @brief The dispatched save op: runs performSave() on the owner, then the caller's completion.
	/// @param self The SaveUI instance.
	static void runSaveOp(void* self);

	/// @brief Runs one save's caller-specific follow-up, on the owner.
	/// @param request   The snapshot the op ran on.
	/// @param succeeded What performSave() returned.
	void runSaveCompletion(const deluge::storage::SaveRequest& request, bool succeeded);

	/// @brief `collectingSamples` for the snapshot. SaveSongUI owns the flag; the base builds the
	///        request, so it needs a way to read it without knowing the subclass.
	[[nodiscard]] virtual bool collectingSamplesForDispatch() const { return false; }

	/// @brief How many times a performSave() body has begun on the INTERACTION tier.
	///
	/// Writing the card from there is the violation this rung exists to remove, so the gate asserts
	/// this is zero. Counting rather than freezing lets the gate report the number before the
	/// conversion as well as after it; the hard assertion lands alongside.
	[[nodiscard]] static int32_t interactionTierSaves() { return interactionTierSaves_; }
	/// @brief Record one performSave() entry. Call as the FIRST statement of every override.
	static void noteSaveTier();

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
		WRITE,     ///< One whole-document write, through the channel or handed to the owner.
		FINISH,    ///< Running the caller's completion.
	};

	void runSerializePhase();
	void runWritePhase();
	void runFinishPhase();
	/// @brief Turn one write's outcome into a prompt, an error, or a success.
	void handleWriteOutcome(bool alreadyExists, bool ok);
	/// @brief Release the staged document and end the save. `succeeded` reaches the completion.
	void endStagedSave(bool succeeded);
	/// @brief Hand the shared serialiser back to file-backed use.
	static void releaseStagedWriter();
	/// @brief Defer one more tick, or give up if this save has stopped making progress.
	static bool deferStagedSave();

	static inline int32_t interactionTierSaves_{};
	static inline int32_t writeChannelServedOps_{};
	static inline int32_t offOwnerSaveTicks_{};
	static inline SavePhase savePhase_{SavePhase::NONE};
	/// The UI whose staged save is running. The phase state is static because saves are
	/// single-flight, so this says which instance the static state belongs to.
	static inline SaveUI* phaseOwner_{nullptr};
	static inline bool writeIssued_{};
	static inline bool stagedSucceeded_{};
	static inline int32_t stallTicks_{};
	/// The in-flight save, if any. Single-flight by refusal: see dispatchSave().
	static inline bool saveInFlight_{};
	static inline deluge::storage::SaveRequest inFlightRequest_{};
};
