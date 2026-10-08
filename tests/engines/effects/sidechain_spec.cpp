#include "engines/effects/sidechain.h"

#include "dsp-next/core/control.h"
#include "dsp-next/fixed/dynamics/duck_envelope.h"
#include "util/lookuptables/lookuptables.h"

#include <cppspec.hpp>
#include <cstdint>
#include <limits>
#include <vector>

using namespace deluge::effects;
using deluge::dsp::Control;
using deluge::dsp::fixed::DuckEnvelope;
using deluge::engine::SyncLevel;

namespace {

/// attackRateTable[20] << 2: 222 samples unsynced.
constexpr int32_t kAttack = 37640;
/// releaseRateTable[10] << 3: 2279 samples unsynced.
constexpr int32_t kRelease = 3680;
/// Two tick inverses, a faster tempo and a slower one.
constexpr uint32_t kFastInverse = 6492772;
constexpr uint32_t kSlowInverse = 2597109;
constexpr int32_t kHit = 0x60000000;
/// The straightest release shape.
constexpr int32_t kStraightShape = std::numeric_limits<int32_t>::min();
/// A late hit's age that lands it in the release at the eighth level and the fast inverse, where the shape bends the
/// level: the attack lasts 575 samples there.
constexpr uint32_t kReleaseAge = 1000;

/// The resolved {attack, release} at each inverse, at the eighth level (the attack shifted right by 3) and at the
/// 1/256 level (the attack shifted left by 2).
const std::vector<int32_t> kFastEighth{14566, 1424};
const std::vector<int32_t> kFastTwoFiftySixth{466132, 45573};
const std::vector<int32_t> kSlowEighth{5826, 569};
const std::vector<int32_t> kSlowTwoFiftySixth{186452, 18229};

/// One hit, then 50 blocks of 128 at the eighth level and the fast inverse.
const std::vector<int32_t> kSyncedRender{
    -91475906,   -260876546,  -574626050,  -1155640322, -1609082976, -1605972528, -1601241852, -1594900560, -1586958264,
    -1577424576, -1566267432, -1553499648, -1539094500, -1523029536, -1505361384, -1485960720, -1464926916, -1442248200,
    -1417850808, -1391737920, -1363947984, -1334459592, -1303231020, -1270288944, -1235584140, -1199187096, -1161045360,
    -1121174880, -1079581968, -1036290024, -991339536,  -944749584,  -896528520,  -846787320,  -795541740,  -742929408,
    -689030136,  -634000680,  -578005200,  -521231280,  -463954128,  -406465920,  -349125000,  -292360128,  -236702592,
    -182747544,  -131240796,  -83034720,   -39150384,   -788184};
/// A late hit 300 samples ago at the same settings (in its attack), then four blocks of 128.
const std::vector<int32_t> kSyncedLate{-735523898, -1453628270, -1609082976, -1605972528};

SidechainState state(SyncLevel level, int32_t attack = kAttack) {
	return {.attack = attack, .release = kRelease, .sync_level = level};
}

std::vector<int32_t> attackAndRelease(const DuckEnvelope::Settings& settings) {
	return {settings.attack, settings.release};
}

/// Three blocks of 128 at @p shape, a hit kReleaseAge samples ago, three more at @p shape: through a Sidechain.
std::vector<int32_t> lateHitBetweenBlocks(int32_t shape) {
	const SidechainState synced = state(SyncLevel::EIGHTH);
	Sidechain sidechain;
	std::vector<int32_t> out;
	for (int i = 0; i < 3; i++) {
		out.push_back(sidechain.render(128, shape, synced, kFastInverse));
	}
	sidechain.registerHitRetrospectively(kHit, kReleaseAge, synced, kFastInverse);
	for (int i = 0; i < 3; i++) {
		out.push_back(sidechain.render(128, shape, synced, kFastInverse));
	}
	return out;
}

} // namespace

// clang-format off
describe sidechain("an owner's sidechain on the dsp-next duck envelope", ${
	it("passes the attack and release through unsynced, whatever the tempo", _ {
		const DuckEnvelope::Settings settings = Sidechain::settings(state(SyncLevel::NONE), kFastInverse, 0);
		expect(settings.attack).to_equal(kAttack);
		expect(settings.release).to_equal(kRelease);
		expect(settings.instant_attack).to_be_false();
		expect(attackAndRelease(Sidechain::settings(state(SyncLevel::NONE), kSlowInverse, 0)))
		    .to_equal(std::vector<int32_t>{kAttack, kRelease});
	});

	it("follows the tempo synced, the attack shifted right at the eighth level", _ {
		expect(attackAndRelease(Sidechain::settings(state(SyncLevel::EIGHTH), kFastInverse, 0))).to_equal(kFastEighth);
		expect(attackAndRelease(Sidechain::settings(state(SyncLevel::EIGHTH), kSlowInverse, 0))).to_equal(kSlowEighth);
	});

	it("follows the tempo synced, the attack shifted left at the 1/256 level", _ {
		expect(attackAndRelease(Sidechain::settings(state(SyncLevel::TWO_HUNDRED_FIFTY_SIXTH), kFastInverse, 0)))
		    .to_equal(kFastTwoFiftySixth);
		expect(attackAndRelease(Sidechain::settings(state(SyncLevel::TWO_HUNDRED_FIFTY_SIXTH), kSlowInverse, 0)))
		    .to_equal(kSlowTwoFiftySixth);
	});

	it("skips the attack exactly at the attack param's minimum", _ {
		expect(Sidechain::kInstantAttack).to_equal(attackRateTable[0] << 2);
		expect(Sidechain::settings(state(SyncLevel::NONE, Sidechain::kInstantAttack), kFastInverse, 0).instant_attack)
		    .to_be_true();
		expect(Sidechain::settings(state(SyncLevel::EIGHTH, Sidechain::kInstantAttack), kFastInverse, 0).instant_attack)
		    .to_be_true();
		expect(Sidechain::settings(state(SyncLevel::NONE, Sidechain::kInstantAttack + 1), kFastInverse, 0)
		           .instant_attack)
		    .to_be_false();
		expect(Sidechain::settings(state(SyncLevel::NONE, Sidechain::kInstantAttack - 1), kFastInverse, 0)
		           .instant_attack)
		    .to_be_false();
	});

	it("passes the shape through to the duck's settings", _ {
		expect(Sidechain::settings(state(SyncLevel::NONE), kFastInverse, kStraightShape).shape).to_equal(kStraightShape);
		expect(Sidechain::settings(state(SyncLevel::EIGHTH), kFastInverse, 0x40000000).shape).to_equal(0x40000000);
	});

	it("renders a synced duck exactly", _ {
		Sidechain sidechain;
		sidechain.registerHit(kHit);
		std::vector<int32_t> out;
		for (int i = 0; i < 50; i++) {
			out.push_back(sidechain.render(128, 0, state(SyncLevel::EIGHTH), kFastInverse));
		}
		expect(out).to_equal(kSyncedRender);
	});

	it("renders as the duck envelope does at the same settings", _ {
		const SidechainState synced = state(SyncLevel::TWO_HUNDRED_FIFTY_SIXTH);
		Sidechain sidechain;
		Control<DuckEnvelope> duck;
		sidechain.registerHit(kHit);
		duck.on<&DuckEnvelope::hit>(kHit);
		std::vector<int32_t> through_sidechain;
		std::vector<int32_t> through_duck;
		for (int i = 0; i < 20; i++) {
			through_sidechain.push_back(sidechain.render(16, 0x40000000, synced, kSlowInverse));
			duck.configure(Sidechain::settings(synced, kSlowInverse, 0x40000000));
			through_duck.push_back(duck.advance(16));
		}
		expect(through_sidechain).to_equal(through_duck);
		expect(through_sidechain.front()).not_().to_equal(0);
	});

	it("keeps the shape last rendered at when it places a late hit", _ {
		const SidechainState synced = state(SyncLevel::EIGHTH);
		Control<DuckEnvelope> duck;
		std::vector<int32_t> through_duck;
		for (int i = 0; i < 3; i++) {
			duck.configure(Sidechain::settings(synced, kFastInverse, kStraightShape));
			through_duck.push_back(duck.advance(128));
		}
		duck.on<&DuckEnvelope::hit_at>(kHit, kReleaseAge);
		for (int i = 0; i < 3; i++) {
			duck.configure(Sidechain::settings(synced, kFastInverse, kStraightShape));
			through_duck.push_back(duck.advance(128));
		}

		const std::vector<int32_t> straight = lateHitBetweenBlocks(kStraightShape);
		expect(straight).to_equal(through_duck);
		// The late hit lands in the release, where the shape bends the level: the shape is what these blocks test.
		expect(straight).not_().to_equal(lateHitBetweenBlocks(0));
	});

	it("places a late hit at the synced rates", _ {
		Sidechain sidechain;
		sidechain.registerHitRetrospectively(kHit, 300, state(SyncLevel::EIGHTH), kFastInverse);
		std::vector<int32_t> out;
		for (int i = 0; i < 4; i++) {
			out.push_back(sidechain.render(128, 0, state(SyncLevel::EIGHTH), kFastInverse));
		}
		expect(out).to_equal(kSyncedLate);
	});
});
// clang-format on

CPPSPEC_SPEC(sidechain)
