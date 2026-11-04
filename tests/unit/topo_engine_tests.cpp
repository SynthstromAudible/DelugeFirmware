/*
 * Copyright © 2026 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

// Tests for the "Topo Drums" engine ported from MI Grids. The golden streams below
// were produced by compiling the ORIGINAL grids/pattern_generator.cc on the host
// (with pgm_read/eeprom shims) and recording the 8-bit output state after every one
// of 192 clock pulses (2 bars at 3 pulses/step, 24 PPQN), with avrlib's Random
// seeded to 0x21. Reconstructing the state byte from our engine's event masks must
// match bit-for-bit.

#include "CppUTest/TestHarness.h"
#include "model/generative/topo/topo_engine.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <string>
#include <vector>

using namespace deluge::model::topo;

TEST_GROUP(TopoEngineTests){};

namespace {

struct GoldenConfig {
	const char* name;
	TopoMode mode;
	bool swing;
	uint8_t x, y, randomness;
	uint8_t density[3];
	const char* stateHex;  // 192 hex-encoded state bytes
	const char* swingLine; // 32 signed swing amounts, space separated
};

// Original state byte: BIT7 RND, BIT6 flag (always set), BIT5..3 accents/resets, BIT2..0 fires.
const GoldenConfig kGoldenConfigs[] = {
    {
        "corner",
        TopoMode::Drums,
        false,
        0,
        0,
        0,
        {255, 255, 255},
        "6540c0c0404042c0c040c0c0464040c0c0c04340c0c04040c6c04040404046c0404040405f4040c0c0c0c740c0c040c0"
        "7740c0c040c04b40c04040c046c0c040c0c042c040404040c740c040404046c0404040404740c0c040404640c0c040c0"
        "6540c0c040c0c2c0c0c0c040c6c0c0404040c340c040c040c6c0c0404040c640c0404040df40c0404040c740c0c040c0"
        "f740c0c040c0cb4040c0404046404040404042c040c040c047c040404040c64040c040404740c040404046c0404040c0",
        "0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0",
    },
    {
        "mid",
        TopoMode::Drums,
        false,
        128,
        128,
        0,
        {128, 128, 128},
        "4940c0c0404040c0c040c0c0644040c0c0c04040c0c04040d6c04040404040c040404040654040c0c0c0c040c0c040c0"
        "4940c0c040c04040c04040c067c0c040c0c040c040404040d640c040404052c0404040404640c0c040404040c0c040c0"
        "4940c0c040c0c0c0c0c0c040e4c0c0404040c040c040c040d6c0c0404040c040c0404040e540c0404040c040c0c040c0"
        "c940c0c040c0c04040c0404067404040404040c040c040c056c040404040d24040c040404640c040404040c0404040c0",
        "0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0",
    },
    {
        "random",
        TopoMode::Drums,
        false,
        90,
        200,
        180,
        {90, 140, 200},
        "6d40c0c0404044c0c040c0c0654040c0c0c04440c0c04040d6c04040404044c040404040644040c0c0c0c640c0c040c0"
        "4440c0c040c04640c04040c044c0c040c0c041c040404040d640c040404044c0404040404440c0c040404640c0c040c0"
        "6d40c0c040c0c4c0c0c0c040e5c0c0404040c440c040c040d6c0c0404040c440c0404040e440c0404040c640c0c040c0"
        "c440c0c040c0c24040c0404044404040404041c040c040c056c040404040c44040c040404440c040404046c0404040c0",
        "0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0",
    },
    {
        "silent",
        TopoMode::Drums,
        false,
        255,
        255,
        0,
        {0, 0, 0},
        "4040c0c0404040c0c040c0c0404040c0c0c04040c0c04040c0c04040404040c040404040404040c0c0c0c040c0c040c0"
        "4040c0c040c04040c04040c040c0c040c0c040c040404040c040c040404040c0404040404040c0c040404040c0c040c0"
        "4040c0c040c0c0c0c0c0c040c0c0c0404040c040c040c040c0c0c0404040c040c0404040c040c0404040c040c0c040c0"
        "c040c0c040c0c04040c0404040404040404040c040c040c040c040404040c04040c040404040c040404040c0404040c0",
        "0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0",
    },
    {
        "swing",
        TopoMode::Drums,
        true,
        100,
        100,
        200,
        {150, 150, 150},
        "4d40c0c0404040c0c040c0c0644040c0c0c04040c0c04040d6c04040404040c040404040474040c0c0c0c040c0c040c0"
        "4d40c0c040c04040c04040c065c0c040c0c040c040404040d640c040404042c0404040404640c0c040404040c0c040c0"
        "4d40c0c040c0c0c0c0c0c040e4c0c0404040c040c040c040d6c0c0404040c040c0404040c740c0404040c040c0c040c0"
        "cd40c0c040c0c04040c0404065404040404040c040c040c056c040404040c24040c040404640c040404040c0404040c0",
        "33 33 -33 -33 33 33 -33 -33 33 33 -33 -33 33 33 -33 -33 33 33 -33 -33 33 33 -33 -33 33 33 -33 -33 33 33 -33 "
        "-33",
    },
    {
        "euclid",
        TopoMode::Euclidean,
        false,
        48,
        88,
        32,
        {24, 40, 16},
        "7b4040c040c0c0404040c0c040c0c0404040c0c0c04040c0c04040c0c04060404040c040424040404040c9c0c0c040c0"
        "c040c04040c0c040c04040c06040c040c0c040c0c040c040524040c040c040404040c0404940404040c0e040404040c0"
        "c040c040c0c0c040c0c040c0c2c0c0c0c040c0c0c0404040e040c040c040c9c0c0404040c040c0404040c040c0404040"
        "d240c0c040c0e040c0c040c0c04040c0404040404040404049c040c040c040c040404040e24040c040404040c0404040",
        "0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0",
    },
};

uint8_t parseHexNibble(char c) {
	if (c >= '0' && c <= '9') {
		return c - '0';
	}
	return (c - 'a') + 10;
}

uint8_t parseHexByte(const char* hex, size_t index) {
	return (parseHexNibble(hex[index * 2]) << 4) | parseHexNibble(hex[index * 2 + 1]);
}

void configure(TopoEngine& engine, const GoldenConfig& config) {
	engine.reset();
	engine.seedRng(0x21);
	engine.setMode(config.mode);
	engine.setSwing(config.swing);
	engine.setGateMode(false);
	engine.settings().x = config.x;
	engine.settings().y = config.y;
	engine.settings().randomness = config.randomness;
	for (uint8_t bank = 0; bank < kNumBanks; ++bank) {
		engine.settings().density[bank] = config.density[bank];
	}
}

// A deterministic non-trivial configuration for cross-engine comparisons.
void configureDeterministic(TopoEngine& engine) {
	engine.reset();
	engine.seedRng(0x21);
	engine.setMode(TopoMode::Drums);
	engine.settings().x = 90;
	engine.settings().y = 200;
	engine.settings().randomness = 180;
	engine.settings().density[0] = 90;
	engine.settings().density[1] = 140;
	engine.settings().density[2] = 200;
}

} // namespace

TEST(TopoEngineTests, goldenStateStreamsMatchOriginalGrids) {
	for (const auto& config : kGoldenConfigs) {
		TopoEngine engine;
		configure(engine, config);

		for (int pulse = 0; pulse < 192; ++pulse) {
			TopoStepEvent ev = engine.advance(1);
			uint8_t state = 0x40 | (engine.noiseBit() ? 0x80 : 0);
			if (ev.stepsCrossed) {
				state |= (ev.fireMask & 7) | ((ev.accentMask & 7) << 3);
			}
			uint8_t expected = parseHexByte(config.stateHex, pulse);
			if (state != expected) {
				FAIL((std::string("config ") + config.name + " pulse " + std::to_string(pulse) + ": expected "
				      + std::to_string(expected) + " got " + std::to_string(state))
				         .c_str());
			}
		}
	}
}

TEST(TopoEngineTests, goldenSwingAmountsMatchOriginalGrids) {
	for (const auto& config : kGoldenConfigs) {
		TopoEngine engine;
		configure(engine, config);

		int step = 0;
		for (int pulse = 0; pulse < 96 && step < kStepsPerPattern; ++pulse) {
			if (pulse % kPulsesPerStep == 0) {
				// Parse the step'th value of the golden swing line.
				const char* p = config.swingLine;
				for (int i = 0; i < step; ++i) {
					while (*p && *p != ' ') {
						p++;
					}
					while (*p == ' ') {
						p++;
					}
				}
				bool negative = (*p == '-');
				if (negative) {
					p++;
				}
				int value = 0;
				while (*p >= '0' && *p <= '9') {
					value = value * 10 + (*p - '0');
					p++;
				}
				int8_t expected = negative ? -value : value;

				CHECK_EQUAL_TEXT(expected, engine.swingAmount(), config.name);
				++step;
			}
			engine.advance(1);
		}
	}
}

TEST(TopoEngineTests, fixedPointHelpersMatchAvrlibAsmSemantics) {
	// U8Mix is the high byte of (b * balance + a * (255 - balance)).
	CHECK_EQUAL(254, detail::u8Mix(255, 0, 0)); // (255 * 255) >> 8
	CHECK_EQUAL(254, detail::u8Mix(255, 255, 128));
	CHECK_EQUAL(0, detail::u8Mix(0, 0, 255));
	CHECK_EQUAL(127, detail::u8Mix(0, 255, 128)); // (255 * 128) >> 8
	// U8U8MulShift8 is (a * b) >> 8.
	CHECK_EQUAL(0, detail::u8u8MulShift8(255, 0));
	CHECK_EQUAL(127, detail::u8u8MulShift8(255, 128));
	CHECK_EQUAL(42, detail::u8u8MulShift8(255, 43));
}

namespace {

// Reference reimplementation of resources/lookup_tables.py EuclidianPattern(),
// including the Python 2 rounding used when building lut_res_euclidean:
//   num_notes = int(round(float(num_notes_raw) / 31 * num_steps))
std::vector<int> referenceEuclidianPattern(int k, int n) {
	std::vector<std::vector<int>> pattern;
	for (int i = 0; i < k; ++i) {
		pattern.push_back(std::vector<int>{1});
	}
	for (int i = 0; i < n - k; ++i) {
		pattern.push_back(std::vector<int>{0});
	}
	while (k) {
		int cut = std::min(k, static_cast<int>(pattern.size()) - k);
		std::vector<std::vector<int>> next;
		for (int i = 0; i < cut; ++i) {
			std::vector<int> joined = pattern[i];
			joined.insert(joined.end(), pattern[k + i].begin(), pattern[k + i].end());
			next.push_back(joined);
		}
		for (int i = cut; i < k; ++i) {
			next.push_back(pattern[i]);
		}
		for (size_t i = static_cast<size_t>(k) + cut; i < pattern.size(); ++i) {
			next.push_back(pattern[i]);
		}
		k = cut;
		pattern = next;
	}
	std::vector<int> flat;
	for (const auto& part : pattern) {
		flat.insert(flat.end(), part.begin(), part.end());
	}
	return flat;
}

} // namespace

TEST(TopoEngineTests, euclideanLutMatchesReferenceBjorklund) {
	for (int numSteps = 1; numSteps <= 32; ++numSteps) {
		for (int numNotesRaw = 0; numNotesRaw < 32; ++numNotesRaw) {
			int numNotes = static_cast<int>(std::floor(static_cast<double>(numNotesRaw) / 31.0 * numSteps + 0.5));
			uint32_t expected = 0;
			std::vector<int> pattern = referenceEuclidianPattern(numNotes, numSteps);
			for (size_t i = 0; i < pattern.size() && i < 32; ++i) {
				if (pattern[i]) {
					expected |= 1u << i;
				}
			}
			uint32_t actual = kEuclideanPatterns[(numSteps - 1) * 32 + numNotesRaw];
			if (actual != expected) {
				FAIL((std::string("euclid LUT mismatch at length ") + std::to_string(numSteps) + " density index "
				      + std::to_string(numNotesRaw) + ": expected " + std::to_string(expected) + " got "
				      + std::to_string(actual))
				         .c_str());
			}
		}
	}
}

TEST(TopoEngineTests, euclideanFiringSequenceFollowsLut) {
	// Bank A: length 8 (raw 56 -> (56 >> 3) + 1). The density knob is >>3 to select the
	// LUT column (0-31), whose notes are round(column / 31 * length): raw 80 -> column
	// 10 -> round(10 / 31 * 8) == 3 notes.
	TopoEngine engine;
	engine.reset();
	engine.setMode(TopoMode::Euclidean);
	engine.settings().x = 56;
	engine.settings().density[0] = 80;

	std::vector<bool> fired;
	// The part counter advances at sixteenth-note (every 2 steps), so one cycle of
	// length 8 spans 16 pattern steps = half a bar. Collect a full cycle.
	for (int step = 0; step < 16; ++step) {
		TopoStepEvent ev = engine.advance(kPulsesPerStep);
		CHECK_EQUAL(1, ev.stepsCrossed);
		fired.push_back(ev.fired(0));
	}

	uint32_t pattern = kEuclideanPatterns[(8 - 1) * 32 + 10];
	CHECK_EQUAL(3, std::popcount(pattern));
	for (int i = 0; i < 8; ++i) {
		// Even steps are sixteenths; euclidean step i maps to pattern step 2*i.
		CHECK_EQUAL_TEXT(static_cast<bool>(pattern & (1u << i)), static_cast<bool>(fired[i * 2]),
		                 std::to_string(i).c_str());
		CHECK_EQUAL_TEXT(false, static_cast<bool>(fired[i * 2 + 1]), "odd steps never fire");
	}
}

TEST(TopoEngineTests, densityExtremes) {
	TopoEngine engine;
	engine.reset();
	engine.setMode(TopoMode::Drums);
	engine.settings().x = 128;
	engine.settings().y = 128;
	engine.settings().randomness = 0;

	// Density 0 -> threshold 255 -> nothing fires anywhere.
	for (uint8_t& d : engine.settings().density) {
		d = 0;
	}
	uint8_t anyFire = 0;
	for (int step = 0; step < kStepsPerPattern; ++step) {
		TopoStepEvent ev = engine.advance(kPulsesPerStep);
		anyFire |= ev.fireMask;
	}
	CHECK_EQUAL(0, anyFire);

	// Density 255 -> threshold 0 -> every step with nonzero map level fires, for all banks.
	engine.reset();
	for (uint8_t& d : engine.settings().density) {
		d = 255;
	}
	for (uint8_t bank = 0; bank < kNumBanks; ++bank) {
		for (uint8_t step = 0; step < kStepsPerPattern; ++step) {
			bool shouldFire = engine.levelAt(step, bank) > 0;
			CHECK_TRUE_TEXT(shouldFire == engine.fireAt(step, bank),
			                (std::string("bank ") + std::to_string(bank) + " step " + std::to_string(step)).c_str());
		}
	}
}

TEST(TopoEngineTests, accentThresholdIsHonoured) {
	TopoEngine engine;
	engine.reset();
	engine.setMode(TopoMode::Drums);
	engine.settings().x = 0;
	engine.settings().y = 0;
	engine.settings().randomness = 0;
	for (uint8_t& d : engine.settings().density) {
		d = 255; // everything with level > 0 fires
	}

	// With the threshold at 255, nothing is ever accented.
	engine.settings().accentThreshold = 255;
	uint8_t anyAccent = 0;
	for (int step = 0; step < kStepsPerPattern; ++step) {
		TopoStepEvent ev = engine.advance(kPulsesPerStep);
		anyAccent |= ev.accentMask;
	}
	CHECK_EQUAL(0, anyAccent);

	// With the threshold at 0, every fired hit is accented.
	engine.reset();
	engine.settings().accentThreshold = 0;
	uint8_t anyDivergence = 0;
	for (int step = 0; step < kStepsPerPattern; ++step) {
		TopoStepEvent ev = engine.advance(kPulsesPerStep);
		anyDivergence |= (ev.fireMask ^ ev.accentMask);
	}
	CHECK_EQUAL(0, anyDivergence);
}

TEST(TopoEngineTests, loopWrapFlagAndStepSequence) {
	TopoEngine engine;
	engine.reset();
	engine.setMode(TopoMode::Drums);
	for (int pulse = 0; pulse < 2 * kPulsesPerPattern; ++pulse) {
		TopoStepEvent ev = engine.advance(1);
		if (pulse % kPulsesPerStep == 0) {
			CHECK_EQUAL(1, ev.stepsCrossed);
			CHECK_EQUAL((pulse / kPulsesPerStep) % kStepsPerPattern, ev.step);
			CHECK_EQUAL((pulse % kPulsesPerPattern) == 0, ev.loopWrapped);
		}
		else {
			CHECK_EQUAL(0, ev.stepsCrossed);
		}
	}
}

TEST(TopoEngineTests, multipleBoundariesUnionNoDroppedHits) {
	// Advancing 6 pulses in one call must fire the union of the two crossed steps.
	TopoEngine batch, stepped;
	configureDeterministic(batch);
	configureDeterministic(stepped);

	TopoStepEvent ev = batch.advance(6);
	CHECK_EQUAL(2, ev.stepsCrossed);

	TopoStepEvent first = stepped.advance(3);
	TopoStepEvent second = stepped.advance(3);
	CHECK_EQUAL(first.fireMask | second.fireMask, ev.fireMask);
	CHECK_EQUAL(first.accentMask | second.accentMask, ev.accentMask);
	CHECK_EQUAL(second.step, ev.step);
}

TEST(TopoEngineTests, resetRestartsPattern) {
	TopoEngine engine;
	configureDeterministic(engine);

	uint8_t firstLoop[kStepsPerPattern];
	for (int step = 0; step < kStepsPerPattern; ++step) {
		firstLoop[step] = engine.advance(kPulsesPerStep).fireMask;
	}

	engine.reset();
	engine.seedRng(0x21);
	for (int step = 0; step < kStepsPerPattern; ++step) {
		CHECK_EQUAL(firstLoop[step], engine.advance(kPulsesPerStep).fireMask);
	}
}

TEST(TopoEngineTests, rngIsDeterministicAndReseedable) {
	TopoEngine a, b;
	a.seedRng(0x1234);
	b.seedRng(0x1234);
	a.settings().randomness = 200;
	b.settings().randomness = 200;

	for (int pulse = 0; pulse < 300; ++pulse) {
		TopoStepEvent ea = a.advance(1);
		TopoStepEvent eb = b.advance(1);
		CHECK_EQUAL(ea.fireMask, eb.fireMask);
		CHECK_EQUAL(ea.accentMask, eb.accentMask);
		CHECK_EQUAL(a.noiseBit(), b.noiseBit());
	}

	// A different seed must change the random output bit (otherwise re-seeding is a no-op).
	b.seedRng(0x9abc);
	bool diverged = false;
	for (int pulse = 0; pulse < 64 && !diverged; ++pulse) {
		a.advance(1);
		b.advance(1);
		diverged |= (a.noiseBit() != b.noiseBit());
	}
	CHECK_TRUE(diverged);
}

TEST(TopoEngineTests, simulateNextStepsMatchesLiveEngine) {
	TopoEngine engine;
	engine.reset();
	engine.seedRng(0x77);
	engine.settings().randomness = 140;
	engine.settings().x = 77;
	engine.settings().y = 190;

	// Move to a mid-loop, mid-step position.
	engine.advance(41);

	TopoStepEvent simulated[kStepsPerPattern];
	engine.simulateNextSteps(kStepsPerPattern, simulated);

	// The live engine, advanced pulse by pulse, must produce exactly these events.
	int produced = 0;
	for (int pulse = 0; pulse < kPulsesPerPattern * kStepsPerPattern + 4 && produced < kStepsPerPattern; ++pulse) {
		TopoStepEvent ev = engine.advance(1);
		if (ev.stepsCrossed) {
			CHECK_EQUAL(simulated[produced].step, ev.step);
			CHECK_EQUAL(simulated[produced].fireMask, ev.fireMask);
			CHECK_EQUAL(simulated[produced].accentMask, ev.accentMask);
			++produced;
		}
	}
	CHECK_EQUAL(kStepsPerPattern, produced);
}

TEST(TopoEngineTests, levelAtReflectsMapAndPerturbation) {
	TopoEngine engine;
	engine.reset();
	engine.seedRng(0x21);
	engine.settings().x = 0;
	engine.settings().y = 0;
	engine.settings().randomness = 0;

	// At the map corner x=0,y=0 the bilinear mix degenerates to the four corner nodes
	// scaled by the U8Mix(., ., 0) semantics.
	auto baseLevelAt = [&](uint8_t bank) -> uint8_t {
		uint16_t offset = static_cast<uint16_t>(bank) * kStepsPerPattern;
		uint8_t a = kPatternNodes[kDrumMap[0][0]][offset];
		uint8_t b = kPatternNodes[kDrumMap[1][0]][offset];
		uint8_t c = kPatternNodes[kDrumMap[0][1]][offset];
		uint8_t d = kPatternNodes[kDrumMap[1][1]][offset];
		return detail::u8Mix(detail::u8Mix(a, b, 0), detail::u8Mix(c, d, 0), 0);
	};
	for (uint8_t bank = 0; bank < kNumBanks; ++bank) {
		CHECK_EQUAL(baseLevelAt(bank), engine.levelAt(0, bank));
	}

	// Perturbation can only raise levels (it is added), never lower them.
	engine.reset();
	engine.seedRng(0x21);
	engine.settings().randomness = 255;
	engine.advance(1); // re-rolls perturbations at step 0
	for (uint8_t bank = 0; bank < kNumBanks; ++bank) {
		CHECK_TRUE(engine.levelAt(0, bank) >= baseLevelAt(bank));
	}

	// Euclidean mode reports no map levels.
	engine.setMode(TopoMode::Euclidean);
	CHECK_EQUAL(0, engine.levelAt(0, 0));
}
