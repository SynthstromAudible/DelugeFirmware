// tests/spec_io/favourites_policy_spec.cpp
#include "model/favourite/favourites_policy.h"

#include "cppspec.hpp"

#include <deque>
#include <string>

using deluge::favourites::BankState;
using deluge::favourites::editAllowed;
using deluge::favourites::enqueueIntent;
using deluge::favourites::FavouritesIntent;
using deluge::favourites::loadIsCurrent;
using deluge::favourites::stateAfterLoad;
using deluge::storage::bytes::SettingsOutcome;

namespace {
FavouritesIntent save(const char* path, const char* bytes) {
	return {.kind = FavouritesIntent::Kind::Save, .path = path, .bytes = bytes};
}
FavouritesIntent load(const char* path, uint32_t generation) {
	return {.kind = FavouritesIntent::Kind::Load, .path = path, .generation = generation};
}
} // namespace

// clang-format off
describe favourites_policy("the favourites bank never writes less than the user had", $ {

	it("loads a parsed or genuinely absent bank", _ {
		expect(stateAfterLoad(SettingsOutcome::Parsed) == BankState::Loaded).to_equal(true);
		expect(stateAfterLoad(SettingsOutcome::Absent) == BankState::Loaded).to_equal(true);
	});

	// An unreadable card is never "absent": a bank that could not be read must not be editable, or the
	// next save replaces the favourites still on the card.
	it("makes an unreadable bank unavailable", _ {
		expect(stateAfterLoad(SettingsOutcome::Unreadable) == BankState::Unavailable).to_equal(true);
	});

	it("treats a write outcome delivered to a load as unavailable", _ {
		expect(stateAfterLoad(SettingsOutcome::Written) == BankState::Unavailable).to_equal(true);
		expect(stateAfterLoad(SettingsOutcome::WriteFailed) == BankState::Unavailable).to_equal(true);
	});

	it("allows edits only on a loaded bank", _ {
		expect(editAllowed(BankState::Loaded)).to_equal(true);
		expect(editAllowed(BankState::Loading)).to_equal(false);
		expect(editAllowed(BankState::Unavailable)).to_equal(false);
	});

	it("applies a load only for the generation it was asked for", _ {
		expect(loadIsCurrent(4, 4)).to_equal(true);
		expect(loadIsCurrent(4, 5)).to_equal(false);
	});

	it("keeps saves of different banks in order", _ {
		std::deque<FavouritesIntent> q;
		enqueueIntent(q, save("A", "a1"));
		enqueueIntent(q, save("B", "b1"));
		expect(q.size() == 2).to_equal(true);
		expect(q[0].path == "A" && q[1].path == "B").to_equal(true);
	});

	// Each save carries the whole bank, so the newer bytes for the same file are all that need to land.
	it("collapses a waiting save of the same bank to the newest bytes, in place", _ {
		std::deque<FavouritesIntent> q;
		enqueueIntent(q, save("A", "a1"));
		enqueueIntent(q, save("B", "b1"));
		enqueueIntent(q, save("A", "a2"));
		expect(q.size() == 2).to_equal(true);
		expect(q[0].path == "A" && q[0].bytes == "a2").to_equal(true);
		expect(q[1].path == "B" && q[1].bytes == "b1").to_equal(true);
	});

	// Only where the user settled matters for a load, and it must read what every waiting save wrote.
	it("keeps one waiting load, the newest, behind every waiting save", _ {
		std::deque<FavouritesIntent> q;
		enqueueIntent(q, save("A", "a1"));
		enqueueIntent(q, load("B", 1));
		enqueueIntent(q, save("C", "c1"));
		enqueueIntent(q, load("A", 2));
		expect(q.size() == 3).to_equal(true);
		expect(q[0].path == "A" && q[1].path == "C").to_equal(true);
		expect(q[2].kind == FavouritesIntent::Kind::Load && q[2].path == "A" && q[2].generation == 2).to_equal(true);
	});

	it("appends a save behind a waiting load", _ {
		std::deque<FavouritesIntent> q;
		enqueueIntent(q, load("A", 1));
		enqueueIntent(q, save("B", "b1"));
		expect(q.size() == 2).to_equal(true);
		expect(q[0].kind == FavouritesIntent::Kind::Load && q[1].kind == FavouritesIntent::Kind::Save).to_equal(true);
	});
});
// clang-format on

CPPSPEC_SPEC(favourites_policy)
