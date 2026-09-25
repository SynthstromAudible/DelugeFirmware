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
#include "gui/ui/load/preset_walk.h"
#include "storage/preset_acquisition.h"

#include <cstdint>
#include <expected>
#include <string>

class FileItem;
class LoadInstrumentPresetUI;

namespace deluge::gui {

/// @brief The SYNTH/KIT change-type: the preset a clip last had of the new type, or the next one that may be used, or
///        any at all.
///
/// Reads one folder a window at a time into a listing of its OWN, through the browser's resumable read, and hands
/// each window to decideConfirm. "Any at all" is the whole-type Unlaunched search, run by an embedded PresetWalk --
/// the search the old walk ran for a missing folder and then threw away.
///
/// @note The FileItem* handed back points into this machine's listing or its PresetWalk's, whichever landed, and stays
///       valid until it is armed again.
class ConfirmPreset {
public:
	/// A backstop: termination does not depend on it. Hitting it ends with an error and is counted.
	static constexpr int32_t kMaxConfirmReads = 64;

	void arm(LoadInstrumentPresetUI& ui, const deluge::storage::AcquireWalkSpec& spec);
	/// @return Whether it is still waiting on a folder open.
	bool tick();
	[[nodiscard]] std::expected<FileItem*, Error> result() const { return result_; }
	/// The folder the landed preset lives in.
	[[nodiscard]] const std::string& dir() const { return walking_ ? walk_.foundDir() : dir_; }
	[[nodiscard]] int32_t giveUps() const { return giveUps_; }

private:
	enum class Phase : uint8_t { Idle, Read, Walk, Done };

	void finish(std::expected<FileItem*, Error> result);
	void startAnyPreset();

	Phase phase_{Phase::Idle};
	deluge::gui::browser::FileListing listing_;
	PresetWalk walk_{};
	bool walking_{};
	LoadInstrumentPresetUI* ui_{};
	std::string dir_;
	std::string searchName_; ///< empty once a FromStart has cleared it
	OutputType outputType_{OutputType::SYNTH};
	Availability availability_{Availability::ANY};
	bool grabbingLeftmost_{};
	int32_t reads_{};
	int32_t giveUps_{};
	std::expected<FileItem*, Error> result_{std::unexpected{Error::UNSPECIFIED}};
};

} // namespace deluge::gui
