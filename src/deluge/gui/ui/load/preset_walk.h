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

#include "definitions_cxx.hpp"
#include "gui/ui/browser/file_listing.h"

#include <array>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

class FileItem;
class LoadInstrumentPresetUI;
class Song;

namespace deluge::gui {

/// @brief Where a preset search has got to.
///
/// One phase per `goto` in the recursive walk this replaces, so the two can be read side by side.
enum class WalkPhase : uint8_t {
	/// Nothing is being searched for.
	Idle,
	/// A folder read is outstanding. The ONLY phase that touches the card.
	ReadFolder,
	/// Looking through what the read produced for a usable preset.
	ScanFiles,
	/// Looking through what the read produced for a subfolder to go down into.
	ScanFolders,
	/// Pushing a frame for the subfolder just chosen.
	Descend,
	/// Terminal: a preset was found.
	Done,
	/// Terminal: no preset, or a read failed.
	Failed,
};

/// @brief A depth-first preset search that can stop between folders.
///
/// @ref CardReinsert contributed the shape: an `arm()`/`tick()` pair whose tick advances by at most
/// one card operation and returns, so the work happens on whatever tier its caller stands on.
/// @ref Browser's `ListingPhase` machine contributed the suspension: a read that cannot finish now
/// returns `Error::LISTING_DEFERRED`, and the phase does not advance, so re-entering re-asks for the
/// same directory and no open is ever made twice.
///
/// What this replaces is `LoadInstrumentPresetUI::findAnUnlaunchedPresetIncludingWithinSubfolders`,
/// which recurses into subfolders with backtracking. A recursion cannot be split across a deferral,
/// which is why that walk had to run start-to-finish on the storage owner and why it is one of the
/// last things keeping the fiber alive. Here the recursion is an explicit frame stack, and the read
/// that each frame makes is the browser's own resumable one.
///
/// @warning **A failed descent resumes the parent through the PAGING path**, not by advancing a
///          folder index. The original pops `currentDir` back to the parent and re-reads it from the
///          failed subfolder's name (`goto goAgain`), because `readdir_next` MOVES each name out of
///          the directory snapshot -- a folder cannot be enumerated twice from one handle, and the
///          chunk the parent was looking at may have been culled away while the descent ran. So
///          @ref Frame carries a search name, never an index, and paging and return-from-recursion
///          are ONE mechanism rather than two.
///
/// @note The `FileItem*` handed back on success points into this walk's OWN listing, not the
///       browser's, and stays valid until the walk is armed again. The recursive walk's pointer
///       aimed into the shared list, where any listing could invalidate it.
class PresetWalk {
public:
	/// How deep the search will go. Measured 2026-09-19: depth 1 in every gate, and the reference
	/// card's SYNTHS folder is flat, so this is generous. A fixed bound rather than a growing stack
	/// because a card walk must not allocate, and because the recursion it replaces had no bound at
	/// all -- a deeply nested card overflowed the real stack instead.
	static constexpr int32_t kMaxDepth = 8;

	/// @brief Begin a search rooted at the browser's current directory.
	///
	/// @param ui The browser whose resumable folder read the walk drives. Its file list is NOT
	///        touched: the walk fills its own.
	/// @param startDir Where to search from. A PARAMETER, not `Browser::currentDir`: a caller that
	///        had to park the directory in the global first would be writing browser state to
	///        communicate with this class, and that write is exactly what broke the save browser --
	///        an acquisition for "SYNTHS" moved a save browser sitting in "SONGS".
	/// @param song Searched for already-loaded Instruments too; null reads the folder alone.
	/// @param outputType Which kind of preset is wanted.
	/// @param availabilityRequirement Which presets may be landed on.
	void arm(LoadInstrumentPresetUI& ui, std::string_view startDir, Song* song, OutputType outputType,
	         Availability availabilityRequirement);

	/// @brief Advance by at most one folder read. Returns true while there is still work.
	bool tick();

	[[nodiscard]] bool running() const {
		return phase_ != WalkPhase::Idle && phase_ != WalkPhase::Done && phase_ != WalkPhase::Failed;
	}

	/// @brief The folder the search ENDED in -- where the preset it returned actually lives.
	///
	/// Read by the caller instead of `Browser::currentDir`: the walk no longer moves the browser, so
	/// the browser's directory is no longer an answer to this question.
	[[nodiscard]] const std::string& foundDir() const { return listing_.dir(); }

	/// @brief What the search found. Only meaningful once @ref running is false.
	[[nodiscard]] std::expected<FileItem*, Error> result() const { return result_; }

	/// @name Lenses for the `preset-load` gate's PL-WALK legs.
	/// @{
	/// The deepest the stack got. 1 means the walk never descended -- the failure a flat card cannot
	/// see, and the one that would make a user's nested presets silently vanish.
	[[nodiscard]] int32_t maxDepthReached() const { return maxDepthReached_; }
	/// Reads issued from a search name rather than from the start of a folder.
	[[nodiscard]] int32_t pagedReads() const { return pagedReads_; }
	/// Subfolders the walk declined to enter because @ref kMaxDepth was reached. Never silent: a
	/// give-up is counted here and the search carries on past it, so presets above the bound are
	/// still found.
	[[nodiscard]] int32_t depthGiveUps() const { return depthGiveUps_; }
	/// @}

private:
	/// @brief One folder of the search.
	///
	/// @warning `searchName` is a paging cursor, NOT an index into a chunk -- see the class warning.
	struct Frame {
		/// The folder this level is searching.
		std::string dir;
		/// Where the next read of THIS folder resumes.
		std::string searchName;
		/// Which of the two passes this level is on: files first, then subfolders.
		bool doingSubfolders{};
		/// Whether this level still has somewhere to look after the child it descended into comes
		/// back empty. The original computed it before recursing, for the same reason.
		bool anyMoreForLater{};
	};

	[[nodiscard]] Frame& top() { return frames_[depth_ - 1]; }

	/// Perform the pending read; false means it deferred and the phase has not advanced.
	bool stepReadFolder();
	void stepScanFiles();
	void stepScanFolders();
	void stepDescend();

	/// Re-read this level from @p searchName -- the original's `goto goAgain`.
	void pageFrom(const std::string& searchName);
	/// Restart this level looking for subfolders -- the original's `goto startDoingFolders`.
	void startDoingFolders();
	/// The child just descended into found nothing. The stack top is the PARENT.
	void childYieldedNothing();
	/// This level found nothing. Pop it, and tell whoever is above.
	void frameExhausted();
	void succeed(FileItem* found);
	void fail(Error error);

	WalkPhase phase_{WalkPhase::Idle};

	/// @brief This walk's own folder listing.
	///
	/// The whole point of the type. The walk reads folders without ever replacing what the browser
	/// is showing, so it can stop between folders and nothing else can observe a half-walked list --
	/// which is exactly what went wrong when it borrowed the browser's.
	deluge::gui::browser::FileListing listing_;

	LoadInstrumentPresetUI* ui_{};
	Song* song_{};
	OutputType outputType_{};
	Availability availabilityRequirement_{Availability::ANY};

	std::array<Frame, kMaxDepth> frames_;
	int32_t depth_{};

	/// The rightmost name the last read produced, BEFORE filtering removed anything. Paging resumes
	/// from it, so it has to be taken before `deleteFolderAndDuplicateItems` runs. Lives across the
	/// scan only -- no read can happen in between -- so one is enough for the whole stack.
	std::string lastNameBeforeFiltering_;
	/// Which entry `ScanFolders` chose, handed to `Descend`.
	int32_t descendIndex_{};

	std::expected<FileItem*, Error> result_{std::unexpected{Error::NO_FURTHER_FILES_THIS_DIRECTION}};
	int32_t maxDepthReached_{};
	int32_t pagedReads_{};
	int32_t depthGiveUps_{};
};

} // namespace deluge::gui
