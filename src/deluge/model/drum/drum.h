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
#include "io/midi/learned_midi.h"
#include "model/voiced.h"
#include "modulation/arpeggiator.h"
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

class Clip;
class Kit;
class ModControllable;
class ModelStackWithSoundFlags;
class ModelStackWithThreeMainThings;
class ModelStackWithTimelineCounter;
class ParamManager;
class ParamManagerForTimeline;
class Song;

namespace deluge::project {
struct DocDrum;
}

/*
 * Kits are made up of multiple Drums. Even when they are not drum sounds, the class is called Drum, for better or
 * worse. In most instructional material for users, Synthstrom has referred to them often as “items within kits”, or
 * sometimes “rows” or “sounds” where applicable.
 *
 * Types of Drum are MIDIDrum, GateDrum, and SoundDrum (most often a sample).
 */

class Drum : public virtual Voiced {
public:
	Drum(DrumType newType);
	~Drum() override = default;

	std::string drumName{};
	Kit* kit;

	// M3 owner-3 (drum structure) slice 2: the single write funnel for the drum name. All the scattered `drumName =`
	// assignment sites route through here so the name can be shadowed into DocDrum (added in slice 2 Task 2). This
	// slice is a pure funnel — the body just writes `drumName` (behaviour-identical); the DocDrum dual-write lands in
	// Task 2. Overloads mirror the argument shapes at the call sites: string literals / `.c_str()` (const char*),
	// `std::string_view` (rename UI), and `std::string` rvalues (move-preserving). Bodies live in drum.cpp so the
	// dual-write can see the complete DocDrum type without pulling the doc header into this light header.
	void setName(const char* newName);
	void setName(std::string_view newName);
	void setName(std::string&& newName);

	const DrumType type;
	bool noteRowAssignedTemp{};
	uint8_t earlyNoteVelocity; // If 0, then there's none
	bool earlyNoteStillActive;

	bool auditioned;
	uint8_t lastMIDIChannelAuditioned; // Primarily for MPE purposes

	int8_t lastExpressionInputsReceived[2][kNumExpressionDimensions]{};

	Drum* next;

	std::optional<uint32_t> id_; // Stable per-song identity; see id(). nullopt = standalone / unassigned.

	LearnedMIDI midiInput;
	LearnedMIDI muteMIDICommand;

	ArpeggiatorForDrum arpeggiator;
	ArpeggiatorSettings arpSettings;

	virtual void noteOn(ModelStackWithThreeMainThings* modelStack, uint8_t velocity, int16_t const* mpeValues,
	                    int32_t fromMIDIChannel = MIDI_CHANNEL_NONE, uint32_t sampleSyncLength = 0,
	                    int32_t ticksLate = 0, uint32_t samplesLate = 0) = 0;
	virtual void noteOff(ModelStackWithThreeMainThings* modelStack, int32_t velocity = kDefaultLiftValue) = 0;

	virtual Error loadAllSamples(bool mayActuallyReadFiles) { return Error::NONE; }
	virtual void prepareDrumToHaveNoActiveClip() {}

	virtual void writeToFile(Serializer& writer, bool savingSong, ParamManager* paramManager) = 0;
	virtual Error readFromFile(Deserializer& reader, Song* song, Clip* clip, int32_t readAutomationUpToPos) = 0;
	virtual void drumWontBeRenderedForAWhile();
	virtual std::string getDrumName() = 0;
	virtual void choke(ModelStackWithSoundFlags* modelStack) {} // modelStack can be NULL if you really insist

	void writeMIDICommandsToFile(Serializer& writer);
	void writeDrumTagsToFile(Serializer& writer, bool savingSong);
	bool readDrumTagFromFile(Deserializer& reader, char const* tagName);

	// --- Stable per-song serial identity (see Clip/Output::id). Assigned lazily on first song-save from
	// Song::generateNewDrumId, restored on load, and used to address this drum's automation lane in the
	// persistent tree (OwnerId::Drum) independent of its transient linked-list position. nullopt = unassigned. ---
	[[nodiscard]] std::optional<uint32_t> id() const { return id_; }
	void setId(uint32_t id) { id_ = id; }

	// M3 owner-3 (drum structure): non-owning pointer to this drum's DOM-owned structural record, held in the
	// project's `drum_structure` keyed by id(). Bound via Song::adoptDrum once the drum has an id. Nothing reads it
	// yet (shadow, dual-write only). Not carried across any drum copy path (drums aren't value-copied).
	deluge::project::DocDrum* docDrum_{nullptr};
	void bindDocDrum(deluge::project::DocDrum* d) { docDrum_ = d; }

	void recordNoteOnEarly(int32_t velocity, bool noteTailsAllowed);
	void expressionEventPossiblyToRecord(ModelStackWithTimelineCounter* modelStack, int16_t newValue,
	                                     int32_t expressionDimension, int32_t level);
	virtual void expressionEvent(int32_t newValue, int32_t expressionDimension) {}
	void getCombinedExpressionInputs(int16_t* combined);

	virtual ModControllable* toModControllable() { return nullptr; }
};
