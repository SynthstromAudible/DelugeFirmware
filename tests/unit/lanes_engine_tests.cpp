#include "CppUTest/TestHarness.h"
#include "model/sequencing/lanes_engine.h"

#include "model/scale/musical_key.h"
#include "model/scale/preset_scales.h"

TEST_GROUP(LanesLaneTest){};

TEST(LanesLaneTest, forwardWrap) {
	LanesLane lane;
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

TEST(LanesLaneTest, reverseWrap) {
	LanesLane lane;
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

TEST(LanesLaneTest, pingpong) {
	LanesLane lane;
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

TEST(LanesLaneTest, singleStep) {
	LanesLane lane;
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

TEST(LanesLaneTest, disabledLane) {
	LanesLane lane;
	lane.length = 0; // disabled

	CHECK_EQUAL(0, lane.currentValue());
	lane.advance(); // should not crash
	CHECK_EQUAL(0, lane.currentValue());
}

TEST(LanesLaneTest, reset) {
	LanesLane lane;
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

TEST_GROUP(LanesNoteTest){};

TEST(LanesNoteTest, restNote) {
	LanesNote note = LanesNote::rest();
	CHECK_EQUAL(true, note.isRest());
	CHECK_EQUAL(-1, note.noteCode);
}

TEST(LanesNoteTest, validNote) {
	LanesNote note;
	note.noteCode = 60;
	note.velocity = 100;
	note.gate = 64;
	note.retrigger = 0;
	note.glide = false;

	CHECK_EQUAL(false, note.isRest());
	CHECK_EQUAL(60, note.noteCode);
}

// All engine tests use baseNote=60 (C4) and pitch values as scale-degree offsets.
// C major scale: C=0, D=2, E=4, F=5, G=7, A=9, B=11
// So scale degree 0=C(60), 1=D(62), 2=E(64), 3=F(65), 4=G(67), 5=A(69), 6=B(71), 7=C5(72)
TEST_GROUP(LanesEngineTest) {
	LanesEngine engine;
	MusicalKey cMajor;

	void setup() {
		engine = LanesEngine();
		cMajor.rootNote = 60;
		cMajor.modeNotes = presetScaleNotes[MAJOR_SCALE];
	}
};

TEST(LanesEngineTest, allRestsWhenTriggerAllZeros) {
	engine.trigger.length = 4;
	engine.trigger.values[0] = 0;
	engine.trigger.values[1] = 0;
	engine.trigger.values[2] = 0;
	engine.trigger.values[3] = 0;

	engine.pitch.length = 2;
	engine.pitch.values[0] = 0; // base note
	engine.pitch.values[1] = 2; // +2 degrees

	for (int i = 0; i < 8; i++) {
		LanesNote note = engine.step(cMajor);
		CHECK(note.isRest());
	}
}

TEST(LanesEngineTest, triggerGatesOutputNotAdvancement) {
	engine.trigger.length = 4;
	engine.trigger.values[0] = 1;
	engine.trigger.values[1] = 0;
	engine.trigger.values[2] = 1;
	engine.trigger.values[3] = 0;

	// Pitch as scale degrees: 0=C(60), 2=E(64), 4=G(67)
	engine.pitch.length = 3;
	engine.pitch.values[0] = 0; // C4 = 60
	engine.pitch.values[1] = 2; // E4 = 64
	engine.pitch.values[2] = 4; // G4 = 67

	// All lanes free-run every tick; trigger only gates output

	// Step 0: trigger=1, pitch[0]=0 → C4(60)
	LanesNote n0 = engine.step(cMajor);
	CHECK_EQUAL(60, n0.noteCode);

	// Step 1: trigger=0 → rest (pitch advances to [1] but output is gated)
	LanesNote n1 = engine.step(cMajor);
	CHECK(n1.isRest());

	// Step 2: trigger=1, pitch[2]=4 → G4(67)
	LanesNote n2 = engine.step(cMajor);
	CHECK_EQUAL(67, n2.noteCode);

	// Step 3: trigger=0 → rest (pitch wraps to [0])
	LanesNote n3 = engine.step(cMajor);
	CHECK(n3.isRest());

	// Step 4: trigger wraps to [0]=1, pitch[1]=2 → E4(64)
	LanesNote n4 = engine.step(cMajor);
	CHECK_EQUAL(64, n4.noteCode);
}

TEST(LanesEngineTest, trackLengthResetsAllLanes) {
	engine.trackLength = 4;

	engine.trigger.length = 4;
	engine.trigger.values[0] = 1;
	engine.trigger.values[1] = 1;
	engine.trigger.values[2] = 1;
	engine.trigger.values[3] = 1;

	engine.pitch.length = 4;
	engine.pitch.values[0] = 0; // C4 = 60
	engine.pitch.values[1] = 2; // E4 = 64
	engine.pitch.values[2] = 4; // G4 = 67
	engine.pitch.values[3] = 7; // C5 = 72

	// Play through 4 steps
	CHECK_EQUAL(60, engine.step(cMajor).noteCode);
	CHECK_EQUAL(64, engine.step(cMajor).noteCode);
	CHECK_EQUAL(67, engine.step(cMajor).noteCode);
	CHECK_EQUAL(72, engine.step(cMajor).noteCode);

	// Step 5: trackLength reached, all lanes reset → back to step 0
	CHECK_EQUAL(60, engine.step(cMajor).noteCode);
	CHECK_EQUAL(64, engine.step(cMajor).noteCode);
}

TEST(LanesEngineTest, velocityFromLane) {
	engine.trigger.length = 2;
	engine.trigger.values[0] = 1;
	engine.trigger.values[1] = 1;

	engine.pitch.length = 1;
	engine.pitch.values[0] = 0; // C4

	engine.velocity.length = 2;
	engine.velocity.values[0] = 100;
	engine.velocity.values[1] = 50;

	LanesNote n0 = engine.step(cMajor);
	CHECK_EQUAL(100, n0.velocity);

	LanesNote n1 = engine.step(cMajor);
	CHECK_EQUAL(50, n1.velocity);
}

TEST(LanesEngineTest, gateFromLane) {
	engine.trigger.length = 1;
	engine.trigger.values[0] = 1;

	engine.pitch.length = 1;
	engine.pitch.values[0] = 0; // C4

	engine.gate.length = 3;
	engine.gate.values[0] = 32;
	engine.gate.values[1] = 96;
	engine.gate.values[2] = 64;

	CHECK_EQUAL(32, engine.step(cMajor).gate);
	CHECK_EQUAL(96, engine.step(cMajor).gate);
	CHECK_EQUAL(64, engine.step(cMajor).gate);
}

TEST(LanesEngineTest, retriggerFromLane) {
	engine.trigger.length = 1;
	engine.trigger.values[0] = 1;

	engine.pitch.length = 1;
	engine.pitch.values[0] = 0;

	engine.retrigger.length = 2;
	engine.retrigger.values[0] = 0;
	engine.retrigger.values[1] = 3;

	CHECK_EQUAL(0, engine.step(cMajor).retrigger);
	CHECK_EQUAL(3, engine.step(cMajor).retrigger);
}

TEST(LanesEngineTest, glideFromLane) {
	engine.trigger.length = 1;
	engine.trigger.values[0] = 1;

	engine.pitch.length = 1;
	engine.pitch.values[0] = 0;

	engine.glide.length = 2;
	engine.glide.values[0] = 0;
	engine.glide.values[1] = 1;

	CHECK_EQUAL(false, engine.step(cMajor).glide);
	CHECK_EQUAL(true, engine.step(cMajor).glide);
}

TEST(LanesEngineTest, defaultLanesProduceDefaults) {
	engine.trigger.length = 1;
	engine.trigger.values[0] = 1;

	engine.pitch.length = 1;
	engine.pitch.values[0] = 0; // base note

	LanesNote note = engine.step(cMajor);
	CHECK_EQUAL(60, note.noteCode);  // baseNote + degree 0
	CHECK_EQUAL(100, note.velocity); // default
	CHECK_EQUAL(75, note.gate);      // kDefaultGate
	CHECK_EQUAL(0, note.retrigger);  // default
	CHECK_EQUAL(false, note.glide);  // default
}

TEST(LanesEngineTest, probabilityZeroKillsNote) {
	engine.trigger.length = 1;
	engine.trigger.values[0] = 1;

	engine.pitch.length = 1;
	engine.pitch.values[0] = 0;

	engine.probability.length = 2;
	engine.probability.values[0] = 0;   // never fire
	engine.probability.values[1] = 100; // always fire

	LanesNote n0 = engine.step(cMajor);
	CHECK(n0.isRest()); // probability 0 = rest

	LanesNote n1 = engine.step(cMajor);
	CHECK_EQUAL(60, n1.noteCode); // probability 100 = fires
}

TEST(LanesEngineTest, resetClearsAllPositions) {
	engine.trigger.length = 4;
	engine.trigger.values[0] = 1;
	engine.trigger.values[1] = 1;
	engine.trigger.values[2] = 1;
	engine.trigger.values[3] = 1;
	// Pitch as scale degrees: 0=C(60), 2=E(64), 4=G(67)
	engine.pitch.length = 3;
	engine.pitch.values[0] = 0;
	engine.pitch.values[1] = 2;
	engine.pitch.values[2] = 4;

	engine.step(cMajor); // advance both
	engine.step(cMajor); // advance both

	engine.reset();

	LanesNote n = engine.step(cMajor);
	CHECK_EQUAL(60, n.noteCode); // back to start: degree 0 = C4
}

TEST(LanesEngineTest, intervalAccumulatesAcrossSteps) {
	engine.trigger.length = 1;
	engine.trigger.values[0] = 1;
	engine.pitch.length = 1;
	engine.pitch.values[0] = 0; // C4 base
	engine.interval.length = 3;
	engine.interval.values[0] = 2;
	engine.interval.values[1] = -1;
	engine.interval.values[2] = 3;

	// C major: degree offsets via scaleDegreeToSemitoneOffset
	// Step 0: pitch=0(C4=60), interval acc=+2 → +4 semi → 64 (E4)
	CHECK_EQUAL(64, engine.step(cMajor).noteCode);
	// Step 1: pitch=0(60), interval acc=+1 → +2 semi → 62 (D4)
	CHECK_EQUAL(62, engine.step(cMajor).noteCode);
	// Step 2: pitch=0(60), interval acc=+4 → +7 semi → 67 (G4)
	CHECK_EQUAL(67, engine.step(cMajor).noteCode);
	// Step 3: pitch=0(60), interval acc=+6 → +11 semi → 71 (B4)
	CHECK_EQUAL(71, engine.step(cMajor).noteCode);
}

TEST(LanesEngineTest, intervalWrapsAtBoundary) {
	engine.trigger.length = 1;
	engine.trigger.values[0] = 1;
	engine.pitch.length = 1;
	engine.pitch.values[0] = 0;
	engine.interval.length = 1;
	engine.interval.values[0] = 5;
	engine.intervalMax = 7;
	engine.intervalMin = -7;

	// acc: 5, wraps on next (10 > 7 → jumps to -7)
	LanesNote n0 = engine.step(cMajor);
	CHECK(n0.noteCode > 60); // acc=5, positive offset

	LanesNote n1 = engine.step(cMajor);
	// acc would be 10, exceeds max(7) → wraps to min(-7)
	CHECK_EQUAL(-7, engine.intervalAccumulator);
}

TEST(LanesEngineTest, intervalResetsOnEngineReset) {
	engine.trigger.length = 1;
	engine.trigger.values[0] = 1;
	engine.pitch.length = 1;
	engine.pitch.values[0] = 0;
	engine.interval.length = 1;
	engine.interval.values[0] = 2;

	engine.step(cMajor);
	engine.step(cMajor);
	engine.reset();
	CHECK_EQUAL(0, engine.intervalAccumulator);
	// acc=+2 → scaleDegreeToSemitoneOffset(2, cMajor) = +4 → 60+4 = 64 (E4)
	CHECK_EQUAL(64, engine.step(cMajor).noteCode);
}

TEST(LanesEngineTest, octaveShift) {
	engine.trigger.length = 1;
	engine.trigger.values[0] = 1;
	engine.pitch.length = 1;
	engine.pitch.values[0] = 0; // C4 = 60
	engine.octave.length = 3;
	engine.octave.values[0] = 0;
	engine.octave.values[1] = 1;
	engine.octave.values[2] = -1;

	CHECK_EQUAL(60, engine.step(cMajor).noteCode); // C4
	CHECK_EQUAL(72, engine.step(cMajor).noteCode); // C5
	CHECK_EQUAL(48, engine.step(cMajor).noteCode); // C3
}

TEST(LanesEngineTest, pitchIntervalAndOctaveCombined) {
	engine.trigger.length = 1;
	engine.trigger.values[0] = 1;
	engine.pitch.length = 1;
	engine.pitch.values[0] = 0; // C4 = 60
	engine.interval.length = 1;
	engine.interval.values[0] = 2; // acc=+2 → +4 semitones in C major
	engine.octave.length = 1;
	engine.octave.values[0] = 1; // +12 semitones

	// pitch: 60, interval: +4 semi, octave: +12 → 60+4+12=76
	CHECK_EQUAL(76, engine.step(cMajor).noteCode);
}

TEST(LanesEngineTest, noteClampedToMidiRange) {
	engine.trigger.length = 1;
	engine.trigger.values[0] = 1;
	engine.pitch.length = 1;
	engine.pitch.values[0] = 20; // high degree offset
	engine.octave.length = 1;
	engine.octave.values[0] = 4; // +48

	LanesNote n = engine.step(cMajor);
	CHECK(n.noteCode <= 127);

	engine.reset();
	engine.pitch.values[0] = -20; // low degree offset
	engine.octave.values[0] = -4;
	n = engine.step(cMajor);
	CHECK(n.noteCode >= 0);
}

TEST(LanesEngineTest, probabilityZeroAlwaysRest) {
	engine.trigger.length = 1;
	engine.trigger.values[0] = 1;
	engine.pitch.length = 1;
	engine.pitch.values[0] = 0;
	engine.probability.length = 1;
	engine.probability.values[0] = 0;

	for (int i = 0; i < 10; i++) {
		CHECK(engine.step(cMajor).isRest());
	}
}

TEST(LanesEngineTest, probabilityHundredAlwaysPlays) {
	engine.trigger.length = 1;
	engine.trigger.values[0] = 1;
	engine.pitch.length = 1;
	engine.pitch.values[0] = 0;
	engine.probability.length = 1;
	engine.probability.values[0] = 100;

	for (int i = 0; i < 10; i++) {
		CHECK_EQUAL(false, engine.step(cMajor).isRest());
	}
}

TEST(LanesEngineTest, probabilityFiftyProducesMix) {
	engine.trigger.length = 1;
	engine.trigger.values[0] = 1;
	engine.pitch.length = 1;
	engine.pitch.values[0] = 0;
	engine.probability.length = 1;
	engine.probability.values[0] = 50;

	engine.setSeed(42);

	int notes = 0, rests = 0;
	for (int i = 0; i < 100; i++) {
		if (engine.step(cMajor).isRest())
			rests++;
		else
			notes++;
	}
	CHECK(notes > 20);
	CHECK(rests > 20);
}

TEST(LanesEngineTest, probabilityLanesStillAdvanceOnProbRest) {
	engine.trigger.length = 1;
	engine.trigger.values[0] = 1;
	// Pitch degrees: 0=C(60), 2=E(64), 4=G(67)
	engine.pitch.length = 3;
	engine.pitch.values[0] = 0;
	engine.pitch.values[1] = 2;
	engine.pitch.values[2] = 4;
	engine.probability.length = 3;
	engine.probability.values[0] = 100;
	engine.probability.values[1] = 0;
	engine.probability.values[2] = 100;

	LanesNote n0 = engine.step(cMajor);
	CHECK_EQUAL(60, n0.noteCode); // degree 0 = C4

	LanesNote n1 = engine.step(cMajor);
	CHECK(n1.isRest()); // prob=0

	LanesNote n2 = engine.step(cMajor);
	CHECK_EQUAL(67, n2.noteCode); // degree 4 = G4 (pitch advanced past degree 2)
}

TEST(LanesEngineTest, lockProducesIdenticalOutput) {
	engine.trigger.length = 4;
	for (int i = 0; i < 4; i++)
		engine.trigger.values[i] = 1;
	engine.pitch.length = 3;
	engine.pitch.values[0] = 0;
	engine.pitch.values[1] = 2;
	engine.pitch.values[2] = 4;
	engine.probability.length = 1;
	engine.probability.values[0] = 50;

	engine.setSeed(123);
	engine.lock();

	int16_t firstCycle[12];
	for (int i = 0; i < 12; i++) {
		firstCycle[i] = engine.step(cMajor).noteCode;
	}

	engine.reset();
	for (int i = 0; i < 12; i++) {
		CHECK_EQUAL(firstCycle[i], engine.step(cMajor).noteCode);
	}
}

TEST(LanesEngineTest, unlockAllowsVariation) {
	engine.trigger.length = 1;
	engine.trigger.values[0] = 1;
	engine.pitch.length = 1;
	engine.pitch.values[0] = 0;
	engine.probability.length = 1;
	engine.probability.values[0] = 50;

	engine.setSeed(123);
	engine.lock();
	for (int i = 0; i < 10; i++)
		engine.step(cMajor);

	engine.unlock();
	engine.reset();
	for (int i = 0; i < 10; i++)
		engine.step(cMajor); // should not crash
}

TEST(LanesEngineTest, polymetricPhasingThreeByFive) {
	// 3-step pitch x 5-step trigger = 15 unique steps before repeat
	engine.trigger.length = 5;
	for (int i = 0; i < 5; i++)
		engine.trigger.values[i] = 1;

	// Pitch as scale degrees: 0=C(60), 2=E(64), 4=G(67)
	engine.pitch.length = 3;
	engine.pitch.values[0] = 0; // C4
	engine.pitch.values[1] = 2; // E4
	engine.pitch.values[2] = 4; // G4

	// Collect 15 steps
	int16_t cycle1[15];
	for (int i = 0; i < 15; i++) {
		cycle1[i] = engine.step(cMajor).noteCode;
	}

	// Expected pattern: C E G C E | G C E G C | E G C E G
	CHECK_EQUAL(60, cycle1[0]);
	CHECK_EQUAL(64, cycle1[1]);
	CHECK_EQUAL(67, cycle1[2]);
	CHECK_EQUAL(60, cycle1[3]);
	CHECK_EQUAL(64, cycle1[4]);
	CHECK_EQUAL(67, cycle1[5]);
	CHECK_EQUAL(60, cycle1[6]);
	CHECK_EQUAL(64, cycle1[7]);

	// Verify second cycle is identical
	int16_t cycle2[15];
	for (int i = 0; i < 15; i++) {
		cycle2[i] = engine.step(cMajor).noteCode;
	}
	for (int i = 0; i < 15; i++) {
		CHECK_EQUAL(cycle1[i], cycle2[i]);
	}
}

TEST(LanesEngineTest, polymetricVelocityPhasing) {
	engine.trigger.length = 1;
	engine.trigger.values[0] = 1;

	// Pitch degrees: 0=C(60), 2=E(64)
	engine.pitch.length = 2;
	engine.pitch.values[0] = 0;
	engine.pitch.values[1] = 2;

	engine.velocity.length = 3;
	engine.velocity.values[0] = 100;
	engine.velocity.values[1] = 80;
	engine.velocity.values[2] = 60;

	// LCM(2,3) = 6 steps to repeat
	struct {
		int16_t note;
		uint8_t vel;
	} expected[6] = {{60, 100}, {64, 80}, {60, 60}, {64, 100}, {60, 80}, {64, 60}};

	for (int i = 0; i < 6; i++) {
		LanesNote n = engine.step(cMajor);
		CHECK_EQUAL(expected[i].note, n.noteCode);
		CHECK_EQUAL(expected[i].vel, n.velocity);
	}

	// Verify repeat
	for (int i = 0; i < 6; i++) {
		LanesNote n = engine.step(cMajor);
		CHECK_EQUAL(expected[i].note, n.noteCode);
		CHECK_EQUAL(expected[i].vel, n.velocity);
	}
}

TEST(LanesEngineTest, freeRunningLanesWithSparseTriggger) {
	engine.trigger.length = 4;
	engine.trigger.values[0] = 1;
	engine.trigger.values[1] = 0;
	engine.trigger.values[2] = 0;
	engine.trigger.values[3] = 1;

	// Pitch degrees: 0=C(60), 7=C5(72)
	engine.pitch.length = 2;
	engine.pitch.values[0] = 0;
	engine.pitch.values[1] = 7; // one octave up in 7-note scale

	// All lanes advance every tick. Pitch wraps: 0,7,0,7,...
	// Step 0: trig=1, pitch[0]=0 → 60
	CHECK_EQUAL(60, engine.step(cMajor).noteCode);
	// Step 1: trig=0 → rest (pitch advances to 7)
	CHECK(engine.step(cMajor).isRest());
	// Step 2: trig=0 → rest (pitch wraps to 0)
	CHECK(engine.step(cMajor).isRest());
	// Step 3: trig=1, pitch[1]=7 → 72
	CHECK_EQUAL(72, engine.step(cMajor).noteCode);
}

TEST(LanesEngineTest, allLanesActivePolymetric) {
	engine.trigger.length = 2;
	engine.trigger.values[0] = 1;
	engine.trigger.values[1] = 1;

	// Pitch degrees: 0=C(60), 2=E(64), 4=G(67)
	engine.pitch.length = 3;
	engine.pitch.values[0] = 0;
	engine.pitch.values[1] = 2;
	engine.pitch.values[2] = 4;

	engine.velocity.length = 2;
	engine.velocity.values[0] = 100;
	engine.velocity.values[1] = 50;

	engine.gate.length = 2;
	engine.gate.values[0] = 32;
	engine.gate.values[1] = 96;

	engine.octave.length = 1;
	engine.octave.values[0] = 0;

	engine.retrigger.length = 1;
	engine.retrigger.values[0] = 0;

	engine.glide.length = 2;
	engine.glide.values[0] = 0;
	engine.glide.values[1] = 1;

	int16_t expectedPitch[] = {60, 64, 67, 60, 64, 67};
	for (int i = 0; i < 6; i++) {
		LanesNote n = engine.step(cMajor);
		CHECK_EQUAL(false, n.isRest());
		CHECK_EQUAL(expectedPitch[i], n.noteCode);
	}
}

// --- Scale correctness tests ---

TEST(LanesEngineTest, pitchAlwaysLandsOnScale_CMajor) {
	engine.trigger.length = 1;
	engine.trigger.values[0] = 1;
	engine.pitch.length = 7;
	// All scale degrees from -3 to +3
	engine.pitch.values[0] = -3;
	engine.pitch.values[1] = -2;
	engine.pitch.values[2] = -1;
	engine.pitch.values[3] = 0;
	engine.pitch.values[4] = 1;
	engine.pitch.values[5] = 2;
	engine.pitch.values[6] = 3;

	for (int i = 0; i < 7; i++) {
		LanesNote n = engine.step(cMajor);
		// Note should be in C major scale: C D E F G A B (semitones 0,2,4,5,7,9,11)
		int semitoneInOctave = ((n.noteCode % 12) + 12) % 12;
		CHECK(cMajor.modeNotes.has(semitoneInOctave));
	}
}

TEST(LanesEngineTest, pitchAndIntervalAlwaysOnScale) {
	engine.trigger.length = 1;
	engine.trigger.values[0] = 1;
	engine.pitch.length = 5;
	engine.pitch.values[0] = 0;
	engine.pitch.values[1] = 1;
	engine.pitch.values[2] = 2;
	engine.pitch.values[3] = 3;
	engine.pitch.values[4] = 4;
	engine.interval.length = 3;
	engine.interval.values[0] = 1;
	engine.interval.values[1] = -1;
	engine.interval.values[2] = 2;

	for (int i = 0; i < 30; i++) {
		LanesNote n = engine.step(cMajor);
		if (!n.isRest()) {
			int semitoneInOctave = ((n.noteCode % 12) + 12) % 12;
			CHECK(cMajor.modeNotes.has(semitoneInOctave));
		}
	}
}

TEST(LanesEngineTest, pitchIntervalOctaveAlwaysOnScale) {
	engine.trigger.length = 1;
	engine.trigger.values[0] = 1;
	engine.pitch.length = 4;
	engine.pitch.values[0] = 0;
	engine.pitch.values[1] = 2;
	engine.pitch.values[2] = 4;
	engine.pitch.values[3] = 6;
	engine.interval.length = 2;
	engine.interval.values[0] = 1;
	engine.interval.values[1] = -2;
	engine.octave.length = 3;
	engine.octave.values[0] = 0;
	engine.octave.values[1] = 1;
	engine.octave.values[2] = -1;

	for (int i = 0; i < 50; i++) {
		LanesNote n = engine.step(cMajor);
		if (!n.isRest()) {
			int semitoneInOctave = ((n.noteCode % 12) + 12) % 12;
			CHECK(cMajor.modeNotes.has(semitoneInOctave));
		}
	}
}

TEST(LanesEngineTest, pitchOnScale_MinorPentatonic) {
	MusicalKey pentatonic;
	pentatonic.rootNote = 60;
	pentatonic.modeNotes = presetScaleNotes[PENTATONIC_MINOR_SCALE]; // {0,3,5,7,10}

	engine.trigger.length = 1;
	engine.trigger.values[0] = 1;
	engine.pitch.length = 5;
	for (int i = 0; i < 5; i++)
		engine.pitch.values[i] = i;

	for (int i = 0; i < 20; i++) {
		LanesNote n = engine.step(pentatonic);
		if (!n.isRest()) {
			int semitoneInOctave = ((n.noteCode % 12) + 12) % 12;
			CHECK(pentatonic.modeNotes.has(semitoneInOctave));
		}
	}
}

TEST(LanesEngineTest, intervalChromaticModeBypassesScale) {
	engine.intervalScaleAware = false;
	engine.trigger.length = 1;
	engine.trigger.values[0] = 1;
	engine.pitch.length = 1;
	engine.pitch.values[0] = 0; // C4
	engine.interval.length = 1;
	engine.interval.values[0] = 1; // +1 chromatic semitone per step

	// First step: acc=1, chromatic +1 → C# (61), which is NOT in C major
	LanesNote n = engine.step(cMajor);
	CHECK_EQUAL(61, n.noteCode); // C# — intentionally chromatic
}

TEST(LanesEngineTest, defaultConstructorInitsProbabilityAndGate) {
	// Fresh engine should have probability=100, gate=75, velocity=100 for all steps
	LanesEngine fresh;
	for (int i = 0; i < kMaxLanesSteps; i++) {
		CHECK_EQUAL(100, fresh.probability.values[i]);
		CHECK_EQUAL(75, fresh.gate.values[i]);
		CHECK_EQUAL(100, fresh.velocity.values[i]);
	}
}

TEST(LanesEngineTest, clockDivisionSlowsLane) {
	// Pitch with division=2 should advance every 2 engine steps
	engine.trigger.length = 1;
	engine.trigger.values[0] = 1;

	engine.pitch.length = 3;
	engine.pitch.values[0] = 0; // C
	engine.pitch.values[1] = 2; // E
	engine.pitch.values[2] = 4; // G
	engine.pitch.clockDivision = 2;
	engine.pitch.divisionCounter = 2;

	// Step 1: pitch reads position 0 (degree 0 = C4), then advance with division → counter becomes 1, no advance
	LanesNote n1 = engine.step(cMajor);
	CHECK_EQUAL(60, n1.noteCode); // C4

	// Step 2: pitch reads position 0 again (didn't advance), then advance → counter becomes 0 → advance to pos 1
	LanesNote n2 = engine.step(cMajor);
	CHECK_EQUAL(60, n2.noteCode); // still C4 (read before advance)

	// Step 3: now position is 1 (degree 2 = E), read then advance → counter becomes 1, no advance
	LanesNote n3 = engine.step(cMajor);
	CHECK_EQUAL(64, n3.noteCode); // E4

	// Step 4: still position 1, advance → counter 0 → advance to pos 2
	LanesNote n4 = engine.step(cMajor);
	CHECK_EQUAL(64, n4.noteCode); // still E4

	// Step 5: now position is 2 (degree 4 = G)
	LanesNote n5 = engine.step(cMajor);
	CHECK_EQUAL(67, n5.noteCode); // G4
}

TEST(LanesEngineTest, clockDivisionOneIsDefault) {
	// Division=1 should behave identically to the old behavior
	engine.trigger.length = 1;
	engine.trigger.values[0] = 1;

	engine.pitch.length = 3;
	engine.pitch.values[0] = 0;
	engine.pitch.values[1] = 2;
	engine.pitch.values[2] = 4;
	engine.pitch.clockDivision = 1; // default

	CHECK_EQUAL(60, engine.step(cMajor).noteCode); // degree 0 = C4
	CHECK_EQUAL(64, engine.step(cMajor).noteCode); // degree 2 = E4
	CHECK_EQUAL(67, engine.step(cMajor).noteCode); // degree 4 = G4
	CHECK_EQUAL(60, engine.step(cMajor).noteCode); // wraps back to degree 0
}

// --- getLane mapping tests ---

TEST(LanesEngineTest, getLaneMapping) {
	CHECK(&engine.trigger == engine.getLane(0));
	CHECK(&engine.pitch == engine.getLane(1));
	CHECK(&engine.octave == engine.getLane(2));
	CHECK(&engine.velocity == engine.getLane(3));
	CHECK(&engine.gate == engine.getLane(4));
	CHECK(&engine.interval == engine.getLane(5));
	CHECK(&engine.retrigger == engine.getLane(6));
	CHECK(&engine.probability == engine.getLane(7));
	CHECK(&engine.glide == engine.getLane(8));
	CHECK(nullptr == engine.getLane(-1));
	CHECK(nullptr == engine.getLane(9));
	CHECK(nullptr == engine.getLane(100));
}

// --- generateEuclidean tests ---

TEST(LanesEngineTest, euclideanFourOfSixteen) {
	engine.trigger.length = 16;
	engine.generateEuclidean(16, 4);
	int count = 0;
	for (int i = 0; i < 16; i++) {
		if (engine.trigger.values[i] != 0)
			count++;
	}
	CHECK_EQUAL(4, count);
}

TEST(LanesEngineTest, euclideanAllPulses) {
	engine.trigger.length = 8;
	engine.generateEuclidean(8, 8);
	for (int i = 0; i < 8; i++) {
		CHECK_EQUAL(1, engine.trigger.values[i]);
	}
}

TEST(LanesEngineTest, euclideanZeroPulses) {
	engine.trigger.length = 8;
	// Pre-fill with 1s to verify they get cleared
	for (int i = 0; i < 8; i++)
		engine.trigger.values[i] = 1;
	engine.generateEuclidean(8, 0);
	for (int i = 0; i < 8; i++) {
		CHECK_EQUAL(0, engine.trigger.values[i]);
	}
}

TEST(LanesEngineTest, euclideanThreeOfEight) {
	// E(3,8) distributes 3 pulses across 8 steps as evenly as possible
	engine.trigger.length = 8;
	engine.generateEuclidean(8, 3);
	int count = 0;
	for (int i = 0; i < 8; i++) {
		if (engine.trigger.values[i] != 0)
			count++;
	}
	CHECK_EQUAL(3, count);
}

// --- rotateTriggerPattern tests ---

TEST(LanesEngineTest, rotateTriggerPatternForward) {
	engine.trigger.length = 4;
	engine.trigger.values[0] = 1;
	engine.trigger.values[1] = 0;
	engine.trigger.values[2] = 0;
	engine.trigger.values[3] = 0;

	engine.rotateTriggerPattern(1);
	CHECK_EQUAL(0, engine.trigger.values[0]);
	CHECK_EQUAL(1, engine.trigger.values[1]);
	CHECK_EQUAL(0, engine.trigger.values[2]);
	CHECK_EQUAL(0, engine.trigger.values[3]);
}

TEST(LanesEngineTest, rotateTriggerPatternBackward) {
	engine.trigger.length = 4;
	engine.trigger.values[0] = 0;
	engine.trigger.values[1] = 1;
	engine.trigger.values[2] = 0;
	engine.trigger.values[3] = 0;

	engine.rotateTriggerPattern(-1);
	CHECK_EQUAL(1, engine.trigger.values[0]);
	CHECK_EQUAL(0, engine.trigger.values[1]);
	CHECK_EQUAL(0, engine.trigger.values[2]);
	CHECK_EQUAL(0, engine.trigger.values[3]);
}

TEST(LanesEngineTest, rotateTriggerPatternFullCycle) {
	engine.trigger.length = 4;
	engine.trigger.values[0] = 1;
	engine.trigger.values[1] = 0;
	engine.trigger.values[2] = 1;
	engine.trigger.values[3] = 0;

	// Rotate by length should return to original
	engine.rotateTriggerPattern(4);
	CHECK_EQUAL(1, engine.trigger.values[0]);
	CHECK_EQUAL(0, engine.trigger.values[1]);
	CHECK_EQUAL(1, engine.trigger.values[2]);
	CHECK_EQUAL(0, engine.trigger.values[3]);
}

// --- randomInRange tests ---

TEST(LanesEngineTest, randomInRangeStaysInBounds) {
	engine.setSeed(42);
	for (int i = 0; i < 100; i++) {
		int16_t val = engine.randomInRange(10, 20);
		CHECK(val >= 10);
		CHECK(val <= 20);
	}
}

TEST(LanesEngineTest, randomInRangeEqualMinMax) {
	int16_t val = engine.randomInRange(5, 5);
	CHECK_EQUAL(5, val);
}

TEST(LanesEngineTest, randomInRangeMinGreaterThanMax) {
	// Should return minVal as a safe fallback (div-by-zero guard)
	int16_t val = engine.randomInRange(20, 10);
	CHECK_EQUAL(20, val);
}

TEST(LanesEngineTest, randomInRangeNegativeRange) {
	engine.setSeed(99);
	for (int i = 0; i < 50; i++) {
		int16_t val = engine.randomInRange(-10, -5);
		CHECK(val >= -10);
		CHECK(val <= -5);
	}
}

// --- clearLane / randomizeLane tests ---

TEST(LanesEngineTest, clearLaneSetsToZero) {
	engine.pitch.length = 4;
	engine.pitch.values[0] = 5;
	engine.pitch.values[1] = 10;
	engine.pitch.values[2] = 15;
	engine.pitch.values[3] = 20;

	engine.clearLane(1); // pitch = index 1
	for (int i = 0; i < 4; i++) {
		CHECK_EQUAL(0, engine.pitch.values[i]);
	}
}

TEST(LanesEngineTest, clearLaneRestoresProbability) {
	engine.pitch.length = 4;
	engine.pitch.stepProbability[0] = 50;
	engine.pitch.stepProbability[1] = 25;

	engine.clearLane(1); // pitch = index 1
	for (int i = 0; i < 4; i++) {
		CHECK_EQUAL(100, engine.pitch.stepProbability[i]);
	}
}

TEST(LanesEngineTest, clearLaneInvalidIndex) {
	// Should not crash
	engine.clearLane(-1);
	engine.clearLane(9);
	engine.clearLane(100);
}

TEST(LanesEngineTest, randomizeLaneStaysInBounds) {
	engine.setSeed(42);
	engine.velocity.length = 16;
	engine.randomizeLane(3); // velocity = index 3

	for (int i = 0; i < 16; i++) {
		CHECK(engine.velocity.values[i] >= LanesEngine::kLaneRndMins[3]);
		CHECK(engine.velocity.values[i] <= LanesEngine::kLaneRndMaxs[3]);
	}
}

TEST(LanesEngineTest, randomizeLaneInvalidIndex) {
	// Should not crash
	engine.randomizeLane(-1);
	engine.randomizeLane(9);
}

// --- Interval negative boundary tests ---

TEST(LanesEngineTest, intervalNegativeWrap) {
	engine.trigger.length = 1;
	engine.trigger.values[0] = 1;
	engine.pitch.length = 1;
	engine.pitch.values[0] = 0;
	engine.interval.length = 1;
	engine.interval.values[0] = -5;
	engine.intervalMax = 7;
	engine.intervalMin = -7;

	// acc: -5
	LanesNote n0 = engine.step(cMajor);
	CHECK(n0.noteCode < 60); // negative offset

	// acc would be -10, exceeds min(-7) → wraps to max(7)
	LanesNote n1 = engine.step(cMajor);
	CHECK_EQUAL(7, engine.intervalAccumulator);
}

TEST(LanesEngineTest, clockDivisionResetRestoresCounter) {
	engine.trigger.length = 1;
	engine.trigger.values[0] = 1;
	engine.pitch.length = 2;
	engine.pitch.values[0] = 0;
	engine.pitch.values[1] = 2;
	engine.pitch.clockDivision = 3;
	engine.pitch.divisionCounter = 3;

	// Step 3 times — pitch only advances once after 3 steps
	engine.step(cMajor); // reads pos 0, counter 3→2
	engine.step(cMajor); // reads pos 0, counter 2→1
	engine.step(cMajor); // reads pos 0, counter 1→0→reset to 3, advance to pos 1

	engine.reset();
	CHECK_EQUAL(0, engine.pitch.position);
	CHECK_EQUAL(3, engine.pitch.divisionCounter); // counter restored to clockDivision
}

// --- loadDefaultPattern tests ---

TEST(LanesEngineTest, loadDefaultPatternSetsAllLaneLengths) {
	engine.loadDefaultPattern();
	CHECK_EQUAL(16, engine.trigger.length);
	CHECK_EQUAL(16, engine.pitch.length);
	CHECK_EQUAL(16, engine.velocity.length);
	CHECK_EQUAL(16, engine.octave.length);
	CHECK_EQUAL(16, engine.gate.length);
	CHECK_EQUAL(16, engine.interval.length);
	CHECK_EQUAL(16, engine.retrigger.length);
	CHECK_EQUAL(16, engine.probability.length);
	CHECK_EQUAL(16, engine.glide.length);
}

TEST(LanesEngineTest, loadDefaultPatternTriggerIsEighthNotes) {
	engine.loadDefaultPattern();
	// 8th-note = every other step: 0,2,4,6,8,10,12,14 should be 1
	int32_t count = 0;
	for (int32_t i = 0; i < 16; i++) {
		if (engine.trigger.values[i] == 1) {
			CHECK(i % 2 == 0); // triggers only on even steps
			count++;
		}
	}
	CHECK_EQUAL(8, count);
}

TEST(LanesEngineTest, loadDefaultPatternPitchAscends) {
	engine.loadDefaultPattern();
	// Pitch should ascend: 0,1,2,3,4,5,6,7 on triggered steps
	CHECK_EQUAL(0, engine.pitch.values[0]);
	CHECK_EQUAL(1, engine.pitch.values[2]);
	CHECK_EQUAL(2, engine.pitch.values[4]);
	CHECK_EQUAL(7, engine.pitch.values[14]);
}

TEST(LanesEngineTest, loadDefaultPatternProbabilityAllMax) {
	engine.loadDefaultPattern();
	for (int32_t i = 0; i < 16; i++) {
		CHECK_EQUAL(100, engine.probability.values[i]);
	}
}

// --- lastPosition tests ---

TEST(LanesLaneTest, lastPositionTracksPreAdvancePosition) {
	LanesLane lane;
	lane.length = 4;
	lane.direction = LaneDirection::FORWARD;
	lane.position = 0;
	lane.lastPosition = 0;

	lane.advance();
	CHECK_EQUAL(0, lane.lastPosition); // was at 0 before advancing to 1
	CHECK_EQUAL(1, lane.position);

	lane.advance();
	CHECK_EQUAL(1, lane.lastPosition);
	CHECK_EQUAL(2, lane.position);
}

TEST(LanesLaneTest, lastPositionResetToZero) {
	LanesLane lane;
	lane.length = 4;
	lane.position = 3;
	lane.lastPosition = 2;
	lane.reset();
	CHECK_EQUAL(0, lane.lastPosition);
	CHECK_EQUAL(0, lane.position);
}

// --- Per-step probability tests ---

TEST(LanesEngineTest, stepProbabilityAffectsPitchLane) {
	engine.setSeed(42);
	engine.trigger.length = 4;
	engine.trigger.values.fill(1); // all steps fire
	engine.pitch.length = 4;
	engine.pitch.values[0] = 5; // 5 scale degrees up

	// Set per-step probability to 0% — should always fall back to default (0)
	engine.pitch.stepProbability[0] = 0;

	MusicalKey cMajor;
	cMajor.modeNotes = NoteSet({0, 2, 4, 5, 7, 9, 11});

	// With 0% prob on step 0, pitch should be 0 (default), so noteCode = baseNote
	LanesNote note = engine.step(cMajor);
	CHECK(!note.isRest());
	CHECK_EQUAL(60, note.noteCode); // base note, not 60+pitchOffset
}

TEST(LanesEngineTest, stepProbability100PercentAlwaysUsesLaneValue) {
	engine.setSeed(1);
	engine.trigger.length = 1;
	engine.trigger.values[0] = 1;
	engine.pitch.length = 1;
	engine.pitch.values[0] = 3; // 3 scale degrees up

	// 100% per-step probability (default) — should always use lane value
	engine.pitch.stepProbability[0] = 100;

	MusicalKey cMajor;
	cMajor.modeNotes = NoteSet({0, 2, 4, 5, 7, 9, 11});

	// 3 scale degrees up in C major = F (65)
	LanesNote note = engine.step(cMajor);
	CHECK(!note.isRest());
	CHECK_EQUAL(65, note.noteCode); // C4 + 3 scale degrees = F4
}

TEST(LanesEngineTest, stepProbabilityOnVelocityFallsBackToDefault) {
	engine.setSeed(42);
	engine.trigger.length = 1;
	engine.trigger.values[0] = 1;
	engine.velocity.length = 1;
	engine.velocity.values[0] = 120;
	engine.defaultVelocity = 100;

	// 0% per-step probability on velocity — should fall back to defaultVelocity
	engine.velocity.stepProbability[0] = 0;

	MusicalKey cMajor;
	cMajor.modeNotes = NoteSet({0, 2, 4, 5, 7, 9, 11});

	LanesNote note = engine.step(cMajor);
	CHECK(!note.isRest());
	CHECK_EQUAL(100, note.velocity); // defaultVelocity, not 120
}
