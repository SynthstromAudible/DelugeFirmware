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

class ArpeggiatorSettings;
class AudioFileHolder;
class ModControllableAudio;
class ModelStackWithThreeMainThings;
class MultiRange;
class ParamManagerForTimeline;
class SampleControls;
class Sound;
class Source;

namespace deluge::gui {

/// @brief The sound a sample claim, a folder import or a slice acts on, taken when it is asked for and carried with
///        it: the sound, its source and range, and the clip's (or the kit row's) params and arp.
struct SoundTarget {
	Sound* sound = nullptr;
	ModControllableAudio* mod = nullptr;
	ParamManagerForTimeline* params = nullptr;
	ArpeggiatorSettings* arp = nullptr;
	::MultiRange* range = nullptr;
	int16_t rangeIndex = -1;
	int8_t sourceIndex = 0;
	Source* source = nullptr;
	SampleControls* sampleControls = nullptr;
	/// The kit's own effects rather than a row's.
	bool kitGlobalFx = false;
};

/// @brief Point @p target's range at the one its index names, after an oscillator-type change reallocated the source's
///        ranges. A change that had to empty the list (it holds fewer ranges than @p rangesBefore) starts again from
///        range 0, as `Source::setOscType` does.
void retargetRangeAfterRetype(SoundTarget& target, int32_t rangesBefore);

/// @brief The model stack @p target edits: the song's off a clip minder, the kit's own with affect-entire on, else the
///        current clip's with the kit's selected row.
ModelStackWithThreeMainThings* modelStackFor(const SoundTarget& target, void* memory);

/// @brief The holder a claim loads into: the audio clip's, or @p target's range's.
AudioFileHolder* audioFileHolderFor(const SoundTarget& target);

/// @brief Silence what a claim is about to replace: the audio clip's voice sample, or @p target's sound's voices.
void cutSound(const SoundTarget& target);

/// @brief A file is now set on @p target's source: its oscillator volume goes back to full if it is not automated.
void resetSilentOscVolume(const SoundTarget& target);

} // namespace deluge::gui
