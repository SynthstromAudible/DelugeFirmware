#include "CppUTest/TestHarness.h"
#include "model/scale/musical_key.h"
#include "model/scale/preset_scales.h"
#include "model/sequencing/lanes_engine.h"

#include <algorithm>
#include <numeric>
#include <random>

TEST_GROUP(LanesPropertyTest) {
	std::mt19937 rng;
	MusicalKey cMajor;

	void setup() {
		rng.seed(12345); // deterministic
		cMajor.rootNote = 60;
		cMajor.modeNotes = presetScaleNotes[MAJOR_SCALE];
	}

	uint32_t computeLCM(const LanesEngine& engine) {
		std::vector<uint8_t> lengths;
		if (engine.trigger.length > 0)
			lengths.push_back(engine.trigger.length);
		if (engine.pitch.length > 0)
			lengths.push_back(engine.pitch.length);
		if (engine.velocity.length > 0)
			lengths.push_back(engine.velocity.length);
		if (engine.gate.length > 0)
			lengths.push_back(engine.gate.length);
		if (engine.octave.length > 0)
			lengths.push_back(engine.octave.length);
		if (engine.retrigger.length > 0)
			lengths.push_back(engine.retrigger.length);
		if (engine.glide.length > 0)
			lengths.push_back(engine.glide.length);
		if (lengths.empty())
			return 1;

		uint32_t result = lengths[0];
		for (size_t i = 1; i < lengths.size(); i++) {
			result = std::lcm(result, (uint32_t)lengths[i]);
		}
		return result;
	}

	void randomizeLane(LanesLane & lane, int16_t minVal, int16_t maxVal) {
		std::uniform_int_distribution<int> lenDist(1, 16);
		std::uniform_int_distribution<int16_t> valDist(minVal, maxVal);
		std::uniform_int_distribution<int> dirDist(0, 2);

		lane.length = lenDist(rng);
		lane.direction = static_cast<LaneDirection>(dirDist(rng));
		for (int i = 0; i < lane.length; i++) {
			lane.values[i] = valDist(rng);
		}
	}
};

TEST(LanesPropertyTest, noteAlwaysInMidiRange) {
	for (int trial = 0; trial < 50; trial++) {
		LanesEngine engine;
		engine.trigger.length = std::uniform_int_distribution<int>(1, 8)(rng);
		for (int i = 0; i < engine.trigger.length; i++) {
			engine.trigger.values[i] = std::uniform_int_distribution<int>(0, 1)(rng);
		}
		engine.trigger.values[0] = 1;

		randomizeLane(engine.pitch, 0, 127);
		randomizeLane(engine.interval, -7, 7);
		randomizeLane(engine.octave, -3, 3);
		randomizeLane(engine.velocity, 1, 127);
		randomizeLane(engine.gate, 1, 127);

		for (int step = 0; step < 200; step++) {
			LanesNote note = engine.step(cMajor);
			if (!note.isRest()) {
				CHECK(note.noteCode >= 0);
				CHECK(note.noteCode <= 127);
			}
			else {
				CHECK_EQUAL(-1, note.noteCode);
			}
		}
	}
}

TEST(LanesPropertyTest, allTriggersZeroMeansAllRests) {
	for (int trial = 0; trial < 20; trial++) {
		LanesEngine engine;
		engine.trigger.length = std::uniform_int_distribution<int>(1, 16)(rng);
		for (int i = 0; i < engine.trigger.length; i++) {
			engine.trigger.values[i] = 0;
		}
		randomizeLane(engine.pitch, 0, 127);

		for (int step = 0; step < 50; step++) {
			CHECK(engine.step(cMajor).isRest());
		}
	}
}

TEST(LanesPropertyTest, deterministicLockReproducesOutput) {
	for (int trial = 0; trial < 20; trial++) {
		LanesEngine engine;
		engine.trigger.length = std::uniform_int_distribution<int>(1, 8)(rng);
		for (int i = 0; i < engine.trigger.length; i++) {
			engine.trigger.values[i] = 1;
		}
		randomizeLane(engine.pitch, -7, 7);
		engine.probability.length = 1;
		engine.probability.values[0] = 50;

		uint32_t seed = rng();
		engine.setSeed(seed);
		engine.lock();

		int16_t firstRun[64];
		for (int i = 0; i < 64; i++) {
			firstRun[i] = engine.step(cMajor).noteCode;
		}

		engine.reset();
		for (int i = 0; i < 64; i++) {
			CHECK_EQUAL(firstRun[i], engine.step(cMajor).noteCode);
		}
	}
}

TEST(LanesPropertyTest, pingpongFullCycleLength) {
	for (int len = 2; len <= 16; len++) {
		LanesLane lane;
		lane.length = len;
		lane.direction = LaneDirection::PINGPONG;
		for (int i = 0; i < len; i++) {
			lane.values[i] = i;
		}

		int fullCycle = 2 * (len - 1);
		int16_t values[128];

		for (int i = 0; i < fullCycle; i++) {
			values[i] = lane.currentValue();
			lane.advance();
		}

		CHECK_EQUAL(0, lane.currentValue());

		for (int i = 0; i < fullCycle; i++) {
			CHECK_EQUAL(values[i], lane.currentValue());
			lane.advance();
		}
	}
}

TEST(LanesPropertyTest, disabledLanesNeutral) {
	for (int trial = 0; trial < 20; trial++) {
		LanesEngine engine1;
		LanesEngine engine2;

		engine1.trigger.length = 4;
		engine2.trigger.length = 4;
		for (int i = 0; i < 4; i++) {
			engine1.trigger.values[i] = 1;
			engine2.trigger.values[i] = 1;
		}

		engine1.pitch.length = 3;
		engine2.pitch.length = 3;
		for (int i = 0; i < 3; i++) {
			int16_t v = std::uniform_int_distribution<int16_t>(-7, 7)(rng);
			engine1.pitch.values[i] = v;
			engine2.pitch.values[i] = v;
		}

		engine2.velocity.length = 0;
		engine2.gate.length = 0;
		engine2.octave.length = 0;
		engine2.interval.length = 0;
		engine2.retrigger.length = 0;
		engine2.glide.length = 0;
		engine2.probability.length = 0;

		for (int step = 0; step < 12; step++) {
			LanesNote n1 = engine1.step(cMajor);
			LanesNote n2 = engine2.step(cMajor);
			CHECK_EQUAL(n1.noteCode, n2.noteCode);
			CHECK_EQUAL(n1.velocity, n2.velocity);
			CHECK_EQUAL(n1.gate, n2.gate);
		}
	}
}

TEST(LanesPropertyTest, intervalReversibility) {
	for (int x = 1; x <= 7; x++) {
		LanesEngine engine;
		engine.trigger.length = 1;
		engine.trigger.values[0] = 1;
		engine.pitch.length = 1;
		engine.pitch.values[0] = 0;
		engine.interval.length = 2;
		engine.interval.values[0] = x;
		engine.interval.values[1] = -x;

		engine.step(cMajor); // +x
		engine.step(cMajor); // -x, should be back to 0

		CHECK_EQUAL(0, engine.intervalAccumulator);
	}
}

TEST(LanesPropertyTest, patternRepeatsAtLCMBoundary) {
	// For any engine with forward lanes (no interval/probability), the pattern
	// repeats exactly at the LCM of all active lane lengths
	for (int trial = 0; trial < 20; trial++) {
		LanesEngine engine;

		// All triggers fire so we get deterministic output
		engine.trigger.length = std::uniform_int_distribution<int>(1, 8)(rng);
		engine.trigger.direction = LaneDirection::FORWARD;
		for (int i = 0; i < engine.trigger.length; i++) {
			engine.trigger.values[i] = 1;
		}

		// Randomize pitch, velocity, gate with forward direction only
		randomizeLane(engine.pitch, -7, 7);
		engine.pitch.direction = LaneDirection::FORWARD;
		randomizeLane(engine.velocity, 1, 127);
		engine.velocity.direction = LaneDirection::FORWARD;
		randomizeLane(engine.gate, 1, 127);
		engine.gate.direction = LaneDirection::FORWARD;

		uint32_t lcm = computeLCM(engine);
		if (lcm > 128)
			continue; // skip huge LCMs

		// Record first cycle
		int16_t firstNotes[128];
		uint8_t firstVels[128];
		for (uint32_t i = 0; i < lcm; i++) {
			LanesNote n = engine.step(cMajor);
			firstNotes[i] = n.noteCode;
			firstVels[i] = n.velocity;
		}

		// Verify second cycle is identical
		for (uint32_t i = 0; i < lcm; i++) {
			LanesNote n = engine.step(cMajor);
			CHECK_EQUAL(firstNotes[i], n.noteCode);
			CHECK_EQUAL(firstVels[i], n.velocity);
		}
	}
}
