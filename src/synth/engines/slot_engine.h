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

#include "synth/engines/engine.h"

namespace deluge::synth {

/// @brief An engine over the Deluge voice's source slots: the per-source note-on and phase increments every engine
/// here shares. Each source is marked active in every part where it can sound, gets its phase increments, and its
/// slot starts.
class SlotEngine : public Engine {
public:
	bool note_on(const EngineNote& note) override;
	void calculate_phase_increments(const EngineNote& note) override;

protected:
	/// Marks each source active in every part where it can sound. @return false (with @p refuse_silent) when a source
	/// cannot sound: ring-mod needs both.
	static bool mark_active(const EngineNote& note, bool refuse_silent);
	/// Starts each active slot for the note. @return false when one cannot start.
	static bool start_sources(const EngineNote& note);
};

} // namespace deluge::synth
