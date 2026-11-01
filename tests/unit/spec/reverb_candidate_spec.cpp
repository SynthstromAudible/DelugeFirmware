// The reverb auto duck's pick against a verbatim copy of the owner walk it replaces (updateReverbParams with
// getThingWithMostReverb's comparison): the song's own send starts it, a kit asks its channel before its drums, and a
// tie keeps the first owner.

#include "model/fx/reverb_candidate.h"

#include "cppspec.hpp"
#include <cstdint>
#include <optional>
#include <vector>

namespace {

/// One owner's send as the walk sees it: present (not automated, contains something) or not.
struct Send {
	int id;
	std::optional<int32_t> amount;
};

/// The walk as updateReverbParams and getThingWithMostReverb did it, over owners in walk order: the winner's id, or -1
/// when none beat the song's send.
int legacyWinner(int32_t songSend, const std::vector<Send>& walk) {
	int32_t highestReverbAmountFound = songSend;
	int winner = -1;
	for (const Send& s : walk) {
		if (s.amount && highestReverbAmountFound < *s.amount) {
			highestReverbAmountFound = *s.amount;
			winner = s.id;
		}
	}
	return winner;
}

int newWinner(int32_t songSend, const std::vector<Send>& walk) {
	deluge::model::ReverbCandidate best{.amount = songSend};
	int winner = -1;
	for (const Send& s : walk) {
		if (s.amount && deluge::model::beats(*s.amount, best)) {
			best.amount = *s.amount;
			best.found = true;
			winner = s.id;
		}
	}
	return winner;
}

} // namespace

// clang-format off
describe reverb_candidate("the reverb auto duck's pick", $ {
	it("keeps the first owner at a tie", _ {
		const std::vector<Send> walk{{1, 100}, {2, 100}};
		expect(newWinner(0, walk)).to_equal(legacyWinner(0, walk));
		expect(newWinner(0, walk)).to_equal(1);
	});
	it("starts from the song's own send", _ {
		const std::vector<Send> walk{{1, 50}, {2, 60}};
		expect(newWinner(60, walk)).to_equal(-1);
		expect(newWinner(59, walk)).to_equal(2);
	});
	it("skips owners whose send is automated or empty", _ {
		const std::vector<Send> walk{{1, std::nullopt}, {2, 10}};
		expect(newWinner(0, walk)).to_equal(legacyWinner(0, walk));
		expect(newWinner(0, walk)).to_equal(2);
	});
	it("asks a kit's channel before its drums", _ {
		// the kit's channel (10) then its rows (11, 12) at equal sends: the channel wins, as the legacy walk had it
		const std::vector<Send> walk{{10, 70}, {11, 70}, {12, 70}};
		expect(newWinner(0, walk)).to_equal(legacyWinner(0, walk));
		expect(newWinner(0, walk)).to_equal(10);
	});
	it("agrees with the legacy walk over a sweep", _ {
		for (int32_t song : {INT32_MIN, -1, 0, 1000}) {
			for (int32_t a : {INT32_MIN, -5, 0, 5, INT32_MAX}) {
				for (int32_t b : {INT32_MIN, -5, 0, 5, INT32_MAX}) {
					const std::vector<Send> walk{{1, a}, {2, std::nullopt}, {3, b}};
					expect(newWinner(song, walk)).to_equal(legacyWinner(song, walk));
				}
			}
		}
	});
});
// clang-format on

CPPSPEC_SPEC(reverb_candidate)
