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

#include "io/midi/midi_harmonizer.h"
#include <algorithm>
#include <cstdlib>

// Global singleton
HarmonizerState midiHarmonizer;

// ---- ChordState ----

ChordState::ChordState() {
	heldNotes_.fill(0);
	pitchClasses_.fill(0);
}

void ChordState::noteOn(uint8_t note) {
	// Check for duplicate
	for (int32_t i = 0; i < heldCount_; i++) {
		if (heldNotes_[i] == note) {
			return;
		}
	}
	if (heldCount_ >= kMaxChordNotes) {
		return;
	}
	heldNotes_[heldCount_] = note;
	heldCount_++;
	// Keep sorted
	std::sort(heldNotes_.begin(), heldNotes_.begin() + heldCount_);
	updatePitchClasses();
}

void ChordState::noteOff(uint8_t note) {
	int32_t pos = -1;
	for (int32_t i = 0; i < heldCount_; i++) {
		if (heldNotes_[i] == note) {
			pos = i;
			break;
		}
	}
	if (pos < 0) {
		return;
	}
	// Shift remaining notes down
	for (int32_t i = pos; i < heldCount_ - 1; i++) {
		heldNotes_[i] = heldNotes_[i + 1];
	}
	heldCount_--;
	updatePitchClasses();
}

void ChordState::reset() {
	heldCount_ = 0;
	pitchClassCount_ = 0;
}

void ChordState::updatePitchClasses() {
	pitchClassCount_ = 0;
	for (int32_t i = 0; i < heldCount_; i++) {
		uint8_t pc = heldNotes_[i] % 12;
		bool alreadyPresent = false;
		for (int32_t j = 0; j < pitchClassCount_; j++) {
			if (pitchClasses_[j] == pc) {
				alreadyPresent = true;
				break;
			}
		}
		if (!alreadyPresent && pitchClassCount_ < 12) {
			pitchClasses_[pitchClassCount_] = pc;
			pitchClassCount_++;
		}
	}
	std::sort(pitchClasses_.begin(), pitchClasses_.begin() + pitchClassCount_);
}

// ---- ChannelState ----

ChannelState::ChannelState() {
	reset();
}

void ChannelState::setMapping(uint8_t input, uint8_t output, uint8_t velocity) {
	activeNotes_[input] = {output, velocity, true};
	lastOutputNote = output;
	hasLastOutput = true;
}

ActiveNote ChannelState::getMapping(uint8_t input) const {
	return activeNotes_[input];
}

ActiveNote ChannelState::removeMapping(uint8_t input) {
	ActiveNote result = activeNotes_[input];
	activeNotes_[input] = {0, 0, false};
	return result;
}

ActiveNote ChannelState::getIntervalMapping(uint8_t input) const {
	return intervalNotes_[input];
}

void ChannelState::setIntervalMapping(uint8_t input, uint8_t output, uint8_t velocity) {
	intervalNotes_[input] = {output, velocity, true};
}

ActiveNote ChannelState::removeIntervalMapping(uint8_t input) {
	ActiveNote result = intervalNotes_[input];
	intervalNotes_[input] = {0, 0, false};
	return result;
}

void ChannelState::reset() {
	for (auto& note : activeNotes_) {
		note = {0, 0, false};
	}
	for (auto& note : intervalNotes_) {
		note = {0, 0, false};
	}
	lastOutputNote = -1;
	hasLastOutput = false;
}

// ---- Core harmonization functions ----

uint8_t applyTranspose(uint8_t note, int32_t transpose) {
	int32_t result = static_cast<int32_t>(note) + transpose;
	if (result < 0) {
		return 0;
	}
	if (result > 127) {
		return 127;
	}
	return static_cast<uint8_t>(result);
}

ExpandedTones expandChordTones(const uint8_t* pitchClasses, int32_t pitchClassCount) {
	ExpandedTones result;
	for (uint8_t octave = 0; octave <= 10; octave++) {
		for (int32_t i = 0; i < pitchClassCount; i++) {
			uint8_t note = octave * 12 + pitchClasses[i];
			if (note <= 127 && result.count < kMaxMidiNotes) {
				result.notes[result.count] = note;
				result.count++;
			}
		}
	}
	return result;
}

/// Check if a pitch class is within 1 semitone of any chord pitch class.
static bool isNearChordTone(uint8_t pitchClass, const uint8_t* chordPitchClasses, int32_t count) {
	for (int32_t i = 0; i < count; i++) {
		int8_t diff1 = static_cast<int8_t>((pitchClass - chordPitchClasses[i] + 12) % 12);
		int8_t diff2 = static_cast<int8_t>((chordPitchClasses[i] - pitchClass + 12) % 12);
		int8_t minDiff = (diff1 < diff2) ? diff1 : diff2;
		if (minDiff <= 1) {
			return true;
		}
	}
	return false;
}

uint8_t harmonizeNote(uint8_t input, const ChordState& chord, HarmonizerMappingMode mode) {
	if (chord.isEmpty()) {
		return input;
	}

	ExpandedTones tones = expandChordTones(chord.pitchClasses(), chord.pitchClassCount());

	switch (mode) {
	case HarmonizerMappingMode::Nearest: {
		uint8_t best = input;
		int16_t bestDist = INT16_MAX;
		for (int32_t i = 0; i < tones.count; i++) {
			int16_t dist = std::abs(static_cast<int16_t>(tones.notes[i]) - static_cast<int16_t>(input));
			if (dist < bestDist) {
				bestDist = dist;
				best = tones.notes[i];
			}
		}
		return best;
	}
	case HarmonizerMappingMode::RoundDown: {
		uint8_t best = input;
		for (int32_t i = 0; i < tones.count; i++) {
			if (tones.notes[i] <= input) {
				best = tones.notes[i];
			}
			else {
				break;
			}
		}
		return best;
	}
	case HarmonizerMappingMode::RoundUp: {
		for (int32_t i = 0; i < tones.count; i++) {
			if (tones.notes[i] >= input) {
				return tones.notes[i];
			}
		}
		return input;
	}
	case HarmonizerMappingMode::Root: {
		// Snap to the nearest occurrence of the chord root
		if (chord.pitchClassCount() == 0) {
			return input;
		}
		uint8_t rootPc = chord.pitchClasses()[0]; // Smallest pitch class number (not necessarily the musical root)
		uint8_t best = input;
		int16_t bestDist = INT16_MAX;
		for (uint8_t octave = 0; octave <= 10; octave++) {
			uint8_t candidate = octave * 12 + rootPc;
			if (candidate > 127) {
				break;
			}
			int16_t dist = std::abs(static_cast<int16_t>(candidate) - static_cast<int16_t>(input));
			if (dist < bestDist) {
				bestDist = dist;
				best = candidate;
			}
		}
		return best;
	}
	case HarmonizerMappingMode::Root5th: {
		// Snap to the nearest root or 5th of the chord
		if (chord.pitchClassCount() == 0) {
			return input;
		}
		uint8_t rootPc = chord.pitchClasses()[0];
		uint8_t fifthPc = (rootPc + 7) % 12;
		uint8_t best = input;
		int16_t bestDist = INT16_MAX;
		for (uint8_t octave = 0; octave <= 10; octave++) {
			uint8_t r = octave * 12 + rootPc;
			if (r <= 127) {
				int16_t dist = std::abs(static_cast<int16_t>(r) - static_cast<int16_t>(input));
				if (dist < bestDist) {
					bestDist = dist;
					best = r;
				}
			}
			uint8_t f = octave * 12 + fifthPc;
			if (f <= 127) {
				int16_t dist = std::abs(static_cast<int16_t>(f) - static_cast<int16_t>(input));
				if (dist < bestDist) {
					bestDist = dist;
					best = f;
				}
			}
		}
		return best;
	}
	default:
		break;
	}
	return input;
}

uint8_t harmonizeWithVoiceLeading(uint8_t input, const ChordState& chord, int32_t lastOutput, bool hasLastOutput,
                                  HarmonizerMappingMode mode) {
	uint8_t base = harmonizeNote(input, chord, mode);

	if (!hasLastOutput) {
		return base;
	}

	if (chord.isEmpty()) {
		return input;
	}

	ExpandedTones tones = expandChordTones(chord.pitchClasses(), chord.pitchClassCount());

	uint8_t best = base;
	int32_t bestScore = INT32_MAX;

	for (int32_t i = 0; i < tones.count; i++) {
		uint8_t t = tones.notes[i];
		int32_t distToInput = std::abs(static_cast<int32_t>(t) - static_cast<int32_t>(input));
		if (distToInput <= 7) {
			int32_t distToPrev = std::abs(static_cast<int32_t>(t) - lastOutput);
			int32_t score = distToInput * 2 + distToPrev;
			if (score < bestScore) {
				bestScore = score;
				best = t;
			}
		}
	}

	return best;
}

uint8_t harmonize(uint8_t input, const ChordState& chord, int32_t lastOutput, bool hasLastOutput,
                  const HarmonizeConfig& config) {
	if (chord.isEmpty()) {
		return applyTranspose(input, config.transpose);
	}

	uint8_t harmonized = input;

	switch (config.tightness) {
	case HarmonizerTightness::Strict: {
		if (config.voiceLeading) {
			harmonized = harmonizeWithVoiceLeading(input, chord, lastOutput, hasLastOutput, config.mode);
		}
		else {
			harmonized = harmonizeNote(input, chord, config.mode);
		}
		break;
	}
	case HarmonizerTightness::Scale: {
		// Check if the note is on-scale using NoteSet bitfield
		uint8_t pc = input % 12;
		int8_t interval = static_cast<int8_t>((pc - config.scaleRoot + 12) % 12);
		bool onScale = (config.scaleBits >> interval) & 1;
		if (onScale) {
			harmonized = input; // On-scale, pass through
		}
		else {
			// Off-scale, snap to chord tones
			if (config.voiceLeading) {
				harmonized = harmonizeWithVoiceLeading(input, chord, lastOutput, hasLastOutput, config.mode);
			}
			else {
				harmonized = harmonizeNote(input, chord, config.mode);
			}
		}
		break;
	}
	case HarmonizerTightness::Extensions: {
		// Allow chord tones and "color tones" (jazz extensions), block avoid notes.
		// Avoid notes = half-step above any chord tone.
		uint8_t pc = input % 12;
		bool isChordTone = false;
		for (int32_t i = 0; i < chord.pitchClassCount(); i++) {
			if (chord.pitchClasses()[i] == pc) {
				isChordTone = true;
				break;
			}
		}
		if (isChordTone) {
			harmonized = input; // Chord tone, always pass
		}
		else {
			// Check if this note is an "avoid note" (half-step above any chord tone)
			bool isAvoidNote = false;
			for (int32_t i = 0; i < chord.pitchClassCount(); i++) {
				if (pc == (chord.pitchClasses()[i] + 1) % 12) {
					isAvoidNote = true;
					break;
				}
			}
			if (isAvoidNote) {
				// Avoid note — snap to nearest chord tone
				if (config.voiceLeading) {
					harmonized = harmonizeWithVoiceLeading(input, chord, lastOutput, hasLastOutput, config.mode);
				}
				else {
					harmonized = harmonizeNote(input, chord, config.mode);
				}
			}
			else {
				harmonized = input; // Color tone (extension), pass through
			}
		}
		break;
	}
	case HarmonizerTightness::Loose: {
		uint8_t pc = input % 12;
		// Check if it's already a chord tone
		bool isChordTone = false;
		for (int32_t i = 0; i < chord.pitchClassCount(); i++) {
			if (chord.pitchClasses()[i] == pc) {
				isChordTone = true;
				break;
			}
		}
		if (isChordTone) {
			harmonized = input; // Already a chord tone, pass through
		}
		else if (isNearChordTone(pc, chord.pitchClasses(), chord.pitchClassCount())) {
			// Clashing with a chord tone, snap it
			if (config.voiceLeading) {
				harmonized = harmonizeWithVoiceLeading(input, chord, lastOutput, hasLastOutput, config.mode);
			}
			else {
				harmonized = harmonizeNote(input, chord, config.mode);
			}
		}
		else {
			harmonized = input; // Not clashing, pass through
		}
		break;
	}
	default:
		// Unknown tightness — fall back to Strict
		harmonized = harmonizeNote(input, chord, config.mode);
		break;
	}

	return applyTranspose(harmonized, config.transpose);
}

// ---- Diatonic Interval ----

/// Build the scale as an array of MIDI notes across the full MIDI range (0-127).
/// Returns how many scale degrees were found.
static int32_t buildScaleArray(uint8_t scaleRoot, uint16_t scaleBits, uint8_t* outNotes, int32_t maxNotes) {
	int32_t count = 0;
	for (int32_t midiNote = 0; midiNote <= 127 && count < maxNotes; midiNote++) {
		uint8_t interval = (midiNote - scaleRoot + 120) % 12; // +120 to avoid negative modulo
		if ((scaleBits >> interval) & 1) {
			outNotes[count++] = static_cast<uint8_t>(midiNote);
		}
	}
	return count;
}

int32_t computeDiatonicInterval(uint8_t inputNote, DiatonicInterval interval, uint8_t scaleRoot, uint16_t scaleBits) {
	if (interval == DiatonicInterval::Off || scaleBits == 0) {
		return -1;
	}

	// Octave is simple — doesn't need scale lookup
	if (interval == DiatonicInterval::Octave_Above) {
		int32_t result = inputNote + 12;
		return (result <= 127) ? result : -1;
	}

	// Build scale array for lookup
	uint8_t scaleNotes[256]; // Enough for all scale notes across MIDI range
	int32_t scaleLen = buildScaleArray(scaleRoot, scaleBits, scaleNotes, 256);
	if (scaleLen == 0) {
		return -1;
	}

	// Find the input note's position in the scale (or nearest scale degree)
	int32_t inputIdx = -1;
	int32_t bestDist = INT32_MAX;
	for (int32_t i = 0; i < scaleLen; i++) {
		int32_t dist = std::abs(static_cast<int32_t>(scaleNotes[i]) - static_cast<int32_t>(inputNote));
		if (dist < bestDist) {
			bestDist = dist;
			inputIdx = i;
		}
		if (scaleNotes[i] >= inputNote) {
			break; // Scale is sorted, no need to continue
		}
	}

	if (inputIdx < 0) {
		return -1;
	}

	// Compute the target scale degree offset
	int32_t degreeOffset = 0;
	switch (interval) {
	case DiatonicInterval::Third_Above:
		degreeOffset = 2; // 2 scale steps up = diatonic 3rd
		break;
	case DiatonicInterval::Third_Below:
		degreeOffset = -2;
		break;
	case DiatonicInterval::Sixth_Above:
		degreeOffset = 5; // 5 scale steps up = diatonic 6th
		break;
	case DiatonicInterval::Sixth_Below:
		degreeOffset = -5;
		break;
	default:
		return -1;
	}

	int32_t targetIdx = inputIdx + degreeOffset;
	if (targetIdx < 0 || targetIdx >= scaleLen) {
		return -1; // Out of MIDI range
	}

	return scaleNotes[targetIdx];
}

// ---- HarmonizerState ----

HarmonizerState::HarmonizerState() {
	config.mode = HarmonizerMappingMode::Nearest;
	config.tightness = HarmonizerTightness::Strict;
	config.voiceLeading = false;
	config.transpose = 0;
}

void HarmonizerState::reset() {
	chordState.reset();
	for (auto& ch : channelStates) {
		ch.reset();
	}
	physicallyHeldCount_ = 0;
}
