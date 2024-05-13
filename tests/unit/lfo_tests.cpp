#include "CppUTest/TestHarness.h"
#include "modulation/lfo.h"
#include "definitions_cxx.hpp"

TEST_GROUP(LFOTest) {
	void setup() {
		// Set CONG for all tests, so they're deterministic
		CONG = 13287131;
	}
};

TEST(LFOTest, renderGlobalTriangle) {
	LFO lfo;
	LFOType type = LFOType::TRIANGLE;
	// as per resyncGlobalLFO()
	lfo.phase = getLFOInitialPhaseForZero(type);
	int32_t numSamples = 10, phaseIncrement = 100;
	CHECK_EQUAL(0, lfo.render(10, type, 100));
	CHECK_EQUAL(numSamples*phaseIncrement*2, lfo.render(0, type, 0));
}

TEST(LFOTest, renderLocalTriangle) {
	LFO lfo;
	LFOType type = LFOType::TRIANGLE;
	// as per Voice::noteOn()
	lfo.phase = getLFOInitialPhaseForNegativeExtreme(type);
	CHECK_EQUAL(INT32_MIN, lfo.render(10, type, 100));
	CHECK_EQUAL(INT32_MIN+2000, lfo.render(0, type, 0));
}

TEST(LFOTest, renderGlobalSine) {
	LFO lfo;
	LFOType type = LFOType::SINE;
	// as per resyncGlobalLFO()
	lfo.phase = getLFOInitialPhaseForZero(type);
	// sin(0) == 0
	CHECK_EQUAL(0, lfo.phase);
	lfo.phase = 1024;
	// (2^31)*sin(2*pi*1024/(2^32)) = 3216.99
	CHECK_EQUAL(3216, lfo.render(0, type, 0));
}

TEST(LFOTest, renderLocalSine) {
	LFO lfo;
	LFOType type = LFOType::SINE;
	// as per Voice::noteOn()
	lfo.phase = getLFOInitialPhaseForNegativeExtreme(type);
	CHECK_EQUAL(3221225472, lfo.phase);
	// These are nasty numbers, but the first one represents the
	// initial value for a local sine LFO, and a second one is a
	// an arbitray step forward.
	//
	// (2^31)*sin(2*pi*3221225472/(2^32)) = -2147483648 ... close?
	CHECK_EQUAL(-2147418112, lfo.render(10, type, 100));
	CHECK_EQUAL(-2147418082, lfo.render(0, type, 0));
}

TEST(LFOTest, renderSaw) {
	LFO lfo;
	LFOType type = LFOType::SAW;
	// as per resyncGlobalLFO() AND Voice::noteOn()
	lfo.phase = getLFOInitialPhaseForNegativeExtreme(type);
	CHECK_EQUAL(INT32_MIN, lfo.render(10, type, 100));
	CHECK_EQUAL(INT32_MIN+1000, lfo.render(0, type, 0));
}

TEST(LFOTest, renderSquare) {
	LFO lfo;
	LFOType type = LFOType::SQUARE;
	// as per resyncGlobalLFO() AND Voice::noteOn()
	lfo.phase = getLFOInitialPhaseForNegativeExtreme(type);
	// ...it's interesting that our initial phase for "negative extreme"
	// for square LFO actually gets the positive extreme...
	CHECK_EQUAL(INT32_MAX, lfo.render(0, type, 0));
	lfo.phase = 0x80000001; // force over phase width
	CHECK_EQUAL(INT32_MIN, lfo.render(0, type, 0));
}

TEST(LFOTest, renderSampleAndHold) {
	LFO lfo;
	LFOType type = LFOType::SAMPLE_AND_HOLD;
	// as per resyncGlobalLFO() AND Voice::noteOn()
	lfo.phase = getLFOInitialPhaseForNegativeExtreme(type);
	CHECK_EQUAL(0, lfo.phase);
	// CHECK_EQUAL evaluates multiple times: get the value just once.
	int32_t value = lfo.render(10, type, 10);
	CHECK_EQUAL(-1392915738, value);
	CHECK_EQUAL(100, lfo.phase);

	value = lfo.render(10, type, 10); // no change
	CHECK_EQUAL(-1392915738, value);
	CHECK_EQUAL(200, lfo.phase);

	lfo.phase = 0; // force reset
	value = lfo.render(10, type, 10);
	CHECK_EQUAL(-28442955, value);
	CHECK_EQUAL(100, lfo.phase);

	value = lfo.render(10, type, 10); // no change
	CHECK_EQUAL(-28442955, value);
	CHECK_EQUAL(200, lfo.phase);

	lfo.phase = UINT32_MAX; // phase to max, so next will overflow
	value = lfo.render(10, type, 10);
	CHECK_EQUAL(-1725170056, value);
	CHECK_EQUAL(99, lfo.phase);

	value = lfo.render(10, type, 10); // no change
	CHECK_EQUAL(-1725170056, value);
	CHECK_EQUAL(199, lfo.phase);
}

TEST(LFOTest, renderRandomWalk) {
	LFO lfo;
	LFOType type = LFOType::RANDOM_WALK;
	// as per resyncGlobalLFO() AND Voice::noteOn()
	lfo.phase = getLFOInitialPhaseForNegativeExtreme(type);
	CHECK_EQUAL(0, lfo.phase);
	// CHECK_EQUAL evaluates multiple times: get the value just once.
	int32_t value = lfo.render(10, type, 10);
	CHECK_EQUAL(-2948644, value);
	CHECK_EQUAL(100, lfo.phase);

	value = lfo.render(10, type, 10); // no change
	CHECK_EQUAL(-2948644, value);
	CHECK_EQUAL(200, lfo.phase);

	lfo.phase = 0; // force reset
	value = lfo.render(10, type, 10);
	CHECK_EQUAL(-78931243, value);
	CHECK_EQUAL(100, lfo.phase);

	value = lfo.render(10, type, 10); // no change
	CHECK_EQUAL(-78931243, value);
	CHECK_EQUAL(200, lfo.phase);

	lfo.phase = UINT32_MAX; // overflow
	value = lfo.render(10, type, 10);
	CHECK_EQUAL(-174189095, value);
	CHECK_EQUAL(99, lfo.phase);

	value = lfo.render(10, type, 10); // no change
	CHECK_EQUAL(-174189095, value);
	CHECK_EQUAL(199, lfo.phase);
}
