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

#include "instruments/classic/source_constants.h"

#include <optional>

class DxPatch;

/// @brief The DX7 voice the app holds for a sound's DX7 engine: made by the DX7 engine the first time it is needed,
/// freed with its holder.
class HeldDxPatch {
public:
	HeldDxPatch() = default;
	~HeldDxPatch();
	HeldDxPatch(const HeldDxPatch&) = delete;
	HeldDxPatch& operator=(const HeldDxPatch&) = delete;
	HeldDxPatch(HeldDxPatch&&) = delete;
	HeldDxPatch& operator=(HeldDxPatch&&) = delete;

	/// @brief The voice, made on first use.
	/// @return Null when the DX7 engine cannot be made (no RAM).
	[[nodiscard]] DxPatch* ensure();

	/// @brief The voice, or null while none has been needed.
	[[nodiscard]] DxPatch* get() const noexcept { return patch_; }

	/// @brief The voice as the record holds a patch; none while there is no voice.
	[[nodiscard]] std::optional<deluge::instruments::classic::DxPatchState> state() const;

	/// @brief The voice made to play @p from: its bytes, engine mode and random detune.
	/// @return False when the voice cannot be made.
	[[nodiscard]] bool assign(const deluge::instruments::classic::DxPatchState& from);

	/// The voice changed since the last block: its voices renew their copy of its bytes.
	bool changed{false};

private:
	DxPatch* patch_{nullptr};
};
