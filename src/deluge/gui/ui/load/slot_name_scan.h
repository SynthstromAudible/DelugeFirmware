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

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

class LoadInstrumentPresetUI;

namespace deluge::gui {

enum class ScanPhase : uint8_t { Idle, Read, Done, Failed };

/// @brief The name a new blank instrument takes: the slot after the highest-numbered preset of its type.
///
/// One folder read, made through the browser's resumable readFolder into a listing of its OWN, so the scan never
/// moves the browser's folder or touches its list -- which the storage-owner walk it replaces did both of. A read
/// that is not ready leaves the scan in Read, and the next tick asks for the same open again.
///
/// The read keeps song instruments in the listing (includeSongInstruments), as the walk it replaces did: a numbered
/// synth that exists only in memory still takes its slot.
class SlotNameScan {
public:
	/// @brief Start a scan of @p dir for @p outputType's next slot name.
	void arm(LoadInstrumentPresetUI& ui, std::string_view dir, OutputType outputType);

	/// @brief Advance the scan.
	/// @return Whether it is still waiting on its folder open.
	bool tick();

	[[nodiscard]] bool running() const { return phase_ == ScanPhase::Read; }

	/// @brief The slot name, or the error the read ended with. Meaningful once @ref running is false.
	[[nodiscard]] std::expected<std::string, Error> result() const { return result_; }

private:
	ScanPhase phase_{ScanPhase::Idle};
	deluge::gui::browser::FileListing listing_;
	LoadInstrumentPresetUI* ui_{};
	std::string dir_;
	OutputType outputType_{OutputType::SYNTH};
	std::expected<std::string, Error> result_{std::unexpected{Error::UNSPECIFIED}};
};

} // namespace deluge::gui
