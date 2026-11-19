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

class ParamManagerForTimeline;
class WaveTable;
class SampleHolder;
class DxPatch;

#include "instruments/deluge/sound_state.h"

class Source {
public:
	Source();
	~Source();

	/// @brief Non-owning pointer into the owning Sound's SoundState (state_->sources[s]); null while unbound.
	///
	/// A Source is never copied by value (it lives as a fixed member of its Sound), so this pointer has no clone
	/// hazard. The record holds the oscillator's settings; the Sound's config is what the render reads of them.
	deluge::instrument::SourceState* state_{nullptr};

	/// @brief Bind this Source to its record and seed the record's DX7 patch and ranges from the native ones.
	/// @param d The SourceState record to bind (state_->sources[s] of the owning Sound).
	void bindState(deluge::instrument::SourceState* d);
	/// @brief Play @p d, which already holds this source's settings: nothing is seeded into it.
	void attachState(deluge::instrument::SourceState* d);

	/// @brief Re-seed the bound record's DX7 patch from the native patch; no-op while unbound.
	///
	/// The DX7 patch is mutated in place (per-param edits, engine mode, cartridge load) rather than through one
	/// setter, so the whole optional<DxPatchState> record is re-seeded after any mutation (bind, the dx/ menus,
	/// setOscType via ensureDxPatch).
	void seedDocDxPatch();

	/// @brief Copy the DX7 patch into @p d: the 156 params, engineMode and random_detune, or clear it when the
	/// engine patch is absent (non-DX7 osc).
	/// @param d The record to fill.
	void seedDxPatchInto(deluge::instrument::SourceState& d) const;

	/// @brief Re-seed the bound record's whole range list from the native ranges; no-op while unbound.
	///
	/// Called after any range mutation (zone edit, add/remove, sample-set, reverse). The engine's
	/// sampleHolder/ranges stay the working structure; this mirrors them into the record.
	void seedDocRanges();

	/// @brief Copy the ranges into @p d: each one's top note and file, and while they are sample ranges (whatever the
	/// type now) their zones and tuning, which a type change and back keeps.
	/// @param d The record to fill.
	void seedRangesInto(deluge::instrument::SourceState& d) const;

	OscType oscType;

	/// @brief Whether the note last set up on this sample source plays against the record's direction (the
	/// arpeggiator's reverse step); cleared when a voice of the sound releases.
	bool invertReversed = false;

	/// @brief Whether the ranges play backwards now: the record's direction, inverted for an arpeggiated note.
	[[nodiscard]] bool isCurrentlyReversed() const { return state_->reversed != invertReversed; }

	MultiRangeArray ranges;

	DxPatch* dxPatch;
	bool dxPatchChanged = false;
	/// @brief The live histories this oscillator's voices read while its type is an input type; empty otherwise.
	deluge::synth::LiveInputStore liveInputs;

	int16_t defaultRangeI; // -1 means none yet

	/// Whether this source renders in stereo. Static + fully parameterised (no `Sound`/`Source` reach) so the
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
	/// @brief Play the ranges in the direction isCurrentlyReversed() names: a reversed range's end is clamped to its
	/// sample's length, and every range's clusters are claimed for the direction. Writes no record.
	void followDirection();
	MultiRange* getRange(int32_t note);
	int32_t getRangeIndex(int32_t note);
	MultiRange* getOrCreateFirstRange();
	bool hasAtLeastOneAudioFileLoaded();
	bool hasAnyLoopEndPoint();
	void setOscType(OscType newType);

	DxPatch* ensureDxPatch();

	/// @brief Makes or frees the live histories to match the type, handing them to the render inside an
	/// audio-excluded section. [task]
	void reconcileLiveInputs();
};
