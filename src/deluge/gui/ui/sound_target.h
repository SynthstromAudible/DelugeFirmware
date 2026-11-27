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

#include "model/lane_owner.h"
#include "processing/ranges_retype.h"

#include <cstdint>

class ArpeggiatorSettings;
class AudioFileHolder;
class ModelStackWithThreeMainThings;
class MultiRange;
class ParamManagerForTimeline;
class Sound;
class SourceResources;

namespace deluge::synth {
enum class OscType : uint8_t;
} // namespace deluge::synth

namespace deluge::gui {

/// @brief The sound a sample claim, a folder import or a slice acts on, taken when it is asked for and carried with
///        it: the sound, its source and range, and the clip's (or the kit row's) params and arp.
struct SoundTarget {
	Sound* sound = nullptr;
	deluge::model::LaneOwner mod;
	ParamManagerForTimeline* params = nullptr;
	ArpeggiatorSettings* arp = nullptr;
	::MultiRange* range = nullptr;
	int16_t rangeIndex = -1;
	int8_t sourceIndex = 0;
	SourceResources* source = nullptr;
};

/// @brief Point @p target's range at the one its index names after @p retype, by `SourceResources::setOscType`'s rule
/// for the
///        legacy editor's range: nothing moves unless the ranges were reallocated, and a reallocation that had to
///        clear them starts again from range 0. An index past the end leaves the range as it is.
void retargetRangeAfterRetype(SoundTarget& target, RangesRetype retype);

/// @brief Change @p target's source to @p type through its setting Edit, watching the change, and find @p target's
///        range again by `retargetRangeAfterRetype`'s rule. What a claim and an import change a source's type with.
void setOscTypeFor(SoundTarget& target, deluge::synth::OscType type);

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
