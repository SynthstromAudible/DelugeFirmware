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
