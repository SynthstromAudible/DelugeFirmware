#include "model/sequencing/lanes_engine.h"

#include "model/scale/musical_key.h"

LanesEngine::LanesEngine() {
	// Probability lane: 100% so notes always fire
	probability.values.fill(100);
	// Gate lane: default note length
	gate.values.fill(kDefaultGate);
	// Velocity lane: default velocity (overridden by instrument setting at toggle time)
	velocity.values.fill(defaultVelocity);

	// Per-step probability defaults to 100% on all lanes
	trigger.initProbability();
	pitch.initProbability();
	octave.initProbability();
	velocity.initProbability();
	gate.initProbability();
	interval.initProbability();
	retrigger.initProbability();
	probability.initProbability();
	glide.initProbability();
}

int16_t LanesLane::currentValue() const {
	if (length == 0) {
		return 0;
	}
	return values[position];
}

void LanesLane::advance() {
	lastPosition = position;
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

bool LanesLane::advanceWithDivision() {
	if (clockDivision <= 1) {
		advance();
		return true;
	}
	divisionCounter--;
	if (divisionCounter == 0) {
		divisionCounter = clockDivision;
		advance();
		return true;
	}
	return false;
}

void LanesLane::reset() {
	position = 0;
	lastPosition = 0;
	pingpongReversed = false;
	divisionCounter = clockDivision;
}

LanesNote LanesEngine::step(const MusicalKey& key) {
	// Track length: reset all lane positions when global counter wraps
	if (trackLength > 0) {
		if (globalTickCounter >= trackLength) {
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
			globalTickCounter = 0;
		}
		globalTickCounter++;
	}

	// Read ALL lane values at their current positions BEFORE advancing any
	// Muted lanes return their default value (trigger muted = always rest)
	bool shouldFire = (trigger.length > 0 && !trigger.muted) ? (trigger.currentValue() != 0) : false;
	int32_t pitchDegrees = readLaneValue(pitch, 1, 0);
	int16_t intervalVal = readLaneValue(interval, 5, 0);
	int16_t octaveVal = readLaneValue(octave, 2, 0);
	uint8_t probVal =
	    (probability.length > 0 && !probability.muted) ? static_cast<uint8_t>(probability.currentValue()) : 100;
	uint8_t velVal = static_cast<uint8_t>(readLaneValue(velocity, 3, defaultVelocity));
	uint8_t gateVal = (gate.length > 0 && !gate.muted) ? static_cast<uint8_t>(gate.currentValue()) : kDefaultGate;
	uint8_t retrigVal = static_cast<uint8_t>(readLaneValue(retrigger, 6, 0));
	bool glideVal = (glide.length > 0 && !glide.muted) ? (glide.currentValue() != 0) : false;

	// Advance ALL lanes every tick — lanes free-run independently of trigger
	advanceAllLanes();

	// If trigger is off, return rest (lanes have already advanced)
	if (!shouldFire) {
		return LanesNote::rest();
	}

	// Accumulate interval only when trigger fires (affects note output, not lane position)
	if (interval.length > 0) {
		intervalAccumulator += intervalVal;
		if (intervalAccumulator > intervalMax) {
			intervalAccumulator = intervalMin;
		}
		else if (intervalAccumulator < intervalMin) {
			intervalAccumulator = intervalMax;
		}
	}

	// Compose final note: combine pitch + interval as scale degrees, then convert once
	int32_t noteCode;
	if (interval.length > 0 && intervalScaleAware) {
		noteCode = baseNote + scaleDegreeToSemitoneOffset(pitchDegrees + intervalAccumulator, key);
	}
	else if (interval.length > 0) {
		noteCode = baseNote + scaleDegreeToSemitoneOffset(pitchDegrees, key) + intervalAccumulator;
	}
	else {
		noteCode = baseNote + scaleDegreeToSemitoneOffset(pitchDegrees, key);
	}

	// Add octave shift
	noteCode += octaveVal * 12;

	// Clamp to MIDI range
	noteCode = std::clamp(noteCode, (int32_t)0, (int32_t)127);

	// Check probability — values 0-100 control per-step chance of note firing
	if (probVal < 100 && randomPercent() >= probVal) {
		return LanesNote::rest();
	}

	LanesNote note;
	note.noteCode = static_cast<int16_t>(noteCode);
	note.velocity = velVal;
	note.gate = gateVal;
	note.retrigger = retrigVal;
	note.glide = glideVal;

	return note;
}

LanesLane* LanesEngine::getLane(int32_t index) {
	switch (index) {
	case 0:
		return &trigger;
	case 1:
		return &pitch;
	case 2:
		return &octave;
	case 3:
		return &velocity;
	case 4:
		return &gate;
	case 5:
		return &interval;
	case 6:
		return &retrigger;
	case 7:
		return &probability;
	case 8:
		return &glide;
	default:
		return nullptr;
	}
}

void LanesEngine::reset() {
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
	globalTickCounter = 0;
	if (locked) {
		rngState_ = lockedSeed;
	}
}

void LanesEngine::advanceAllLanes() {
	trigger.advanceWithDivision();
	pitch.advanceWithDivision();
	interval.advanceWithDivision();
	velocity.advanceWithDivision();
	octave.advanceWithDivision();
	gate.advanceWithDivision();
	retrigger.advanceWithDivision();
	probability.advanceWithDivision();
	glide.advanceWithDivision();
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

void LanesEngine::setSeed(uint32_t seed) {
	rngState_ = seed ? seed : 1;
}

uint8_t LanesEngine::randomPercent() {
	// xorshift32
	rngState_ ^= rngState_ << 13;
	rngState_ ^= rngState_ >> 17;
	rngState_ ^= rngState_ << 5;
	return rngState_ % 100;
}

int16_t LanesEngine::randomInRange(int16_t minVal, int16_t maxVal) {
	if (minVal >= maxVal) {
		return minVal;
	}
	// xorshift32 step
	rngState_ ^= rngState_ << 13;
	rngState_ ^= rngState_ >> 17;
	rngState_ ^= rngState_ << 5;
	int32_t range = static_cast<int32_t>(maxVal) - static_cast<int32_t>(minVal) + 1;
	return minVal + static_cast<int16_t>(rngState_ % range);
}

void LanesEngine::lock() {
	locked = true;
	lockedSeed = rngState_;
}

void LanesEngine::unlock() {
	locked = false;
}

void LanesLane::initProbability() {
	for (auto& p : stepProbability) {
		p = 100;
	}
}

int16_t LanesEngine::readLaneValue(LanesLane& lane, int32_t laneIdx, int16_t defaultVal) {
	if (lane.length == 0 || lane.muted) {
		return defaultVal;
	}
	if (kLaneHasStepProb[laneIdx]) {
		uint8_t prob = lane.stepProbability[lane.position];
		if (prob < 100 && randomPercent() >= prob) {
			return defaultVal;
		}
	}
	return lane.currentValue();
}

void LanesEngine::generateEuclidean(int32_t length, int32_t pulses) {
	length = std::clamp(length, (int32_t)1, (int32_t)kMaxLanesSteps);
	pulses = std::clamp(pulses, (int32_t)0, length);

	trigger.length = static_cast<uint8_t>(length);
	trigger.euclideanPulses = static_cast<uint8_t>(pulses);

	// Clear all
	for (int32_t i = 0; i < length; i++) {
		trigger.values[i] = 0;
	}

	if (pulses == 0) {
		return;
	}

	// Bresenham-style accumulator to distribute `pulses` ones across `length` slots evenly
	std::array<bool, kMaxLanesSteps> pattern{};
	int32_t bucket = 0;
	for (int32_t i = 0; i < length; i++) {
		bucket += pulses;
		if (bucket >= length) {
			bucket -= length;
			pattern[i] = true;
		}
	}

	for (int32_t i = 0; i < length; i++) {
		trigger.values[i] = pattern[i] ? 1 : 0;
	}
}

void LanesEngine::rotateTriggerPattern(int32_t offset) {
	if (trigger.length <= 1) {
		return;
	}
	int32_t len = trigger.length;
	// Normalize offset to [0, len)
	offset = ((offset % len) + len) % len;
	if (offset == 0) {
		return;
	}
	// Rotate: shift all values by `offset` positions
	std::array<int16_t, kMaxLanesSteps> temp{};
	for (int32_t i = 0; i < len; i++) {
		temp[(i + offset) % len] = trigger.values[i];
	}
	for (int32_t i = 0; i < len; i++) {
		trigger.values[i] = temp[i];
	}
}

void LanesEngine::clearLane(int32_t laneIdx) {
	LanesLane* lane = getLane(laneIdx);
	if (!lane) {
		return;
	}
	for (int32_t i = 0; i < lane->length; i++) {
		lane->values[i] = 0;
		lane->stepProbability[i] = 100;
	}
}

void LanesEngine::randomizeLane(int32_t laneIdx) {
	LanesLane* lane = getLane(laneIdx);
	if (!lane) {
		return;
	}
	// Velocity (3), gate (4): zero is not meaningful, always use range
	// Pitch (1) is now relative — 0 = base note, so zero is fine
	bool allowZero = (laneIdx != 3 && laneIdx != 4);
	for (int32_t i = 0; i < lane->length; i++) {
		if (allowZero && randomPercent() < 30) {
			lane->values[i] = 0;
		}
		else {
			lane->values[i] = randomInRange(kLaneRndMins[laneIdx], kLaneRndMaxs[laneIdx]);
		}
	}
}

void LanesEngine::loadDefaultPattern() {
	// All lanes: 16 steps for a clear, musical default

	// Trigger: 8th-note pattern (every other step)
	trigger.length = 16;
	for (int i = 0; i < 16; i++) {
		trigger.values[i] = 0;
	}
	trigger.values[0] = 1;
	trigger.values[2] = 1;
	trigger.values[4] = 1;
	trigger.values[6] = 1;
	trigger.values[8] = 1;
	trigger.values[10] = 1;
	trigger.values[12] = 1;
	trigger.values[14] = 1;

	// Pitch: ascending scale degrees from base note (0 = root, 1 = 2nd, 2 = 3rd, etc.)
	pitch.length = 16;
	for (int i = 0; i < 16; i++) {
		pitch.values[i] = 0; // all base note by default
	}
	pitch.values[0] = 0;  // root
	pitch.values[2] = 1;  // 2nd
	pitch.values[4] = 2;  // 3rd
	pitch.values[6] = 3;  // 4th
	pitch.values[8] = 4;  // 5th
	pitch.values[10] = 5; // 6th
	pitch.values[12] = 6; // 7th
	pitch.values[14] = 7; // octave (next root)

	// Velocity: uniform
	velocity.length = 16;
	for (int i = 0; i < 16; i++) {
		velocity.values[i] = 100;
	}

	// Octave: all zero (no shift)
	octave.length = 16;
	for (int i = 0; i < 16; i++) {
		octave.values[i] = 0;
	}

	// Gate: uniform
	gate.length = 16;
	for (int i = 0; i < 16; i++) {
		gate.values[i] = 75;
	}

	// Interval: all zero (no interval accumulation)
	interval.length = 16;
	for (int i = 0; i < 16; i++) {
		interval.values[i] = 0;
	}

	// Retrigger: all zero
	retrigger.length = 16;
	for (int i = 0; i < 16; i++) {
		retrigger.values[i] = 0;
	}

	// Probability: all 100%
	probability.length = 16;
	for (int i = 0; i < 16; i++) {
		probability.values[i] = 100;
	}

	// Glide: all off
	glide.length = 16;
	for (int i = 0; i < 16; i++) {
		glide.values[i] = 0;
	}

	// Initialize per-step probability to 100% on all lanes
	trigger.initProbability();
	pitch.initProbability();
	octave.initProbability();
	velocity.initProbability();
	gate.initProbability();
	interval.initProbability();
	retrigger.initProbability();
	probability.initProbability();
	glide.initProbability();
}
