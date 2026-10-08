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

// DuckEnvelope through Control<DuckEnvelope>: a hit dips the level, which recovers over an attack then a curved
// release. Exact reference values pin the attack, the release at three shapes, the stage changes inside a block, a
// weaker hit ignored, a stronger one taken, an instant attack, late hits and combined hits; then the hand-over's
// dropped overrun, a rate change mid-release, an unconfigured duck, and late hits at zero rates. Rates are as an
// owner resolves them: an attack of 37640 per sample (222 samples), a release of 3680 (2279 samples). Each block
// configures before it advances, as the owner does.

#include "dsp-next/fixed/dynamics/duck_envelope.h"

#include "dsp-next/core/concepts.h"
#include "dsp-next/core/control.h"
#include "dsp-next/core/kernel.h"

#include <cppspec.hpp>
#include <cstdint>
#include <limits>
#include <ostream>
#include <type_traits>
#include <vector>

using namespace deluge::dsp;
using namespace deluge::dsp::fixed;
using S = DuckEnvelope::Stage;
using Settings = DuckEnvelope::Settings;

namespace deluge::dsp::fixed {
/// A stage, printed by name, so a failing comparison of stage lists shows it.
std::ostream& operator<<(std::ostream& out, DuckEnvelope::Stage stage) {
	switch (stage) {
	case DuckEnvelope::Stage::OFF:
		return out << "OFF";
	case DuckEnvelope::Stage::ATTACK:
		return out << "ATTACK";
	case DuckEnvelope::Stage::RELEASE:
		return out << "RELEASE";
	}
	return out;
}
} // namespace deluge::dsp::fixed

namespace {

static_assert(control_kernel<DuckEnvelope>);
static_assert(control_generator<Control<DuckEnvelope>>);
static_assert(!std::is_polymorphic_v<Control<DuckEnvelope>>);

// The release's curve extent: the shape biased to 0 at INT32_MIN, in curvedness units of 2^16. The middle shape is
// exactly one unit (fully curved at the release's start); the top is just under two.
static_assert(DuckEnvelope::coefficients({.shape = std::numeric_limits<q31>::min()}).curve_extent16 == 0);
static_assert(DuckEnvelope::coefficients({.shape = 0}).curve_extent16 == 65536);
static_assert(DuckEnvelope::coefficients({.shape = std::numeric_limits<q31>::max()}).curve_extent16 == 131071);

constexpr Settings kRates{.attack = 37640, .release = 3680, .shape = 0, .instant_attack = false};
/// The minimum attack: a 2^23 stage in 8 samples.
constexpr Settings kInstantRates{.attack = 1048576, .release = 3680, .shape = 0, .instant_attack = true};
constexpr q31 kHit = 0x60000000;

/// One hit, then 40 blocks of 128, and the stage after each.
const std::vector<q31> kMain{
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
const std::vector<q31> kWeaker{-420429314,  -1603353912, -1585455984, -1556814480,
                               -1517238720, -1466648424, -1404845568, -1331730312};
/// One hit, three blocks, a hit as deep as the level held (strength 1585455984), five more.
constexpr q31 kEqualStrength = 1585455984;
const std::vector<q31> kEqual{-420429314,  -1603353912, -1585455984, -1556814480,
                              -1517238720, -1466648424, -1404845568, -1331730312};
/// One hit, three blocks, a full-scale hit, five more; and the stages.
const std::vector<q31> kStronger{-420429314,  -1603353912, -1585455984, -1732165928,
                                 -2137805216, -2113941312, -2075752640, -2022984960};
const std::vector<S> kStrongerStages{S::ATTACK,  S::RELEASE, S::RELEASE, S::ATTACK,
                                     S::RELEASE, S::RELEASE, S::RELEASE, S::RELEASE};
/// One hit at the minimum attack, then six blocks of 4; and the stages.
const std::vector<q31> kInstant{-1610558754, -1610494940, -1610421552, -1610338158, -1610196644, -1610093758};
const std::vector<S> kInstantStages{S::RELEASE, S::RELEASE, S::RELEASE, S::RELEASE, S::RELEASE, S::RELEASE};
/// One hit, then 20 blocks at shapes INT32_MIN, INT32_MAX and 2^30.
const std::vector<q31> kShapeMin{-420429314,  -1520173056, -1429733376, -1339293696, -1248854016,
                                 -1158414336, -1067974656, -977534976,  -887095296,  -796655616,
                                 -706215936,  -615776256,  -525336576,  -434896896,  -344457216,
                                 -254017536,  -163577856,  -73138176,   0,           0};
const std::vector<q31> kShapeMax{-420429314,  -1608302592, -1605156864, -1600880640, -1594982400,
                                 -1586970624, -1576009728, -1561116672, -1540669440, -1512849408,
                                 -1474805760, -1422901248, -1351974912, -1255194624, -1123024896,
                                 -942587904,  -696287232,  -359890944,  0,           0};
const std::vector<q31> kShapeQuarter{-420429314,  -1608302592, -1605156864, -1600880640, -1594982400,
                                     -1586970624, -1576009728, -1561116672, -1540669440, -1509002664,
                                     -1427519472, -1327922184, -1208281920, -1066542168, -900251136,
                                     -706900488,  -484035840,  -229536048,  0,           0};
/// A late hit of 0x50000000 50, 1000 and 5000 samples ago, then four blocks; the stage before the first, then
/// after each.
const std::vector<q31> kLate50{-729814812, -1336128260, -1321213320, -1297345400};
const std::vector<S> kLate50Stages{S::ATTACK, S::ATTACK, S::RELEASE, S::RELEASE, S::RELEASE};
const std::vector<q31> kLate1000{-1104626534, -1033333938, -952444524, -861971840};
const std::vector<S> kLate1000Stages{S::RELEASE, S::RELEASE, S::RELEASE, S::RELEASE, S::RELEASE};
const std::vector<q31> kLate5000{0, 0, 0, 0};
const std::vector<S> kLate5000Stages{S::OFF, S::OFF, S::OFF, S::OFF, S::OFF};
/// One hit, three blocks, then a late hit of 0x50000000 50 samples ago, four more.
const std::vector<q31> kLateMidDuck{-420429314,  -1603353912, -1585455984, -729814812,
                                    -1336128260, -1321213320, -1297345400};
/// Late hits 221, 222, 2500 and 2501 samples ago: the stage each lands in.
const std::vector<S> kLateEdgeStages{S::ATTACK, S::RELEASE, S::RELEASE, S::OFF};
/// Hits of 0x30000000 and 0x40000000 before one render, then four blocks.
const std::vector<q31> kTwoHits{-385393538, -1469741086, -1453334652, -1427079940};
/// Stopped one block into a duck with a weaker hit pending (dropped against the duck's level), then that hit again.
const std::vector<q31> kStopped{-420429314, 0, 0, -70071554, -267225652};
/// combine_hit_strengths at (0, 2^30), (2^30, 0), (x, x), near full scale, at full scale, and across 2^30.
const std::vector<q31> kCombined{1073741824, 1073741824, 1207959552, 2013265919, 2147483646, 1610612735};

/// @p rates at the release shape @p shape.
constexpr Settings at_shape(Settings rates, q31 shape) {
	rates.shape = shape;
	return rates;
}

/// A duck with its outputs and its stage after each block.
struct Rig {
	Control<DuckEnvelope> duck;
	std::vector<q31> out;
	std::vector<S> stages;

	/// @p count blocks of @p length, each configured at @p rates and @p shape before it advances.
	void blocks(int count, std::size_t length, q31 shape = 0, Settings rates = kRates) {
		for (int i = 0; i < count; i++) {
			duck.configure(at_shape(rates, shape));
			out.push_back(duck.advance(length));
			stages.push_back(duck.state().stage);
		}
	}
};

/// @p count blocks of 128 after one hit of @p strength at @p shape.
std::vector<q31> afterHit(q31 strength, int count, q31 shape = 0) {
	Rig rig;
	rig.duck.on<&DuckEnvelope::hit>(strength);
	rig.blocks(count, 128, shape);
	return rig.out;
}

/// A late hit of 0x50000000 @p samples_ago at kRates.
void hitLate(Control<DuckEnvelope>& duck, uint32_t samples_ago) {
	duck.configure(kRates);
	duck.on<&DuckEnvelope::hit_at>(q31{0x50000000}, samples_ago);
}

/// Four blocks of 128 after a late hit @p samples_ago, a pending hit dropped by it; the stage from before the first.
Rig late(uint32_t samples_ago) {
	Rig rig;
	rig.duck.on<&DuckEnvelope::hit>(ONE_Q31);
	hitLate(rig.duck, samples_ago);
	rig.stages.push_back(rig.duck.state().stage);
	rig.blocks(4, 128);
	return rig;
}

} // namespace

// clang-format off
describe duck_envelope("DuckEnvelope", ${
	it("starts off, with no duck", _ {
		Rig rig;
		expect(rig.duck.state().stage).to_equal(S::OFF);
		rig.blocks(1, 128);
		expect(rig.out.front()).to_equal(0);
	});

	it("ducks through attack, release and off exactly, each stage handing over inside the block it ends in", _ {
		Rig rig;
		rig.duck.on<&DuckEnvelope::hit>(kHit);
		rig.blocks(40, 128);
		expect(rig.out).to_equal(kMain);
		expect(rig.stages).to_equal(kMainStages);
	});

	it("ignores a hit weaker than the duck under way", _ {
		Rig rig;
		rig.duck.on<&DuckEnvelope::hit>(kHit);
		rig.blocks(3, 128);
		rig.duck.on<&DuckEnvelope::hit>(q31{0x20000000});
		rig.blocks(5, 128);
		expect(rig.out).to_equal(kWeaker);
		expect(rig.out).to_equal(afterHit(kHit, 8));
	});

	it("ignores a hit exactly as deep as the level held", _ {
		Rig rig;
		rig.duck.on<&DuckEnvelope::hit>(kHit);
		rig.blocks(3, 128);
		const q31 held = rig.out.back() + ONE_Q31;
		expect(ONE_Q31 - held).to_equal(kEqualStrength);
		expect(rig.duck.state().level).to_equal(held);
		rig.duck.on<&DuckEnvelope::hit>(kEqualStrength);
		rig.blocks(5, 128);
		expect(rig.out).to_equal(kEqual);
		expect(rig.out).to_equal(afterHit(kHit, 8));
	});

	it("takes a stronger hit, attacking again from the level where it lands", _ {
		Rig rig;
		rig.duck.on<&DuckEnvelope::hit>(kHit);
		rig.blocks(3, 128);
		rig.duck.on<&DuckEnvelope::hit>(ONE_Q31);
		rig.blocks(5, 128);
		expect(rig.out).to_equal(kStronger);
		expect(rig.stages).to_equal(kStrongerStages);
	});

	it("jumps straight to release at the minimum attack, in blocks shorter than its 8 samples", _ {
		Rig rig;
		rig.duck.on<&DuckEnvelope::hit>(kHit);
		rig.blocks(6, 4, 0, kInstantRates);
		expect(rig.out).to_equal(kInstant);
		expect(rig.stages).to_equal(kInstantStages);
	});

	it("releases straight at the lowest shape, curved at the middle, clamped at the top", _ {
		expect(afterHit(kHit, 20, std::numeric_limits<q31>::min())).to_equal(kShapeMin);
		expect(afterHit(kHit, 20, 0x40000000)).to_equal(kShapeQuarter);
		expect(afterHit(kHit, 20, std::numeric_limits<q31>::max())).to_equal(kShapeMax);
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
		rig.duck.on<&DuckEnvelope::hit>(kHit);
		rig.blocks(3, 128);
		hitLate(rig.duck, 50);
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
			Control<DuckEnvelope> duck;
			hitLate(duck, samples_ago);
			stages.push_back(duck.state().stage);
		}
		expect(stages).to_equal(kLateEdgeStages);
	});

	it("stops the duck, weighing a pending hit against the level it stood at", _ {
		Rig rig;
		rig.duck.on<&DuckEnvelope::hit>(kHit);
		rig.blocks(1, 128);
		rig.duck.on<&DuckEnvelope::stop>();
		expect(rig.duck.state().stage).to_equal(S::OFF);
		rig.duck.on<&DuckEnvelope::hit>(q31{0x10000000});
		rig.blocks(2, 128);
		rig.duck.on<&DuckEnvelope::hit>(q31{0x10000000});
		rig.blocks(2, 128);
		expect(rig.out).to_equal(kStopped);
	});

	it("combines two hits registered before one block", _ {
		Rig rig;
		rig.duck.on<&DuckEnvelope::hit>(q31{0x30000000});
		rig.duck.on<&DuckEnvelope::hit>(q31{0x40000000});
		expect(rig.duck.state().pending.strength).to_equal(combine_hit_strengths(0x30000000, 0x40000000));
		rig.blocks(4, 128);
		expect(rig.out).to_equal(kTwoHits);
		expect(rig.out).to_equal(afterHit(combine_hit_strengths(0x30000000, 0x40000000), 4));
		expect(rig.duck.state().pending.empty()).to_be_true();
	});

	it("combines hit strengths exactly, the sum clamped at full scale", _ {
		const std::vector<q31> combined{
		    combine_hit_strengths(0, 0x40000000),          combine_hit_strengths(0x40000000, 0),
		    combine_hit_strengths(0x30000000, 0x30000000), combine_hit_strengths(0x70000000, 0x60000000),
		    combine_hit_strengths(ONE_Q31, ONE_Q31),       combine_hit_strengths(0x3FFFFFFF, 0x40000001),
		};
		expect(combined).to_equal(kCombined);
		static_assert(combine_hit_strengths(0x70000000, 0x60000000) == 2013265919);
		static_assert(PendingHit{}.with(0x30000000).with(0x40000000).strength
		              == combine_hit_strengths(0x30000000, 0x40000000));
	});

	it("restarts the release at position 0 when the attack ends one sample into a block, dropping the overrun", _ {
		// At kRates the attack steps 37640 per sample: after 222 samples it stands at 222 * 37640 = 8356080, short of
		// the 2^23 = 8388608 stage, and its 223rd sample takes it past the end. A first block of 222 leaves it in
		// ATTACK; a second of 128 ends it at that block's first sample, the block taking the attack to 8356080 +
		// 4817920, 4785392 past the end. The release then starts at 0 and advances by the whole block, 128 * 3680 =
		// 471040: not by the 127 samples the attack left over, and not from the attack's overrun.
		Rig rig;
		rig.duck.on<&DuckEnvelope::hit>(kHit);
		rig.blocks(1, 222);
		expect(rig.duck.state().stage).to_equal(S::ATTACK);
		expect(rig.duck.state().pos).to_equal(8356080U);
		rig.blocks(1, 128);
		expect(rig.duck.state().stage).to_equal(S::RELEASE);
		expect(rig.duck.state().pos).to_equal(471040U);
		const DuckEnvelope::State& state = rig.duck.state();
		expect(rig.out.back())
		    .to_equal(release_level(state.depth, state.pos, rig.duck.coefficients().curve_extent16) - ONE_Q31);
	});

	it("uses a new release rate from the next block, the release's position carried over", _ {
		// One hit, 5 blocks of 128 at kRates, then 5 at twice the release. The first block attacks to 128 * 37640 =
		// 4817920; the second ends the attack and releases 128 * 3680 = 471040 from 0; each later block adds 471040,
		// then 128 * 7360 = 942080 once the rate doubles.
		Rig rig;
		rig.duck.on<&DuckEnvelope::hit>(kHit);
		Settings doubled = kRates;
		doubled.release = kRates.release * 2;
		std::vector<uint32_t> positions;
		for (int i = 0; i < 10; i++) {
			rig.blocks(1, 128, 0, i < 5 ? kRates : doubled);
			positions.push_back(rig.duck.state().pos);
		}
		const std::vector<uint32_t> expected{4817920, 471040,  942080,  1413120, 1884160,
		                                     2826240, 3768320, 4710400, 5652480, 6594560};
		expect(positions).to_equal(expected);
		expect(rig.stages.back()).to_equal(S::RELEASE);
	});

	it("advances a duck never configured as no duck, staying off", _ {
		Control<DuckEnvelope> duck;
		expect(duck.advance(128)).to_equal(0);
		expect(duck.state().stage).to_equal(S::OFF);
		expect(duck.state().level).to_equal(ONE_Q31);
	});

	it("places a late hit at a zero rate in that stage, at its start, as a stage that never ends", _ {
		// Only an envelope never configured has a zero rate; the owner configures before every block. A zero-rate
		// stage never ends, so placement divides by no zero: the hit stays in it, at position 0.
		Control<DuckEnvelope> unconfigured;
		unconfigured.on<&DuckEnvelope::hit_at>(q31{0x50000000}, uint32_t{1000});
		expect(unconfigured.state().stage).to_equal(S::ATTACK);
		expect(unconfigured.state().pos).to_equal(0U);

		Control<DuckEnvelope> no_release{Settings{.attack = 37640, .release = 0}};
		no_release.on<&DuckEnvelope::hit_at>(q31{0x50000000}, uint32_t{1000});
		expect(no_release.state().stage).to_equal(S::RELEASE);
		expect(no_release.state().pos).to_equal(0U);
	});
});
// clang-format on

CPPSPEC_MAIN(duck_envelope);
