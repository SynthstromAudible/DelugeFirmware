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
#include "model/sample/sample_controls.h"
#include "storage/audio/sample_load_walk.h"
#include "storage/multi_range/multi_range_array.h"
#include "util/phase_increment_fine_tuner.h"

class Sound;
class ParamManagerForTimeline;
class WaveTable;
class SampleHolder;
class DxPatch;

#include "engines/deluge/sound_state.h"

class Source {
public:
	Source();
	~Source();

	// M3 owner-5 slice G: non-owning pointer into the owning Sound's SoundState (state_->sources[s]). A Source is
	// never copied by value (it lives as a fixed member of its Sound), so this pointer has no clone hazard; the Sound
	// binds it at bindState and re-seeds it at doneReadingFromFile. Save reads the record via the Sound's state_.
	deluge::instrument::SourceState* state_{nullptr};
	void bindState(deluge::instrument::SourceState* d);
	void seedDocSourceScalars();
	/// The scalars seedDocSourceScalars copies, into @p d.
	void seedScalarsInto(deluge::instrument::SourceState& d) const;
	// The DX7 patch is mutated in-place (per-param edits, engine mode, cartridge load) rather than through one setter,
	// so like modKnobs it re-seeds the whole optional<DxPatchState> record after any mutation (bind,
	// doneReadingFromFile, the dx/ menus, setOscType→ensureDxPatch). Copies the 156 params + engineMode +
	// random_detune, or clears it when the engine patch is absent (non-DX7 osc).
	void seedDocDxPatch();
	/// The DX7 patch seedDocDxPatch copies, into @p d.
	void seedDxPatchInto(deluge::instrument::SourceState& d) const;
	// ranges (owner-5, no undo): whole-list reseed of the RangeState list after any range mutation (zone edit,
	// add/remove, sample-set, reverse). The engine sampleHolder/ranges stay the working structure; this mirrors them
	// into the record.
	void seedDocRanges();
	/// The ranges seedDocRanges copies, into @p d.
	void seedRangesInto(deluge::instrument::SourceState& d) const;

	SampleControls sampleControls;

	OscType oscType;

	// These are not valid for Samples
	int16_t transpose;
	int8_t cents;
	PhaseIncrementFineTuner fineTuner;

	MultiRangeArray ranges;

	DxPatch* dxPatch;
	bool dxPatchChanged = false;
	SampleRepeatMode repeatMode;

	int8_t timeStretchAmount;

	int16_t defaultRangeI; // -1 means none yet

	/// Whether this source renders in stereo. Static + fully parameterised (no `Sound`/`Source` reach) so the
	/// voice kernel can call it straight off its per-block config. Still reads `AudioEngine` stereo/input globals.
	static bool renderInStereo(OscType oscType, uint8_t unisonStereoSpread, uint8_t numUnison,
	                           SampleHolder* sampleHolder);
	void setCents(int32_t newCents);
	// M3 owner-5 slice G: setters that write the SourceState record (when bound) alongside the native cache. Every menu
	// / runtime write site funnels through these; save reads the record.
	void setTranspose(int16_t newTranspose);
	void setRepeatMode(SampleRepeatMode newMode);
	void setTimeStretchAmount(int8_t newAmount);
	void setInterpolationMode(InterpolationMode newMode);
	void setPitchAndSpeedIndependent(bool independent);
	void recalculateFineTuner();
	int32_t getLengthInSamplesAtSystemSampleRate(int32_t note, bool forTimeStretching = false);
	void detachAllAudioFiles();
	/// @brief Report each range's file to @p visitor, with a checkpoint before each. Loads nothing.
	/// @param visitor Receives a demand for each range's audio file and a checkpoint before each one.
	/// @return Walk::Stop if a checkpoint stopped the walk.
	deluge::storage::Walk walkSampleDemands(deluge::storage::SampleLoadVisitor& visitor);
	void setReversed(bool newReversed);
	int32_t getRangeIndex(int32_t note);
	MultiRange* getRange(int32_t note);
	MultiRange* getOrCreateFirstRange();
	bool hasAtLeastOneAudioFileLoaded();
	void doneReadingFromFile(Sound* sound);
	bool hasAnyLoopEndPoint();
	OscType getOscType();
	void setOscType(OscType newType);

	DxPatch* ensureDxPatch();

private:
};
