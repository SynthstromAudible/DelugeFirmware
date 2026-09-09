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

#include "gui/ui/save/save_ui.h"

class AudioFile;
#include <string>
#include <vector>

class ContextMenu;

class SaveSongUI final : public SaveUI {
public:
	SaveSongUI();
	bool opened() override;
	void focusRegained() override;
	// void selectEncoderAction(int8_t offset);
	bool performSave(bool mayOverwrite = false) override;

	/// @brief What one AudioFile needs done, decided but not yet performed.
	///
	/// ONE ENTRY PER FILE carrying up to TWO operations, because the second's input depends on the
	/// first's OUTCOME. A recording still at its temporary path is renamed into place, and that rename
	/// clears `tempFilePathForRecording` on success -- which is exactly what the copy then consults to
	/// choose its source. A failed rename is not fatal: it warns, leaves the temporary path set, and
	/// the copy reads from there instead. A flat list of independent transfers cannot express that.
	struct PlannedTransfer {
		AudioFile* file{};

		/// Set when a recording must be moved out of its temporary location first.
		bool renameTemp = false;
		std::string renameFrom;
		std::string renameTo;

		/// Set when the file must also be copied to or from an alternate location.
		bool copy = false;
		/// The source to use IF the rename succeeded, or if there was none.
		std::string copyFromAfterRename;
		/// The source to use if the rename FAILED and the temporary file is still where it was.
		std::string copyFromIfRenameFailed;
		std::string copyTo;

		/// What `filePath` becomes, empty if unchanged. COMPUTED here, applied later.
		std::string newFilePath;
		/// What `loadedFromAlternatePath` becomes.
		std::string newAlternatePath;
		/// Whether `loadedFromAlternatePath` must be CLEARED, which is what a successful copy does
		/// when this save is not collecting media. Distinct from "unchanged", hence not just an empty
		/// string above.
		bool clearAlternatePath = false;
		/// Whether `tempFilePathForRecording` must be cleared -- the rename's own mutation, deferred.
		bool clearTempPath = false;
	};

	/// @brief Decide what every referenced audio file needs, WITHOUT performing or applying any of it.
	///
	/// @param into               Receives one entry per file that needs work.
	/// @param collectingSamples  Whether this save is collecting media into the song's own folder.
	/// @param songAlternatePath  The song's alternate-media folder, as performSave built it.
	/// @param alternateDirLength Its length, which setupAlternateAudioFilePath wants.
	/// @return false if a path could not be built, which fails the save exactly as it did before.
	[[nodiscard]] bool planTransfers(std::vector<PlannedTransfer>& into, bool collectingSamples,
	                                 const std::string& songAlternatePath, int32_t alternateDirLength);

	/// @brief Drive one step of a sample collection. Called from SaveUI::saveRoutine().
	void collectRoutine();

	/// @brief Whether a sample collection is in flight. NOT the same as the save UI being open.
	[[nodiscard]] bool collectInFlight() const { return collectPhase_ != CollectPhase::NONE; }

	/// @brief Transfers the CHANNEL served, and steps that ran off the storage owner.
	[[nodiscard]] static int32_t transferChannelServedOps() { return transferChannelServedOps_; }
	[[nodiscard]] static int32_t offOwnerTransferTicks() { return offOwnerTransferTicks_; }

private:
	/// Which part of a sample collection runs next.
	///
	/// Separate from SaveUI::SavePhase deliberately: the song save's flow differs enough that one
	/// shared enum would make both harder to read.
	enum class CollectPhase : uint8_t {
		NONE,     ///< No collection in flight.
		TRANSFER, ///< Moving and copying, one entry at a time.
		APPLY,    ///< Writing the decided paths back, in one step.
	};

	void runTransferPhase();
	void applyTransfers();
	/// @brief End the collection having applied NOTHING, and fail the save.
	void abandonCollection();
	/// @brief Defer one more tick, or give up if the collection has stopped making progress.
	bool deferCollection();
	/// @brief Everything the save does after its samples are in place.
	bool finishSaveAfterCollection();

	CollectPhase collectPhase_{CollectPhase::NONE};
	std::vector<PlannedTransfer> transfers_;
	size_t transferCursor_{};
	bool renameIssued_{};
	bool copyIssued_{};
	/// Whether the CURRENT entry's rename succeeded, which decides where its copy reads from.
	bool renameSucceeded_{true};
	int32_t collectStallTicks_{};

	/// Locals the save used to keep on its stack. It no longer finishes inside one call.
	std::string pendingFilePath_;
	bool pendingFileAlreadyExisted_{};
	bool anyErrorMovingTempFiles_{};

	static inline int32_t transferChannelServedOps_{};
	static inline int32_t offOwnerTransferTicks_{};

public:
	bool collectingSamples{};
	[[nodiscard]] bool collectingSamplesForDispatch() const override { return collectingSamples; }
	// ui
	UIType getUIType() override { return UIType::SAVE_SONG; }

protected:
	void onBrowserOpened() override;
	// int32_t arrivedInNewFolder(int32_t direction);
};

extern SaveSongUI saveSongUI;
