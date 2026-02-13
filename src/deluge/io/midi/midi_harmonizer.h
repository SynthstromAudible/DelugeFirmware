/*
 * Copyright © 2024 Synthstrom Audible Limited
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

#include "io/midi/harmonizer_settings.h"
#include <array>
#include <cstdint>

static constexpr int32_t kMaxMidiNotes = 128;
static constexpr int32_t kMaxChordNotes = 16;

/// Configuration for harmonization.
struct HarmonizeConfig {
	HarmonizerMappingMode mode{HarmonizerMappingMode::Nearest};
	HarmonizerTightness tightness{HarmonizerTightness::Strict};
	bool voiceLeading{false};
	uint8_t scaleRoot{0};
	uint16_t scaleBits{0}; // NoteSet-compatible bitfield
	int32_t transpose{0};
};

/// Tracks which notes are currently held on the chord channel.
class ChordState {
public:
	ChordState();
	void noteOn(uint8_t note);
	void noteOff(uint8_t note);
	bool isEmpty() const { return heldCount_ == 0; }
	void reset();

	const uint8_t* pitchClasses() const { return pitchClasses_.data(); }
	int32_t pitchClassCount() const { return pitchClassCount_; }
	const uint8_t* heldNotes() const { return heldNotes_.data(); }
	int32_t heldCount() const { return heldCount_; }

private:
	void updatePitchClasses();
	std::array<uint8_t, kMaxChordNotes> heldNotes_{};
	int32_t heldCount_{0};
	std::array<uint8_t, 12> pitchClasses_{};
	int32_t pitchClassCount_{0};
};

/// Active note mapping: input note -> output note + velocity.
struct ActiveNote {
	uint8_t outputNote{0};
	uint8_t velocity{0};
	bool active{false};
};

/// Per-channel tracking of input->output note mappings for correct note-off handling.
class ChannelState {
public:
	ChannelState();
	void setMapping(uint8_t input, uint8_t output, uint8_t velocity);
	ActiveNote getMapping(uint8_t input) const;
	ActiveNote removeMapping(uint8_t input);

	void setIntervalMapping(uint8_t input, uint8_t output, uint8_t velocity);
	ActiveNote getIntervalMapping(uint8_t input) const;
	ActiveNote removeIntervalMapping(uint8_t input);

	void reset();

	int32_t lastOutputNote{-1}; // -1 = no previous output
	bool hasLastOutput{false};

private:
	std::array<ActiveNote, kMaxMidiNotes> activeNotes_{};
	std::array<ActiveNote, kMaxMidiNotes> intervalNotes_{}; // Diatonic interval parallel voices
};

/// Buffer for expanded chord tones across the MIDI range.
struct ExpandedTones {
	std::array<uint8_t, kMaxMidiNotes> notes{};
	int32_t count{0};
};

// Core harmonization functions
uint8_t applyTranspose(uint8_t note, int32_t transpose);
ExpandedTones expandChordTones(const uint8_t* pitchClasses, int32_t pitchClassCount);
uint8_t harmonizeNote(uint8_t input, const ChordState& chord, HarmonizerMappingMode mode);
uint8_t harmonizeWithVoiceLeading(uint8_t input, const ChordState& chord, int32_t lastOutput, bool hasLastOutput,
                                  HarmonizerMappingMode mode);
uint8_t harmonize(uint8_t input, const ChordState& chord, int32_t lastOutput, bool hasLastOutput,
                  const HarmonizeConfig& config);

/// Compute a diatonic interval voice. Returns the parallel note, or -1 if interval is Off.
/// scaleRoot: 0-11, scaleBits: NoteSet-compatible bitfield (bit 0 = root, bit 1 = minor 2nd, etc.)
int32_t computeDiatonicInterval(uint8_t inputNote, DiatonicInterval interval, uint8_t scaleRoot, uint16_t scaleBits);

/// Global harmonizer state, shared across all MIDIInstrument instances.
class HarmonizerState {
public:
	HarmonizerState();

	ChordState chordState;
	HarmonizeConfig config;
	std::array<ChannelState, 16> channelStates;
	int32_t physicallyHeldCount_{0}; // Tracks chord keys physically held (for latch)

	void reset();
};

extern HarmonizerState midiHarmonizer;
