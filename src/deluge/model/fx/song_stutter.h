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

#include "definitions_cxx.hpp" // Error
#include "dsp-next/core/stereo_sample.h"
#include "effects/state.h"
#include "effects/stutter.h"
#include "modulation/params/param.h"

#include <cstdint>
#include <span>

class ModControllableAudio;
class ParamManager;

namespace deluge::model {

/// @brief The song's stutter: one at a time, on the owner the gold knob's press began it on (a kit, an audio track or
/// the master), over a tape the song holds.
///
/// It is the only place a stutter starts or ends. A begin and an end hand the tape to the render and back around an
/// AudioExcludedSection, so the render never sees a tape half set up or freed, and a begin on a second owner ends the
/// first's in the same section. The owner keeps its own work: the UI mode, the quantized snap of the stutter-rate param
/// (whose memory lives here, beside the stutter it belongs to), and the rate each block.
class SongStutter {
public:
	/// @brief No stutter, with tapes from the app's low-speed heap.
	SongStutter();

	/// @brief [task] Begins @p owner's stutter at @p rate (8.24 on the delay's rate scale), playing as @p settings
	/// say, ending another owner's in the same section.
	/// @param owner    The owner whose chain renders the stutter.
	/// @param settings How the stutter plays.
	/// @param rate     Initial rate, 8.24 on the delay's rate scale.
	/// @return INSUFFICIENT_RAM, and nothing changed, when the tape cannot be allocated.
	[[nodiscard]] Error begin(ModControllableAudio& owner, const deluge::effects::StutterState& settings,
	                          uint32_t rate);
	/// @brief [task] Ends the stutter, if any, and frees its tape.
	void end();
	/// @brief [render] The stutter, while @p owner is the one stuttering; else null.
	/// @param owner The owner asking.
	/// @return The song's stutter, or null.
	[[nodiscard]] deluge::effects::Stutter* stutterFor(const ModControllableAudio& owner) {
		return owner_ == &owner ? &fx_ : nullptr;
	}
	/// @brief Whether @p owner is the one stuttering.
	/// @param owner The owner asking.
	/// @return True if @p owner owns the stutter.
	[[nodiscard]] bool isStuttering(const ModControllableAudio& owner) const { return owner_ == &owner; }
	/// @brief Whether any owner is stuttering.
	/// @return True if a stutter is active.
	[[nodiscard]] bool active() const { return owner_ != nullptr; }
	/// @brief [task] Forgets the quantized snap, as every end of a stutter leaves it.
	void clearSnap() {
		lastQuantizedKnobDiff = 0;
		valueBeforeStuttering = 0;
	}

	/// The stutter-rate param's value (signed Q31) before a quantized stutter snapped it, restored when it ends.
	int32_t valueBeforeStuttering = 0;
	/// The note length a quantized stutter snapped to, in knob positions (-64 to 64): added to the param's position
	/// each block. Zero for a free stutter.
	int32_t lastQuantizedKnobDiff = 0;
	/// The settings of the stutter playing, taken when it began.
	deluge::effects::StutterState playing{};

private:
	deluge::effects::Stutter fx_;
	/// The owner stuttering, compared and never dereferenced; set and cleared with the render excluded.
	const ModControllableAudio* owner_ = nullptr;
};

/// @brief What an owner's chain renders of the song's stutter this block.
struct StutterInput {
	deluge::effects::Stutter* stutter =
	    nullptr;       ///< The song's stutter while the owner is the one stuttering, else null.
	uint32_t rate = 0; ///< Its rate, 8.24 on the delay's rate scale.
};

/// @brief [render] The current song's stutter for @p owner's chain, at the rate @p paramManager's stutter param gives:
/// none with no song current (a sample preview between songs) or while another owner stutters.
/// @param owner        The owner whose chain is rendering.
/// @param paramManager The owner's param manager, which holds its stutter-rate param.
/// @return The stutter and its rate; a null stutter when @p owner is not stuttering.
[[nodiscard]] StutterInput stutterInput(const ModControllableAudio& owner, ParamManager* paramManager);

/// @brief Whether the stutter-rate knob shows its middle position: a free stutter's rate has a centre, a quantized
/// stutter's note lengths don't, except while stuttering.
/// @param owner      The stutter's owner (a sound or a global effectable), which may follow the song's stutter
/// settings.
/// @param stuttering Whether a stutter is playing now.
/// @return True when the middle position is marked.
[[nodiscard]] bool stutterRateIndicatesMiddle(const ModControllableAudio& owner, bool stuttering);

/// @brief Sets @p paramManager's stutter-rate param to its default, as a new owner's params start.
/// @param paramManager The param manager to initialise.
/// @param defaults     Whose table's default: UNPATCHED_SOUND for a sound, UNPATCHED_GLOBAL for a global effectable.
void initStutterParams(ParamManager* paramManager, deluge::modulation::params::Kind defaults);

/// @brief The stutter's rate, 8.24 on the delay's rate scale: the stutter-rate param in @p paramManager, moved by
/// @p knobDiff knob positions (a quantized stutter's note length), synced to the tempo by @p timePerTickInverse at
/// the song's input tick @p magnitude.
/// @param paramManager       The owner's param manager.
/// @param knobDiff           Offset in knob positions added to the param's position (the sum is clamped to
///                           -64..64); zero for a free stutter.
/// @param magnitude          The song's input tick magnitude.
/// @param timePerTickInverse Tempo scale from PlaybackHandler::getTimePerInternalTickInverse().
/// @return The rate, 8.24 on the delay's rate scale.
[[nodiscard]] int32_t stutterRate(ParamManager* paramManager, int32_t knobDiff, int32_t magnitude,
                                  uint32_t timePerTickInverse);

} // namespace deluge::model
