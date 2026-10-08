#include "engines/effects/sidechain.h"

#include "dsp-next/fixed/dynamics/sidechain_envelope.h"
#include "util/lookuptables/lookuptables.h"

#include <cppspec.hpp>
#include <cstdint>
#include <vector>

using namespace deluge::effects;
using deluge::dsp::fixed::SidechainEnvelope;
using deluge::dsp::fixed::SidechainRates;
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

std::vector<int32_t> attackAndRelease(SidechainRates rates) {
	return {rates.attack, rates.release};
}

} // namespace

// clang-format off
describe sidechain("an owner's sidechain on the dsp-next envelope", ${
	it("passes the attack and release through unsynced, whatever the tempo", _ {
		const SidechainRates rates = Sidechain::rates(state(SyncLevel::NONE), kFastInverse);
		expect(rates.attack).to_equal(kAttack);
		expect(rates.release).to_equal(kRelease);
		expect(rates.instant_attack).to_be_false();
		expect(attackAndRelease(Sidechain::rates(state(SyncLevel::NONE), kSlowInverse)))
		    .to_equal(std::vector<int32_t>{kAttack, kRelease});
	});

	it("follows the tempo synced, the attack shifted right at the eighth level", _ {
		expect(attackAndRelease(Sidechain::rates(state(SyncLevel::EIGHTH), kFastInverse))).to_equal(kFastEighth);
		expect(attackAndRelease(Sidechain::rates(state(SyncLevel::EIGHTH), kSlowInverse))).to_equal(kSlowEighth);
	});

	it("follows the tempo synced, the attack shifted left at the 1/256 level", _ {
		expect(attackAndRelease(Sidechain::rates(state(SyncLevel::TWO_HUNDRED_FIFTY_SIXTH), kFastInverse)))
		    .to_equal(kFastTwoFiftySixth);
		expect(attackAndRelease(Sidechain::rates(state(SyncLevel::TWO_HUNDRED_FIFTY_SIXTH), kSlowInverse)))
		    .to_equal(kSlowTwoFiftySixth);
	});

	it("skips the attack exactly at the attack param's minimum", _ {
		expect(Sidechain::kInstantAttack).to_equal(attackRateTable[0] << 2);
		expect(Sidechain::rates(state(SyncLevel::NONE, Sidechain::kInstantAttack), kFastInverse).instant_attack)
		    .to_be_true();
		expect(Sidechain::rates(state(SyncLevel::EIGHTH, Sidechain::kInstantAttack), kFastInverse).instant_attack)
		    .to_be_true();
		expect(Sidechain::rates(state(SyncLevel::NONE, Sidechain::kInstantAttack + 1), kFastInverse).instant_attack)
		    .to_be_false();
		expect(Sidechain::rates(state(SyncLevel::NONE, Sidechain::kInstantAttack - 1), kFastInverse).instant_attack)
		    .to_be_false();
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

	it("renders as the envelope does at the same rates", _ {
		const SidechainState synced = state(SyncLevel::TWO_HUNDRED_FIFTY_SIXTH);
		Sidechain sidechain;
		SidechainEnvelope envelope;
		sidechain.registerHit(kHit);
		envelope.registerHit(kHit);
		std::vector<int32_t> through_sidechain;
		std::vector<int32_t> through_envelope;
		for (int i = 0; i < 20; i++) {
			through_sidechain.push_back(sidechain.render(16, 0x40000000, synced, kSlowInverse));
			through_envelope.push_back(envelope.render(16, 0x40000000, Sidechain::rates(synced, kSlowInverse)));
		}
		expect(through_sidechain).to_equal(through_envelope);
		expect(through_sidechain.front()).not_().to_equal(0);
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
