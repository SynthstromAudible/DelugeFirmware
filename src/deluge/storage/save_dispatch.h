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

#include <cstdint>
#include <string>

namespace deluge::storage {

/// @brief Which caller's follow-up a completed save runs.
///
/// One caller, one value, one completion. Two of these callers consume the save's bool to
/// decide whether their context menu closes, and they do not want the same tail.
///
/// @warning Switch on this enum exhaustively, with no `default:` label. `-Werror=switch` then
///          catches an unhandled enumerator, which is what stops a new caller silently inheriting
///          another one's tail.
enum class SaveConsumer : uint8_t {
	EnterKey,                  ///< SaveUI::enterKeyPress
	OverwriteAccept,           ///< OverwriteFile::acceptCurrentOption (consumes the bool)
	OverwritePad,              ///< OverwriteFile::padAction
	SaveSongOrInstrumentAccept ///< SaveSongOrInstrument::acceptCurrentOption (consumes the bool)
};

/// @brief Everything a dispatched save needs, captured at dispatch time.
///
/// @note This is the snapshot. Once dispatched, the op reads ONLY these fields for anything crossing
///       its yield points -- never live Browser or UI state, which the UI task can mutate while the
///       op is yielded. The snapshot rule covers LIFETIME, not CURRENCY: a completion that MUTATES
///       state needs a currency check as well as a liveness one.
struct SaveRequest {
	SaveConsumer consumer{SaveConsumer::EnterKey};
	bool mayOverwrite{};
	/// @brief `collectingSamples` at dispatch. SaveSongOrInstrument sets it immediately before
	///        dispatching, so it is live state and must not be read again on the owner.
	bool collectingSamples{};
	std::string filePath;    ///< getCurrentFilePath() at dispatch.
	std::string enteredText; ///< enteredText at dispatch.
	std::string dirPath;     ///< currentDir at dispatch.
};

} // namespace deluge::storage
