/*
 * Copyright © 2015-2023 Synthstrom Audible Limited
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
#include "gui/ui/browser/browser.h"
#include "hid/button.h"

class DxSyxBrowser final : public Browser {
public:
	DxSyxBrowser();
	bool opened() override;
	void enterKeyPress() override;
	std::string getCurrentFilePath() override;

	/// @brief Collect a cartridge read that is in flight on the file prefix channel.
	///
	/// Overrides the subclass hook, never `graphicsRoutine()` — which is `final` on `Browser` so that
	/// the listing pump cannot be skipped. Inert while no load is in flight: the inline arm, on the
	/// storage owner or before it starts, reads and parses in enterKeyPress() itself.
	void browserGraphicsRoutine() override;

	/// @brief Cancel an in-flight cartridge read as the browser goes away.
	///
	/// The browser stays up while the read runs, so the read can outlive it. See the
	/// definition for what a stale in-flight flag does.
	void close() override;

	/// @brief Whether a cartridge load is waiting on the file prefix channel.
	///
	/// For the song-preview A/B harness, which cannot reach the flag: it lives in `dx_browser.cpp`'s anonymous
	/// namespace.
	static bool cartridgeLoadInFlight();

	/// @brief How many cartridge loads gave up because the channel left their read unanswered for 600 ticks.
	///
	/// For the song-preview gate, like @ref cartridgeLoadInFlight.
	static int32_t cartridgeGiveUps();

	// ui
	UIType getUIType() override { return UIType::DX_BROWSER; }
};

extern DxSyxBrowser dxBrowser;
