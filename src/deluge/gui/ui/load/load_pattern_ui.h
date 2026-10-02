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
#include "gui/ui/load/load_ui.h"
#include "hid/button.h"
#include "runtime/app_loop.h"
#include "storage/storage_manager.h"

#include <optional>
#include <string>

class LoadPatternUI final : public LoadUI {
public:
	LoadPatternUI() = default;

	bool getGreyoutColsAndRows(uint32_t* cols, uint32_t* rows) override;
	bool opened() override;

	ActionResult buttonAction(deluge::hid::Button b, bool on) override;
	ActionResult padAction(int32_t x, int32_t y, int32_t velocity) override;
	void selectEncoderAction(int8_t offset) override;

	bool renderMainPads(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth] = nullptr,
	                    uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth] = nullptr, bool drawUndefinedArea = true,
	                    int32_t navSys = -1) {
		return true;
	}
	bool renderSidebar(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
	                   uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth]) override {
		return true;
	}

	/// @brief Ask for the selected pattern to be pasted into the clip: loaded, or previewed when `previewOnly` is set.
	///
	/// Returns once the request is made; @ref loadRoutine reads the file over later ticks, through the channels after
	/// boot, and pastes it when it lands. A request made while another is out replaces it.
	/// @return Error::FILE_NOT_FOUND when nothing is selected, otherwise Error::NONE.
	Error performLoad();

	/// @brief Advance the pattern read in flight, if there is one, then ask for the job's next run while it waits. Run
	///        by its own job, so that a load outlives the browser that asked for it: Enter closes the browser at once
	///        and the pattern lands after.
	void loadRoutine();

	/// @brief The job loadRoutine() runs on, woken by DELUGE_WAKE_STORAGE: the "instrument load" job, shared with the
	///        preset browser's loads. It runs when a read is asked for, when a storage channel answers, and at the
	///        staging's next run (StorageManager::stagingNextRunUs()).
	deluge::runtime::JobId jobId{deluge::runtime::kNoJob};

	/// @brief Whether a pattern read is in flight.
	[[nodiscard]] bool loadInFlight() const { return active_.has_value(); }

	/// @brief Whether the channel holds the active read's buffer. For gates: the moment a discard or a newer request
	///        lands on a read the channel cannot cancel.
	[[nodiscard]] bool readIssued() const { return active_.has_value() && staging_.issued; }

	void setupLoadPatternUI(bool overwriteOnLoad = true, bool noScaling = false);

	/// @brief Close the browser. A preview still in flight is discarded; a load from Enter still lands.
	void close() override;

	// ui
	UIType getUIType() override { return UIType::LOAD_PATTERN; }
	bool canDisplayFavourites() override { return true; }

protected:
	void folderContentsReady(int32_t entryDirection) override;
	void currentFileChanged(int32_t movementDirection) override;
	void enterKeyPress() override;
	void onBrowserOpened() override;

private:
	bool selectedDrumOnly{};
	bool previewOnly{};
	bool overwriteExisting{};
	bool noScaling{};
	std::string defaultDir{};
	std::string setupForLoadingPattern();
	Error currentLabelLoadError = Error::NONE;

	/// One pattern read: the file, and what to do with it once it lands.
	struct PatternRead {
		std::string path;
		/// A PLAY preview, dropped if the browser closes first. A load from Enter lands after it closes.
		bool previewOnly;
		/// The paste's settings as they were when it was asked for.
		bool overwriteExisting;
		bool noScaling;
		bool selectedDrumOnly;
	};
	/// The read @ref loadRoutine is advancing.
	std::optional<PatternRead> active_;
	/// How long the read may go without the byte channel finishing anything before it gives up, in µs: 18 s, the same
	/// allowance as the preset loads that share its job.
	static constexpr uint64_t kStagingStallUs = 18'000'000;
	/// The active read's bytes.
	StorageManager::StagingState staging_{.stallBound = deluge::io::StallBound{kStagingStallUs}};

	void requestRead(PatternRead read);
	/// Stop the active read and free its staging.
	void dropRead();
};

extern LoadPatternUI loadPatternUI;
