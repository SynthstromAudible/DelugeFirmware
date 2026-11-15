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

#include "model/fx/stutter_gesture.h"
#include "model/fx/song_stutter.h"
#include "model/fx/stutter_ui.h"
#include "model/song/song.h"
#include "modulation/params/param_manager.h"
#include "modulation/params/param_set.h"
#include "playback/playback_handler.h"

namespace deluge::model {

namespace params = deluge::modulation::params;

void beginStutter(EffectsOwner owner, ParamManagerForTimeline& paramManager) {
	if (!stutter_ui::mayBegin()) {
		return;
	}
	SongStutter& stutter = currentSong->live.stutter;
	const effects::StutterState& settings = owner.effects().stutter.use_song_stutter
	                                            ? currentSong->live.master_channel.binding().effects().stutter
	                                            : owner.effects().stutter;
	UnpatchedParamSet* unpatchedParams = paramManager.getUnpatchedParamSet();
	const int32_t paramValue = unpatchedParams->getValue(params::UNPATCHED_STUTTER_RATE);
	const int32_t knobDiffBefore = stutter.lastQuantizedKnobDiff;
	if (settings.quantized) {
		const int32_t basePos = unpatchedParams->paramValueToKnobPos(paramValue, nullptr);
		int32_t knobPos = basePos;
		if (knobPos < -39) {
			knobPos = -16; // 4ths
		}
		else if (knobPos < -14) {
			knobPos = -8; // 8ths
		}
		else if (knobPos < 14) {
			knobPos = 0; // 16ths
		}
		else if (knobPos < 39) {
			knobPos = 8; // 32nds
		}
		else {
			knobPos = 16; // 64ths
		}
		// The param stays where it is, so automation playing it back sets the absolute value it recorded: the note
		// length is the snap moved by however far the param has moved from where it began, which is the param plus
		// the snap's offset from it.
		stutter.valueBeforeStuttering = paramValue;
		stutter.lastQuantizedKnobDiff = knobPos - basePos;
	}

	const auto rate = static_cast<uint32_t>(stutterRate(&paramManager, stutter.lastQuantizedKnobDiff,
	                                                    currentSong->getInputTickMagnitude(),
	                                                    playbackHandler.getTimePerInternalTickInverse()));
	if (stutter.begin(*owner.core, settings, rate) != Error::NONE) {
		// No stutter: the snap memory goes back to how it was.
		if (settings.quantized) {
			stutter.lastQuantizedKnobDiff = knobDiffBefore;
		}
		return;
	}
	stutter_ui::began(&paramManager);
}

void endStutter(ParamManagerForTimeline* paramManager) {
	// A song being read (none is current yet) has no stutter to end.
	if (currentSong != nullptr) {
		SongStutter& stutter = currentSong->live.stutter;
		stutter.end();
		if (paramManager != nullptr) {
			UnpatchedParamSet* unpatchedParams = paramManager->getUnpatchedParamSet();
			if (stutter.playing.quantized) {
				// Set back the value it had just before stuttering so orange LEDs are redrawn.
				unpatchedParams->params[params::UNPATCHED_STUTTER_RATE].setPlayed(stutter.valueBeforeStuttering);
			}
			else {
				// Regular Stutter FX (if below middle value, reset it back to middle). Not an Edit: like the quantized
				// restore above, it puts the knob back where the gesture expects it, and records nothing
				if (unpatchedParams->getValue(params::UNPATCHED_STUTTER_RATE) < 0) {
					unpatchedParams->params[params::UNPATCHED_STUTTER_RATE].setPlayed(0);
				}
			}
		}
		stutter.clearSnap();
	}
	stutter_ui::ended(paramManager);
}

void endStutterIfStuttering(const effects::ChainCore& core) {
	if (currentSong != nullptr && currentSong->live.stutter.isStuttering(core)) {
		currentSong->live.stutter.end();
		currentSong->live.stutter.clearSnap();
		stutter_ui::ended(nullptr);
	}
}

} // namespace deluge::model
