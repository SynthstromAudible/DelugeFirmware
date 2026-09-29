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
#include "storage/preset_acquisition.h"

#include <cstdint>
#include <expected>
#include <string>

class FileItem;
class Instrument;
class LoadInstrumentPresetUI;

namespace deluge::gui {

/// @brief The preset scroll: from an instrument's preset to the next one along its folder, either way.
///
/// Reads the folder a window at a time into a listing of its OWN, through the browser's resumable read, and hands
/// each window to decideNavigation. A read that is not ready leaves it in Read; the next tick asks for the same open.
/// The walk's wraps are summed here, across windows, which is what bounds it: a page only moves forward, and a
/// read from the far end is a wrap.
///
/// @note The FileItem* handed back points into this walk's own listing and stays valid until it is armed again.
class PresetNavigation {
public:
	/// A backstop: termination does not depend on it, but a machine that could spin for ever should not rest on
	/// that alone. Hitting it gets out and is counted.
	static constexpr int32_t kMaxNavigationReads = 64;

	/// @brief Start a scroll from @p spec's preset, in @p spec's direction.
	void arm(LoadInstrumentPresetUI& ui, const deluge::storage::AcquireWalkSpec& spec);
	/// @return Whether it is still waiting on a folder open.
	bool tick();
	[[nodiscard]] std::expected<FileItem*, Error> result() const { return result_; }
	/// The folder it read: where the landed preset lives.
	[[nodiscard]] const std::string& dir() const { return dir_; }
	[[nodiscard]] int32_t giveUps() const { return giveUps_; }

private:
	enum class Phase : uint8_t { Idle, Read, Done };

	void finish(std::expected<FileItem*, Error> result);

	Phase phase_{Phase::Idle};
	deluge::gui::browser::FileListing listing_;
	LoadInstrumentPresetUI* ui_{};
	std::string dir_;
	std::string searchName_; ///< with its extension; empty once a read from the far end has cleared it
	int32_t offset_{};
	Instrument* oldInstrument_{};
	OutputType outputType_{OutputType::SYNTH};
	bool skipInSong_{};
	int32_t wraps_{};
	int32_t reads_{};
	int32_t giveUps_{};
	std::expected<FileItem*, Error> result_{std::unexpected{Error::UNSPECIFIED}};
};

} // namespace deluge::gui
