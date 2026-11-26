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

#include "model/lane_owner.h"

#include "model/drum/non_audio_drum.h"
#include "model/fx/effects_owner.h"
#include "model/fx/master_channel.h"
#include "model/instrument/kit.h"
#include "model/instrument/non_audio_instrument.h"
#include "processing/audio_output.h"
#include "processing/sound/sound_drum.h"
#include "processing/sound/sound_instrument.h"

#include <type_traits>

namespace deluge::model {

LaneOwner owner_of(::Output& output) {
	switch (output.type) {
	case OutputType::SYNTH:
		return static_cast<::Sound*>(static_cast<::SoundInstrument*>(&output));
	case OutputType::KIT:
		return static_cast<::Kit*>(&output);
	case OutputType::AUDIO:
		return static_cast<::AudioOutput*>(&output);
	case OutputType::MIDI_OUT:
	case OutputType::CV:
		return static_cast<::NonAudioInstrument*>(&output);
	case OutputType::NONE:
		break;
	}
	return std::monostate{};
}

LaneOwner owner_of(::Drum& drum) {
	if (drum.type == DrumType::SOUND) {
		return static_cast<::Sound*>(static_cast<::SoundDrum*>(&drum));
	}
	return static_cast<::NonAudioDrum*>(&drum);
}

std::optional<EffectsOwner> effects_owner_of(const LaneOwner& owner) {
	return std::visit(
	    []<typename T>(T held) -> std::optional<EffectsOwner> {
		    if constexpr (std::is_same_v<T, ::Sound*> || std::is_same_v<T, ::Kit*> || std::is_same_v<T, ::AudioOutput*>
		                  || std::is_same_v<T, MasterChannel*>) {
			    return held->effectsOwner();
		    }
		    else {
			    return std::nullopt;
		    }
	    },
	    owner);
}

} // namespace deluge::model
