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

// SidechainEnvelope: a hit dips the level, which recovers over an attack then a curved release. Exact reference
// values pin the attack, the release at three shapes, the stage changes inside a block, a weaker hit ignored, a
// stronger one taken, an instant attack, late hits and combined hits. Rates are as an owner resolves them: an attack
// of 37640 per sample (222 samples), a release of 3680 (2279 samples).

#include "dsp-next/fixed/dynamics/sidechain_envelope.h"

#include <cppspec.hpp>
#include <cstdint>
#include <limits>
#include <ostream>
#include <vector>

using namespace deluge::dsp::fixed;
using S = SidechainEnvelope::Stage;

namespace deluge::dsp::fixed {
/// A stage, printed by name, so a failing comparison of stage lists shows it.
std::ostream& operator<<(std::ostream& out, SidechainEnvelope::Stage stage) {
	switch (stage) {
	case SidechainEnvelope::Stage::OFF:
		return out << "OFF";
	case SidechainEnvelope::Stage::ATTACK:
		return out << "ATTACK";
	case SidechainEnvelope::Stage::RELEASE:
		return out << "RELEASE";
	}
	return out;
}
} // namespace deluge::dsp::fixed

namespace {

constexpr SidechainRates kRates{.attack = 37640, .release = 3680, .instant_attack = false};
/// The minimum attack: a 2^23 stage in 8 samples.
constexpr SidechainRates kInstantRates{.attack = 1048576, .release = 3680, .instant_attack = true};
constexpr int32_t kHit = 0x60000000;

/// One hit, then 40 blocks of 128, and the stage after each.
const std::vector<int32_t> kMain{
    -420429314,  -1603353912, -1585455984, -1556814480, -1517238720, -1466648424, -1404845568, -1331730312,
    -1247071680, -1150905768, -1043224560, -924359688,  -794962752,  -656393304,  -510967296,  -362615304,
    -217681152,  -86159664,   0,           0,           0,           0,           0,           0,
    0,           0,           0,           0,           0,           0,           0,           0,
    0,           0,           0,           0,           0,           0,           0,           0};
const std::vector<S> kMainStages{S::ATTACK,  S::RELEASE, S::RELEASE, S::RELEASE, S::RELEASE, S::RELEASE, S::RELEASE,
                                 S::RELEASE, S::RELEASE, S::RELEASE, S::RELEASE, S::RELEASE, S::RELEASE, S::RELEASE,
                                 S::RELEASE, S::RELEASE, S::RELEASE, S::RELEASE, S::OFF,     S::OFF,     S::OFF,
                                 S::OFF,     S::OFF,     S::OFF,     S::OFF,     S::OFF,     S::OFF,     S::OFF,
                                 S::OFF,     S::OFF,     S::OFF,     S::OFF,     S::OFF,     S::OFF,     S::OFF,
                                 S::OFF,     S::OFF,     S::OFF,     S::OFF,     S::OFF};
/// One hit, three blocks, a hit of 2^29 (weaker than the duck), five more.
const std::vector<int32_t> kWeaker{-420429314,  -1603353912, -1585455984, -1556814480,
                                   -1517238720, -1466648424, -1404845568, -1331730312};
/// One hit, three blocks, a full-scale hit, five more; and the stages.
const std::vector<int32_t> kStronger{-420429314,  -1603353912, -1585455984, -1732165928,
                                     -2137805216, -2113941312, -2075752640, -2022984960};
const std::vector<S> kStrongerStages{S::ATTACK,  S::RELEASE, S::RELEASE, S::ATTACK,
                                     S::RELEASE, S::RELEASE, S::RELEASE, S::RELEASE};
/// One hit at the minimum attack, then six blocks of 4; and the stages.
const std::vector<int32_t> kInstant{-1610558754, -1610494940, -1610421552, -1610338158, -1610196644, -1610093758};
const std::vector<S> kInstantStages{S::RELEASE, S::RELEASE, S::RELEASE, S::RELEASE, S::RELEASE, S::RELEASE};
/// One hit, then 20 blocks at shapes INT32_MIN, INT32_MAX and 2^30.
const std::vector<int32_t> kShapeMin{-420429314,  -1520173056, -1429733376, -1339293696, -1248854016,
                                     -1158414336, -1067974656, -977534976,  -887095296,  -796655616,
                                     -706215936,  -615776256,  -525336576,  -434896896,  -344457216,
                                     -254017536,  -163577856,  -73138176,   0,           0};
const std::vector<int32_t> kShapeMax{-420429314,  -1608302592, -1605156864, -1600880640, -1594982400,
                                     -1586970624, -1576009728, -1561116672, -1540669440, -1512849408,
                                     -1474805760, -1422901248, -1351974912, -1255194624, -1123024896,
                                     -942587904,  -696287232,  -359890944,  0,           0};
const std::vector<int32_t> kShapeQuarter{-420429314,  -1608302592, -1605156864, -1600880640, -1594982400,
                                         -1586970624, -1576009728, -1561116672, -1540669440, -1509002664,
                                         -1427519472, -1327922184, -1208281920, -1066542168, -900251136,
                                         -706900488,  -484035840,  -229536048,  0,           0};
/// A late hit of 0x50000000 50, 1000 and 5000 samples ago, then four blocks; the stage before the first, then
/// after each.
const std::vector<int32_t> kLate50{-729814812, -1336128260, -1321213320, -1297345400};
const std::vector<S> kLate50Stages{S::ATTACK, S::ATTACK, S::RELEASE, S::RELEASE, S::RELEASE};
const std::vector<int32_t> kLate1000{-1104626534, -1033333938, -952444524, -861971840};
const std::vector<S> kLate1000Stages{S::RELEASE, S::RELEASE, S::RELEASE, S::RELEASE, S::RELEASE};
const std::vector<int32_t> kLate5000{0, 0, 0, 0};
const std::vector<S> kLate5000Stages{S::OFF, S::OFF, S::OFF, S::OFF, S::OFF};
/// One hit, three blocks, then a late hit of 0x50000000 50 samples ago, four more.
const std::vector<int32_t> kLateMidDuck{-420429314,  -1603353912, -1585455984, -729814812,
                                        -1336128260, -1321213320, -1297345400};
/// Late hits 221, 222, 2500 and 2501 samples ago: the stage each lands in.
const std::vector<S> kLateEdgeStages{S::ATTACK, S::RELEASE, S::RELEASE, S::OFF};
/// Hits of 0x30000000 and 0x40000000 before one render, then four blocks.
const std::vector<int32_t> kTwoHits{-385393538, -1469741086, -1453334652, -1427079940};
/// combine_hit_strengths at (0, 2^30), (2^30, 0), (x, x), near full scale, at full scale, and across 2^30.
const std::vector<int32_t> kCombined{1073741824, 1073741824, 1207959552, 2013265919, 2147483646, 1610612735};

/// An envelope with its outputs and its stage after each render.
struct Rig {
	SidechainEnvelope envelope;
	std::vector<int32_t> out;
	std::vector<S> stages;

	void blocks(int count, uint32_t length, int32_t shape = 0, SidechainRates rates = kRates) {
		for (int i = 0; i < count; i++) {
			out.push_back(envelope.render(length, shape, rates));
			stages.push_back(envelope.stage());
		}
	}
};

/// @p count blocks of 128 after one hit of @p strength at @p shape.
std::vector<int32_t> afterHit(int32_t strength, int count, int32_t shape = 0) {
	Rig rig;
	rig.envelope.registerHit(strength);
	rig.blocks(count, 128, shape);
	return rig.out;
}

/// Four blocks of 128 after a late hit @p samples_ago, a pending hit dropped by it; the stage from before the first.
Rig late(uint32_t samples_ago) {
	Rig rig;
	rig.envelope.registerHit(ONE_Q31);
	rig.envelope.registerHitRetrospectively(0x50000000, samples_ago, kRates);
	rig.stages.push_back(rig.envelope.stage());
	rig.blocks(4, 128);
	return rig;
}

} // namespace

// clang-format off
describe sidechain_envelope("SidechainEnvelope", ${
	it("starts off, with no duck", _ {
		Rig rig;
		expect(rig.envelope.stage()).to_equal(S::OFF);
		rig.blocks(1, 128);
		expect(rig.out.front()).to_equal(0);
	});

	it("ducks through attack, release and off exactly, each stage handing over inside the block it ends in", _ {
		Rig rig;
		rig.envelope.registerHit(kHit);
		rig.blocks(40, 128);
		expect(rig.out).to_equal(kMain);
		expect(rig.stages).to_equal(kMainStages);
	});

	it("ignores a hit weaker than the duck under way", _ {
		Rig rig;
		rig.envelope.registerHit(kHit);
		rig.blocks(3, 128);
		rig.envelope.registerHit(0x20000000);
		rig.blocks(5, 128);
		expect(rig.out).to_equal(kWeaker);
		expect(rig.out).to_equal(afterHit(kHit, 8));
	});

	it("takes a stronger hit, attacking again from the level where it lands", _ {
		Rig rig;
		rig.envelope.registerHit(kHit);
		rig.blocks(3, 128);
		rig.envelope.registerHit(ONE_Q31);
		rig.blocks(5, 128);
		expect(rig.out).to_equal(kStronger);
		expect(rig.stages).to_equal(kStrongerStages);
	});

	it("jumps straight to release at the minimum attack, in blocks shorter than its 8 samples", _ {
		Rig rig;
		rig.envelope.registerHit(kHit);
		rig.blocks(6, 4, 0, kInstantRates);
		expect(rig.out).to_equal(kInstant);
		expect(rig.stages).to_equal(kInstantStages);
	});

	it("releases straight at the lowest shape, curved at the middle, clamped at the top", _ {
		expect(afterHit(kHit, 20, std::numeric_limits<int32_t>::min())).to_equal(kShapeMin);
		expect(afterHit(kHit, 20, 0x40000000)).to_equal(kShapeQuarter);
		expect(afterHit(kHit, 20, std::numeric_limits<int32_t>::max())).to_equal(kShapeMax);
	});

	it("places a late hit in its attack, dropping the pending hit", _ {
		const Rig rig = late(50);
		expect(rig.out).to_equal(kLate50);
		expect(rig.stages).to_equal(kLate50Stages);
	});

	it("places a late hit in its release", _ {
		const Rig rig = late(1000);
		expect(rig.out).to_equal(kLate1000);
		expect(rig.stages).to_equal(kLate1000Stages);
	});

	it("starts a late hit's attack from no duck, wherever the level stood", _ {
		Rig rig;
		rig.envelope.registerHit(kHit);
		rig.blocks(3, 128);
		rig.envelope.registerHitRetrospectively(0x50000000, 50, kRates);
		rig.blocks(4, 128);
		expect(rig.out).to_equal(kLateMidDuck);
	});

	it("places a late hit past its release as off", _ {
		const Rig rig = late(5000);
		expect(rig.out).to_equal(kLate5000);
		expect(rig.stages).to_equal(kLate5000Stages);
	});

	it("places a late hit at each stage's last and first sample", _ {
		std::vector<S> stages;
		for (const uint32_t samples_ago : {221u, 222u, 2500u, 2501u}) {
			SidechainEnvelope envelope;
			envelope.registerHitRetrospectively(0x50000000, samples_ago, kRates);
			stages.push_back(envelope.stage());
		}
		expect(stages).to_equal(kLateEdgeStages);
	});

	it("combines two hits registered before one render", _ {
		Rig rig;
		rig.envelope.registerHit(0x30000000);
		rig.envelope.registerHit(0x40000000);
		rig.blocks(4, 128);
		expect(rig.out).to_equal(kTwoHits);
		expect(rig.out).to_equal(afterHit(combine_hit_strengths(0x30000000, 0x40000000), 4));
	});

	it("combines hit strengths exactly, the sum clamped at full scale", _ {
		const std::vector<int32_t> combined{
		    combine_hit_strengths(0, 0x40000000),          combine_hit_strengths(0x40000000, 0),
		    combine_hit_strengths(0x30000000, 0x30000000), combine_hit_strengths(0x70000000, 0x60000000),
		    combine_hit_strengths(ONE_Q31, ONE_Q31),       combine_hit_strengths(0x3FFFFFFF, 0x40000001),
		};
		expect(combined).to_equal(kCombined);
		static_assert(combine_hit_strengths(0x70000000, 0x60000000) == 2013265919);
	});
});
// clang-format on

CPPSPEC_MAIN(sidechain_envelope);
