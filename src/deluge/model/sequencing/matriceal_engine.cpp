#include "model/sequencing/matriceal_engine.h"

#include "model/scale/musical_key.h"

int16_t MatricealLane::currentValue() const {
	if (length == 0) {
		return 0;
	}
	return values[position];
}

void MatricealLane::advance() {
	if (length <= 1) {
		return;
	}

	switch (direction) {
	case LaneDirection::FORWARD:
		position = (position + 1) % length;
		break;

	case LaneDirection::REVERSE:
		if (position == 0) {
			position = length - 1;
		}
		else {
			position--;
		}
		break;

	case LaneDirection::PINGPONG:
		if (!pingpongReversed) {
			if (position >= length - 1) {
				pingpongReversed = true;
				position--;
			}
			else {
				position++;
			}
		}
		else {
			if (position == 0) {
				pingpongReversed = false;
				position++;
			}
			else {
				position--;
			}
		}
		break;
	}
}

void MatricealLane::reset() {
	position = 0;
	pingpongReversed = false;
}

MatricealNote MatricealEngine::step(const MusicalKey& key) {
	// Read trigger value at current position
	bool shouldFire = (trigger.length > 0) ? (trigger.currentValue() != 0) : false;
	trigger.advance();

	if (!shouldFire) {
		return MatricealNote::rest();
	}

	// Read current values from all gated lanes BEFORE advancing
	int32_t noteCode = (pitch.length > 0) ? pitch.currentValue() : 60;

	// Add interval accumulation (scale degrees -> semitones)
	if (interval.length > 0) {
		intervalAccumulator += interval.currentValue();
		intervalAccumulator = std::clamp(intervalAccumulator, -kMaxIntervalSemitones, kMaxIntervalSemitones);
		noteCode += scaleDegreeToSemitoneOffset(intervalAccumulator, key);
	}

	// Add octave shift
	if (octave.length > 0) {
		noteCode += octave.currentValue() * 12;
	}

	// Clamp to MIDI range
	noteCode = std::clamp(noteCode, 0, 127);

	// Check probability — values 0-100 control per-step chance of note firing
	if (probability.length > 0) {
		uint8_t prob = static_cast<uint8_t>(probability.currentValue());
		if (prob < 100 && randomPercent() >= prob) {
			pitch.advance();
			interval.advance();
			velocity.advance();
			octave.advance();
			gate.advance();
			retrigger.advance();
			probability.advance();
			glide.advance();
			return MatricealNote::rest();
		}
	}

	MatricealNote note;
	note.noteCode = static_cast<int16_t>(noteCode);
	note.velocity = (velocity.length > 0) ? static_cast<uint8_t>(velocity.currentValue()) : kDefaultVelocity;
	note.gate = (gate.length > 0) ? static_cast<uint8_t>(gate.currentValue()) : kDefaultGate;
	note.retrigger = (retrigger.length > 0) ? static_cast<uint8_t>(retrigger.currentValue()) : 0;
	note.glide = (glide.length > 0) ? (glide.currentValue() != 0) : false;

	// Now advance all gated lanes
	pitch.advance();
	interval.advance();
	velocity.advance();
	octave.advance();
	gate.advance();
	retrigger.advance();
	probability.advance();
	glide.advance();

	return note;
}

void MatricealEngine::reset() {
	trigger.reset();
	pitch.reset();
	interval.reset();
	velocity.reset();
	octave.reset();
	gate.reset();
	retrigger.reset();
	probability.reset();
	glide.reset();
	intervalAccumulator = 0;
}

int32_t scaleDegreeToSemitoneOffset(int32_t degrees, const MusicalKey& key) {
	int32_t scaleSize = key.modeNotes.count();
	if (scaleSize <= 1) {
		return degrees; // chromatic fallback
	}

	// Decompose into octaves and remaining degrees using floored division
	int32_t octaves;
	int32_t remaining;
	if (degrees >= 0) {
		octaves = degrees / scaleSize;
		remaining = degrees % scaleSize;
	}
	else {
		// For negative: e.g., -1 in a 7-note scale = -1 octave + 6 remaining
		octaves = (degrees - scaleSize + 1) / scaleSize;
		remaining = degrees - octaves * scaleSize;
	}

	int32_t semitones = octaves * 12;
	if (remaining > 0) {
		semitones += key.modeNotes[remaining];
	}
	return semitones;
}

void MatricealEngine::setSeed(uint32_t seed) {
	rngState_ = seed ? seed : 1;
}

uint8_t MatricealEngine::randomPercent() {
	// xorshift32
	rngState_ ^= rngState_ << 13;
	rngState_ ^= rngState_ >> 17;
	rngState_ ^= rngState_ << 5;
	return rngState_ % 100;
}
