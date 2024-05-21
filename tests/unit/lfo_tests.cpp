#include "CppUTest/TestHarness.h"
#include "modulation/lfo.h"
#include "definitions_cxx.hpp"
#include "util/waves.h"

TEST_GROUP(LFOTest) {
	void setup() {
		// Set CONG for all tests, so they're deterministic
		CONG = 13287131;
	}
};

TEST(LFOTest, renderSyncedTriangle) {
	LFO lfo;
	LFOConfig conf(LFOType::TRIANGLE, SYNC_LEVEL_8TH);
	// as in resyncGlobalLFO() & Voide::noteOn()
	lfo.setInitialPhase(conf);
	int32_t numSamples = 10, phaseIncrement = 100;
	CHECK_EQUAL(0, lfo.render(10, conf, 100));
	CHECK_EQUAL(numSamples*phaseIncrement*2, lfo.render(0, conf, 0));
}

TEST(LFOTest, renderUnsyncedTriangle_noSync) {
	LFO lfo;
	LFOConfig conf(LFOType::TRIANGLE, SYNC_LEVEL_NONE);
	// as per Voice::noteOn()
	lfo.setInitialPhase(conf);
	CHECK_EQUAL(INT32_MIN, lfo.render(10, conf, 100));
	CHECK_EQUAL(INT32_MIN+2000, lfo.render(0, conf, 0));
}

TEST(LFOTest, renderSyncedSine) {
	LFO lfo;
	LFOConfig conf(LFOType::SINE, SYNC_LEVEL_8TH);
	// as in resyncGlobalLFO() & Voide::noteOn()
	lfo.setInitialPhase(conf);
	// sin(0) == 0
	CHECK_EQUAL(0, lfo.phase);
	lfo.phase = 1024;
	// (2^31)*sin(2*pi*1024/(2^32)) = 3216.99
	CHECK_EQUAL(3216, lfo.render(0, conf, 0));
}

TEST(LFOTest, renderUnsyncedSine) {
	LFO lfo;
	LFOConfig conf(LFOType::SINE, SYNC_LEVEL_NONE);
	// as per Voice::noteOn()
	lfo.setInitialPhase(conf);
	CHECK_EQUAL(3221225472, lfo.phase);
	// These are nasty numbers, but the first one represents the
	// initial value for a local sine LFO, and a second one is a
	// an arbitray step forward.
	//
	// (2^31)*sin(2*pi*3221225472/(2^32)) = -2147483648 ... close?
	CHECK_EQUAL(-2147418112, lfo.render(10, conf, 100));
	CHECK_EQUAL(-2147418082, lfo.render(0, conf, 0));
}

TEST(LFOTest, renderSaw) {
	LFO lfo;
	LFOConfig conf(LFOType::SAW, SYNC_LEVEL_NONE);
	lfo.setInitialPhase(conf);
	// Same initial phase for synced and unsynced.
	uint32_t unsyncedPhase = lfo.phase;
	conf.syncLevel = SYNC_LEVEL_8TH;
	lfo.setInitialPhase(conf);
	CHECK_EQUAL(unsyncedPhase, lfo.phase);
	// Check the values as well.
	CHECK_EQUAL(INT32_MIN, lfo.render(10, conf, 100));
	CHECK_EQUAL(INT32_MIN+1000, lfo.render(0, conf, 0));
}

TEST(LFOTest, renderSquare) {
	LFO lfo;
	LFOConfig conf(LFOType::SQUARE, SYNC_LEVEL_NONE);
	lfo.setInitialPhase(conf);
	// Same initial phase for synced and unsynced.
	uint32_t unsyncedPhase = lfo.phase;
	conf.syncLevel = SYNC_LEVEL_8TH;
	lfo.setInitialPhase(conf);
	CHECK_EQUAL(unsyncedPhase, lfo.phase);
	// If you check the implementation, this is supposed to be "negative extreme",
	// but clearly is the positive instead. Oops? Should we change that?
	CHECK_EQUAL(INT32_MAX, lfo.render(0, conf, 0));
	lfo.phase = 0x80000001; // force over phase width
	CHECK_EQUAL(INT32_MIN, lfo.render(0, conf, 0));
}

TEST(LFOTest, renderRandomWalk) {
	LFO lfo;
	LFOConfig conf(LFOType::RANDOM_WALK, SYNC_LEVEL_NONE);
	lfo.setInitialPhase(conf);
	// Same initial phase for synced and unsynced.
	uint32_t unsyncedPhase = lfo.phase;
	conf.syncLevel = SYNC_LEVEL_8TH;
	lfo.setInitialPhase(conf);
	CHECK_EQUAL(unsyncedPhase, lfo.phase);
	CHECK_EQUAL(0, lfo.phase);
	// CHECK_EQUAL evaluates multiple times: get the value just once.
	int32_t value = lfo.render(10, conf, 10);
	CHECK_EQUAL(-2948644, value);
	CHECK_EQUAL(100, lfo.phase);

	value = lfo.render(10, conf, 10); // no change
	CHECK_EQUAL(-2948644, value);
	CHECK_EQUAL(200, lfo.phase);

	lfo.phase = 0; // force reset
	value = lfo.render(10, conf, 10);
	CHECK_EQUAL(-78931243, value);
	CHECK_EQUAL(100, lfo.phase);

	value = lfo.render(10, conf, 10); // no change
	CHECK_EQUAL(-78931243, value);
	CHECK_EQUAL(200, lfo.phase);

	lfo.phase = UINT32_MAX; // overflow
	value = lfo.render(10, conf, 10);
	CHECK_EQUAL(-174189095, value);
	CHECK_EQUAL(99, lfo.phase);

	value = lfo.render(10, conf, 10); // no change
	CHECK_EQUAL(-174189095, value);
	CHECK_EQUAL(199, lfo.phase);
}

TEST_GROUP(WaveTest) {
	void setup() {}
};

TEST(WaveTest, triangle) {
	// low turnover
	CHECK_EQUAL(-2147483647, getTriangle(UINT32_MAX));
	CHECK_EQUAL(-2147483648, getTriangle(0));
	CHECK_EQUAL(-2147483646, getTriangle(1));
	// passing zero up
	CHECK_EQUAL(-2, getTriangle(1073741823));
	CHECK_EQUAL( 0, getTriangle(1073741824));
	CHECK_EQUAL( 2, getTriangle(1073741825));
	// thigh turnover
	CHECK_EQUAL( 2147483646, getTriangle(2147483647u));
	CHECK_EQUAL( 2147483647, getTriangle(2147483648u));
	CHECK_EQUAL( 2147483645, getTriangle(2147483649u));
	// passing zero down
	CHECK_EQUAL( 1, getTriangle(3221225471u));
	CHECK_EQUAL(-1, getTriangle(3221225472u));
}
