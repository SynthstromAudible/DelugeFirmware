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
	CHECK_EQUAL(0, lfo.render(0, type, 0));
	lfo.tick(10, 100);
	CHECK_EQUAL(2000, lfo.render(0, type, 0));
}

TEST(LFOTest, renderLocalTriangle) {
	LFO lfo;
	LFOType type = LFOType::TRIANGLE;
	// as per Voice::noteOn()
	lfo.phase = getLFOInitialPhaseForNegativeExtreme(type);
	CHECK_EQUAL(-2147483648, lfo.render(0, type, 0));

	lfo.tick(10, 100);
	CHECK_EQUAL(-2147481648, lfo.render(0, type, 0));
}

TEST(LFOTest, renderGlobalSine) {
	LFO lfo;
	LFOType type = LFOType::SINE;
	// as per resyncGlobalLFO()
	lfo.phase = getLFOInitialPhaseForZero(type);
	CHECK_EQUAL(0, lfo.render(0, type, 0));

	lfo.tick(10, 100);
	CHECK_EQUAL(2412, lfo.render(0, type, 0));
}

TEST(LFOTest, renderLocalSine) {
	LFO lfo;
	LFOType type = LFOType::SINE;
	// as per Voice::noteOn()
	lfo.phase = getLFOInitialPhaseForNegativeExtreme(type);
	CHECK_EQUAL(-2147418112, lfo.render(0, type, 0));

	lfo.tick(10, 100);
	CHECK_EQUAL(-2147418082, lfo.render(0, type, 0));
}

TEST(LFOTest, renderSaw) {
	LFO lfo;
	LFOType type = LFOType::SAW;
	// as per resyncGlobalLFO() AND Voice::noteOn()
	lfo.phase = getLFOInitialPhaseForNegativeExtreme(type);
	CHECK_EQUAL(-2147483648, lfo.render(0, type, 0));

	lfo.tick(10, 100);
	CHECK_EQUAL(-2147482648, lfo.render(0, type, 0));
}

TEST(LFOTest, renderSquare) {
	LFO lfo;
	LFOType type = LFOType::SQUARE;
	// as per resyncGlobalLFO() AND Voice::noteOn()
	lfo.phase = getLFOInitialPhaseForNegativeExtreme(type);
	CHECK_EQUAL(2147483647, lfo.render(0, type, 0));

	lfo.tick(1328979, 193287); // need a big shift to see the jump
	CHECK_EQUAL(-2147483648, lfo.render(0, type, 0));
}

TEST(LFOTest, renderSampleAndHold) {
	LFO lfo;
	LFOType type = LFOType::SAMPLE_AND_HOLD;
	// as per resyncGlobalLFO() AND Voice::noteOn()
	lfo.phase = getLFOInitialPhaseForNegativeExtreme(type);
	// CHECK_EQUAL evaluates multiple times: get the value just once.
	int32_t value = lfo.render(0, type, 0);
	CHECK_EQUAL(-1392915738, value);

	lfo.tick(1328979, 193287); // no change...
	value = lfo.render(0, type, 0);
	CHECK_EQUAL(-1392915738, value);
	CHECK(lfo.phase != 0);

	lfo.phase = 0; // force reset
	value = lfo.render(0, type, 0);
	CHECK_EQUAL(-28442955, value);
	CHECK(lfo.phase == 0);
}

TEST(LFOTest, renderRandomWalk) {
	LFO lfo;
	LFOType type = LFOType::RANDOM_WALK;
	// as per resyncGlobalLFO() AND Voice::noteOn()
	lfo.phase = getLFOInitialPhaseForNegativeExtreme(type);
	// CHECK_EQUAL evaluates multiple times: get the value just once.
	int32_t value = lfo.render(0, type, 0);
	CHECK_EQUAL(-2948644, value);
	CHECK(lfo.phase == 0);

	lfo.phase = 0; // force reset
	value = lfo.render(1, type, 1);
	CHECK_EQUAL(-78931243, value);

	lfo.tick(10, 10); // no change...
	value = lfo.render(1, type, 1);
	CHECK_EQUAL(-78931243, value);
	CHECK_EQUAL(102, lfo.phase);
}
