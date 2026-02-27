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
