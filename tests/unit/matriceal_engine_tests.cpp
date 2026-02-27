#include "CppUTest/TestHarness.h"
#include "model/sequencing/matriceal_engine.h"

#include "model/scale/musical_key.h"
#include "model/scale/preset_scales.h"

TEST_GROUP(MatricealLaneTest){};

TEST(MatricealLaneTest, forwardWrap) {
	MatricealLane lane;
	lane.length = 3;
	lane.direction = LaneDirection::FORWARD;
	lane.values[0] = 10;
	lane.values[1] = 20;
	lane.values[2] = 30;
	lane.position = 0;

	CHECK_EQUAL(10, lane.currentValue());
	lane.advance();
	CHECK_EQUAL(20, lane.currentValue());
	lane.advance();
	CHECK_EQUAL(30, lane.currentValue());
	lane.advance();
	CHECK_EQUAL(10, lane.currentValue()); // wrapped
	CHECK_EQUAL(0, lane.position);
}

TEST(MatricealLaneTest, reverseWrap) {
	MatricealLane lane;
	lane.length = 3;
	lane.direction = LaneDirection::REVERSE;
	lane.values[0] = 10;
	lane.values[1] = 20;
	lane.values[2] = 30;
	lane.position = 2;

	CHECK_EQUAL(30, lane.currentValue());
	lane.advance();
	CHECK_EQUAL(20, lane.currentValue());
	lane.advance();
	CHECK_EQUAL(10, lane.currentValue());
	lane.advance();
	CHECK_EQUAL(30, lane.currentValue()); // wrapped
}

TEST(MatricealLaneTest, pingpong) {
	MatricealLane lane;
	lane.length = 4;
	lane.direction = LaneDirection::PINGPONG;
	lane.values[0] = 1;
	lane.values[1] = 2;
	lane.values[2] = 3;
	lane.values[3] = 4;
	lane.position = 0;
	lane.pingpongReversed = false;

	// Forward: 1, 2, 3, 4
	CHECK_EQUAL(1, lane.currentValue());
	lane.advance();
	CHECK_EQUAL(2, lane.currentValue());
	lane.advance();
	CHECK_EQUAL(3, lane.currentValue());
	lane.advance();
	CHECK_EQUAL(4, lane.currentValue());
	// Reverse: 3, 2, 1
	lane.advance();
	CHECK_EQUAL(3, lane.currentValue());
	lane.advance();
	CHECK_EQUAL(2, lane.currentValue());
	lane.advance();
	CHECK_EQUAL(1, lane.currentValue());
	// Forward again: 2, 3, 4
	lane.advance();
	CHECK_EQUAL(2, lane.currentValue());
}

TEST(MatricealLaneTest, singleStep) {
	MatricealLane lane;
	lane.length = 1;
	lane.direction = LaneDirection::FORWARD;
	lane.values[0] = 42;
	lane.position = 0;

	CHECK_EQUAL(42, lane.currentValue());
	lane.advance();
	CHECK_EQUAL(42, lane.currentValue());
	lane.advance();
	CHECK_EQUAL(42, lane.currentValue());
}

TEST(MatricealLaneTest, disabledLane) {
	MatricealLane lane;
	lane.length = 0; // disabled

	CHECK_EQUAL(0, lane.currentValue());
	lane.advance(); // should not crash
	CHECK_EQUAL(0, lane.currentValue());
}

TEST(MatricealLaneTest, reset) {
	MatricealLane lane;
	lane.length = 4;
	lane.direction = LaneDirection::FORWARD;
	lane.position = 3;
	lane.pingpongReversed = true;

	lane.reset();
	CHECK_EQUAL(0, lane.position);
	CHECK_EQUAL(false, lane.pingpongReversed);
}

TEST_GROUP(ScaleDegreeHelperTest){};

TEST(ScaleDegreeHelperTest, majorScalePositiveDegrees) {
	MusicalKey key;
	key.rootNote = 60;                             // C4
	key.modeNotes = presetScaleNotes[MAJOR_SCALE]; // {0,2,4,5,7,9,11}

	CHECK_EQUAL(2, scaleDegreeToSemitoneOffset(1, key));  // +1 degree = D = +2 semitones
	CHECK_EQUAL(4, scaleDegreeToSemitoneOffset(2, key));  // +2 degrees = E = +4 semitones
	CHECK_EQUAL(12, scaleDegreeToSemitoneOffset(7, key)); // +7 degrees = octave = +12 semitones
	CHECK_EQUAL(14, scaleDegreeToSemitoneOffset(8, key)); // +8 degrees = octave + D = +14 semitones
}

TEST(ScaleDegreeHelperTest, majorScaleNegativeDegrees) {
	MusicalKey key;
	key.rootNote = 60;
	key.modeNotes = presetScaleNotes[MAJOR_SCALE];

	CHECK_EQUAL(-1, scaleDegreeToSemitoneOffset(-1, key));  // -1 degree = B below = -1 semitone
	CHECK_EQUAL(-12, scaleDegreeToSemitoneOffset(-7, key)); // -7 degrees = -12 semitones
}

TEST(ScaleDegreeHelperTest, minorScale) {
	MusicalKey key;
	key.rootNote = 69;                             // A4
	key.modeNotes = presetScaleNotes[MINOR_SCALE]; // {0,2,3,5,7,8,10}

	CHECK_EQUAL(2, scaleDegreeToSemitoneOffset(1, key)); // A minor +1 = B = +2 semitones
	CHECK_EQUAL(3, scaleDegreeToSemitoneOffset(2, key)); // A minor +2 = C = +3 semitones
}

TEST(ScaleDegreeHelperTest, chromaticScale) {
	MusicalKey key;
	key.rootNote = 60;
	key.modeNotes.fill(); // all 12 notes

	for (int d = -12; d <= 12; d++) {
		CHECK_EQUAL(d, scaleDegreeToSemitoneOffset(d, key));
	}
}

TEST(ScaleDegreeHelperTest, pentatonicScale) {
	MusicalKey key;
	key.rootNote = 60;
	key.modeNotes = presetScaleNotes[PENTATONIC_MINOR_SCALE]; // {0,3,5,7,10}

	CHECK_EQUAL(3, scaleDegreeToSemitoneOffset(1, key));  // +1 degree = minor 3rd = +3 semitones
	CHECK_EQUAL(12, scaleDegreeToSemitoneOffset(5, key)); // +5 degrees = octave for 5-note scale
}

TEST(ScaleDegreeHelperTest, zeroDegree) {
	MusicalKey key;
	key.rootNote = 60;
	key.modeNotes = presetScaleNotes[MAJOR_SCALE];

	CHECK_EQUAL(0, scaleDegreeToSemitoneOffset(0, key));
}

TEST_GROUP(MatricealNoteTest){};

TEST(MatricealNoteTest, restNote) {
	MatricealNote note = MatricealNote::rest();
	CHECK_EQUAL(true, note.isRest());
	CHECK_EQUAL(-1, note.noteCode);
}

TEST(MatricealNoteTest, validNote) {
	MatricealNote note;
	note.noteCode = 60;
	note.velocity = 100;
	note.gate = 64;
	note.retrigger = 0;
	note.glide = false;

	CHECK_EQUAL(false, note.isRest());
	CHECK_EQUAL(60, note.noteCode);
}

TEST_GROUP(MatricealEngineTest) {
	MatricealEngine engine;
	MusicalKey cMajor;

	void setup() {
		engine = MatricealEngine();
		cMajor.rootNote = 60;
		cMajor.modeNotes = presetScaleNotes[MAJOR_SCALE];
	}
};

TEST(MatricealEngineTest, allRestsWhenTriggerAllZeros) {
	engine.trigger.length = 4;
	engine.trigger.values[0] = 0;
	engine.trigger.values[1] = 0;
	engine.trigger.values[2] = 0;
	engine.trigger.values[3] = 0;

	engine.pitch.length = 2;
	engine.pitch.values[0] = 60;
	engine.pitch.values[1] = 64;

	for (int i = 0; i < 8; i++) {
		MatricealNote note = engine.step(cMajor);
		CHECK(note.isRest());
	}
}

TEST(MatricealEngineTest, triggerGatesOtherLanes) {
	engine.trigger.length = 4;
	engine.trigger.values[0] = 1;
	engine.trigger.values[1] = 0;
	engine.trigger.values[2] = 1;
	engine.trigger.values[3] = 0;

	engine.pitch.length = 3;
	engine.pitch.values[0] = 60;
	engine.pitch.values[1] = 64;
	engine.pitch.values[2] = 67;

	// Step 0: trigger=1, pitch[0]=60
	MatricealNote n0 = engine.step(cMajor);
	CHECK_EQUAL(60, n0.noteCode);

	// Step 1: trigger=0, rest, pitch does NOT advance
	MatricealNote n1 = engine.step(cMajor);
	CHECK(n1.isRest());

	// Step 2: trigger=1, pitch[1]=64
	MatricealNote n2 = engine.step(cMajor);
	CHECK_EQUAL(64, n2.noteCode);

	// Step 3: trigger=0, rest
	MatricealNote n3 = engine.step(cMajor);
	CHECK(n3.isRest());

	// Step 4: trigger wraps to [0]=1, pitch[2]=67
	MatricealNote n4 = engine.step(cMajor);
	CHECK_EQUAL(67, n4.noteCode);
}

TEST(MatricealEngineTest, velocityFromLane) {
	engine.trigger.length = 2;
	engine.trigger.values[0] = 1;
	engine.trigger.values[1] = 1;

	engine.pitch.length = 1;
	engine.pitch.values[0] = 60;

	engine.velocity.length = 2;
	engine.velocity.values[0] = 100;
	engine.velocity.values[1] = 50;

	MatricealNote n0 = engine.step(cMajor);
	CHECK_EQUAL(100, n0.velocity);

	MatricealNote n1 = engine.step(cMajor);
	CHECK_EQUAL(50, n1.velocity);
}

TEST(MatricealEngineTest, gateFromLane) {
	engine.trigger.length = 1;
	engine.trigger.values[0] = 1;

	engine.pitch.length = 1;
	engine.pitch.values[0] = 60;

	engine.gate.length = 3;
	engine.gate.values[0] = 32;
	engine.gate.values[1] = 96;
	engine.gate.values[2] = 64;

	CHECK_EQUAL(32, engine.step(cMajor).gate);
	CHECK_EQUAL(96, engine.step(cMajor).gate);
	CHECK_EQUAL(64, engine.step(cMajor).gate);
}

TEST(MatricealEngineTest, retriggerFromLane) {
	engine.trigger.length = 1;
	engine.trigger.values[0] = 1;

	engine.pitch.length = 1;
	engine.pitch.values[0] = 60;

	engine.retrigger.length = 2;
	engine.retrigger.values[0] = 0;
	engine.retrigger.values[1] = 3;

	CHECK_EQUAL(0, engine.step(cMajor).retrigger);
	CHECK_EQUAL(3, engine.step(cMajor).retrigger);
}

TEST(MatricealEngineTest, glideFromLane) {
	engine.trigger.length = 1;
	engine.trigger.values[0] = 1;

	engine.pitch.length = 1;
	engine.pitch.values[0] = 60;

	engine.glide.length = 2;
	engine.glide.values[0] = 0;
	engine.glide.values[1] = 1;

	CHECK_EQUAL(false, engine.step(cMajor).glide);
	CHECK_EQUAL(true, engine.step(cMajor).glide);
}

TEST(MatricealEngineTest, defaultLanesProduceDefaults) {
	engine.trigger.length = 1;
	engine.trigger.values[0] = 1;

	engine.pitch.length = 1;
	engine.pitch.values[0] = 60;

	MatricealNote note = engine.step(cMajor);
	CHECK_EQUAL(60, note.noteCode);
	CHECK_EQUAL(100, note.velocity); // default
	CHECK_EQUAL(64, note.gate);      // default
	CHECK_EQUAL(0, note.retrigger);  // default
	CHECK_EQUAL(false, note.glide);  // default
}

TEST(MatricealEngineTest, probabilityZeroKillsNote) {
	engine.trigger.length = 1;
	engine.trigger.values[0] = 1;

	engine.pitch.length = 1;
	engine.pitch.values[0] = 60;

	engine.probability.length = 2;
	engine.probability.values[0] = 0;  // never fire
	engine.probability.values[1] = 50; // always fire (placeholder logic)

	MatricealNote n0 = engine.step(cMajor);
	CHECK(n0.isRest()); // probability 0 = rest

	MatricealNote n1 = engine.step(cMajor);
	CHECK_EQUAL(60, n1.noteCode); // probability 50 = fires
}

TEST(MatricealEngineTest, resetClearsAllPositions) {
	engine.trigger.length = 4;
	engine.trigger.values[0] = 1;
	engine.trigger.values[1] = 1;
	engine.trigger.values[2] = 1;
	engine.trigger.values[3] = 1;
	engine.pitch.length = 3;
	engine.pitch.values[0] = 60;
	engine.pitch.values[1] = 64;
	engine.pitch.values[2] = 67;

	engine.step(cMajor); // advance both
	engine.step(cMajor); // advance both

	engine.reset();

	MatricealNote n = engine.step(cMajor);
	CHECK_EQUAL(60, n.noteCode); // back to start
}
