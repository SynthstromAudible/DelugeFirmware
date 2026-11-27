/*
 * Copyright © 2014-2023 Synthstrom Audible Limited
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
#include "storage/audio/sample_load_walk.h"
#include "storage/multi_range/multi_range_array.h"
#include "synth/sources/input/live/live_inputs.h"

#include "instruments/classic/sound_state.h"

class ParamManagerForTimeline;
class WaveTable;
class SampleHolder;

#include <optional>
#include <vector>

class SourceResources {
public:
	SourceResources();
	~SourceResources();

	/// @brief The ranges it holds: each one's top note and file, and while they are sample ranges (whatever the current
	/// type) their zones and tuning, which a type change and back keeps.
	[[nodiscard]] std::vector<deluge::synth::SourceRange> heldRanges() const;

	/// @brief Whether the note last set up on this sample source plays against the record's direction (the
	/// arpeggiator's reverse step); cleared when a voice of the sound releases.
	bool invertReversed = false;

	/// @brief The oscillator's ranges: each one's file, zone and cluster reservations, as it plays them.
	MultiRangeArray ranges;

	/// @brief The live histories this oscillator's voices read while its type is an input type; empty otherwise.
	deluge::synth::LiveInputStore liveInputs;

	int16_t defaultRangeI{-1}; ///< -1 means none yet

	/// Whether this source renders in stereo. Static + fully parameterised (no `Sound` reach) so the
	/// voice kernel can call it straight off its per-block config. Still reads `AudioEngine` stereo/input globals.
	static bool renderInStereo(OscType oscType, uint8_t unisonStereoSpread, uint8_t numUnison,
	                           SampleHolder* sampleHolder);
	int32_t getLengthInSamplesAtSystemSampleRate(int32_t note, bool forTimeStretching = false);
	void detachAllAudioFiles();
	/// @brief Report each range's file to @p visitor, with a checkpoint before each. Loads nothing.
	/// @param visitor  Receives a demand for each range's audio file and a checkpoint before each one.
	/// @param reversed The direction the source plays, from its sound's config: a walk can reach a sound whose record
	///                 has gone (a kit read into a song it was never added to, walked after that song), so it reads no
	///                 record.
	/// @return Walk::Stop if a checkpoint stopped the walk.
	deluge::storage::Walk walkSampleDemands(deluge::storage::SampleLoadVisitor& visitor, bool reversed);
	/// @brief Play the ranges in direction @p reversed: a reversed range's end is clamped to its sample's length, and
	/// every range's clusters are claimed for the direction. Writes no record.
	void followDirection(bool reversed);
	MultiRange* getRange(int32_t note);
	int32_t getRangeIndex(int32_t note);
	MultiRange* getOrCreateFirstRange();
	bool hasAtLeastOneAudioFileLoaded();
	bool hasAnyLoopEndPoint();
	void setOscType(OscType newType);

	/// @brief Makes or frees the live histories @p type plays from, handing them to the render inside an
	/// audio-excluded section. [task]
	void reconcileLiveInputs(OscType type);
};
