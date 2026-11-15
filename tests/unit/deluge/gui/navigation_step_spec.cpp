#include "gui/ui/load/navigation_step.h"

#include <array>
#include <variant>

#include "cppspec.hpp"
#include "support/spec_main.h"

using namespace deluge::gui;

namespace {
constexpr NavigationItem F{};                               // free
constexpr NavigationItem S{.isOld = false, .inSong = true}; // already in the song
constexpr NavigationItem O{.isOld = true, .inSong = true};  // the old instrument

template <size_t N>
NavigationWindow window(const std::array<NavigationItem, N>& items, int32_t atStart = 0, int32_t atEnd = 0) {
	return {.items = items, .deletedAtStart = atStart, .deletedAtEnd = atEnd};
}

void checkLand(auto& self, const NavigationDecision& d, int32_t index, int32_t wraps) {
	const Land* land = std::get_if<Land>(&d);
	expect(land).not_().to_be_null();
	if (land == nullptr) {
		return;
	}
	expect(land->index).to_equal(index);
	expect(land->wraps).to_equal(wraps);
}

void checkPileLand(auto& self, const PileDecision& d, int32_t index) {
	const Land* land = std::get_if<Land>(&d);
	expect(land).not_().to_be_null();
	if (land == nullptr) {
		return;
	}
	expect(land->index).to_equal(index);
}
} // namespace

// clang-format off
describe navigation_step_group("navigation step", ${
	// ── Single gotos, landing where main's doPresetNavigation lands ──────────────
	it("forward lands on the first", _ {
		const std::array items{F, F, F};
		checkLand(self, decideNavigation(window(items), 1, false, true, 0), 0, 0);
	});

	it("backward lands on the last", _ {
		const std::array items{F, F, F};
		checkLand(self, decideNavigation(window(items), -1, false, true, 0), 2, 0);
	});

	it("an empty window with a name reads from the end", _ {
		const std::array<NavigationItem, 0> items{};
		const auto d = decideNavigation(window(items), 1, true, true, 0);
		expect(std::holds_alternative<FromEnd>(d)).to_be_true();
		expect(std::get<FromEnd>(d).wraps).to_equal(1);
	});

	it("an empty window without a name gets out", _ {
		const std::array<NavigationItem, 0> items{};
		expect(std::holds_alternative<GetOut>(decideNavigation(window(items), 1, true, false, 0))).to_be_true();
	});

	it("only the old instrument reads from the end", _ {
		const std::array items{O};
		expect(std::holds_alternative<FromEnd>(decideNavigation(window(items), 1, true, true, 0))).to_be_true();
	});

	it("only the old instrument gets out without a name", _ {
		const std::array items{O};
		expect(std::holds_alternative<GetOut>(decideNavigation(window(items), 1, true, false, 0))).to_be_true();
	});

	it("in song is landed on when not skipping", _ {
		const std::array items{S, F};
		checkLand(self, decideNavigation(window(items), 1, false, true, 0), 0, 0);
	});

	it("skips in song within the window", _ {
		const std::array items{S, S, F};
		checkLand(self, decideNavigation(window(items), 1, true, true, 0), 2, 0);
	});

	it("skips in song backward", _ {
		const std::array items{F, S, S};
		checkLand(self, decideNavigation(window(items), -1, true, true, 0), 0, 0);
	});

	it("a wrap with nothing trimmed stays in the window", _ {
		// Off the end with nothing trimmed on either side: round to the start in the same window, no read.
		// (A wrap always restarts at the edge, so in one window it only ever lands there, once the cap stops skipping.)
		const std::array items{S};
		checkLand(self, decideNavigation(window(items), 1, true, true, 0), 0, 2);
	});

	it("wrap with the far side trimmed reads from the end", _ {
		// At the folder's end (nothing after), start trimmed: the wrap has to read the start back in.
		const std::array items{S};
		const auto d = decideNavigation(window(items, 3, 0), 1, true, true, 0);
		expect(std::holds_alternative<FromEnd>(d)).to_be_true();
		expect(std::get<FromEnd>(d).wraps).to_equal(1);
	});

	it("skipping stops after two wraps in one window", _ {
		const std::array items{S, S};
		checkLand(self, decideNavigation(window(items), 1, true, true, 0), 0, 2);
	});

	// ── Paging ────────────────────────────────────────────────────────
	it("pages on from the last entry shown", _ {
		// Pages on from the last entry the window SHOWS -- not from its search name, which would re-read
		// the same window forever, and not from FileListing::lastRemaining, which dropFoldersAndDuplicates
		// pops off the window and the next read (which excludes its own search name) would then skip.
		const std::array items{NavigationItem{.inSong = true, .name = "NAV01.XML"},
		                       NavigationItem{.inSong = true, .name = "NAV09.XML"}};
		const auto d = decideNavigation(window(items, 0, 5), 1, true, true, 0);
		expect(std::holds_alternative<PageOn>(d)).to_be_true();
		expect(std::get<PageOn>(d).from).to_equal(std::string{"NAV09.XML"});
		expect(std::get<PageOn>(d).wraps).to_equal(0);
	});

	it("pages back from the first entry shown", _ {
		const std::array items{NavigationItem{.inSong = true, .name = "NAV15.XML"},
		                       NavigationItem{.inSong = true, .name = "NAV23.XML"}};
		const auto d = decideNavigation(window(items, 5, 0), -1, true, true, 0);
		expect(std::holds_alternative<PageOn>(d)).to_be_true();
		expect(std::get<PageOn>(d).from).to_equal(std::string{"NAV15.XML"});
	});

	it("skipping stops once the walk has wrapped twice", _ {
		const std::array items{S, F};
		checkLand(self, decideNavigation(window(items), 1, true, true, 2), 0, 0);
	});

	it("wraps so far count towards the cap", _ {
		// One wrap already spent: one more inside this window reaches the cap, and it lands where it stands.
		const std::array items{S, S};
		checkLand(self, decideNavigation(window(items), 1, true, true, 1), 0, 1);
	});

	it("whole folder in song lands after two wraps", _ {
		const std::array items{S, S, S};
		checkLand(self, decideNavigation(window(items), -1, true, true, 0), 2, 2);
	});

	// ── A pile of detents, moved from the landed entry as main's moveAgain does ───
	it("a pile moves by all its detents inside the window", _ {
		const std::array items{F, F, F, F, F};
		checkPileLand(self, movePile(window(items, 3, 4), 0 + 3, 3, false, 0), 3);
		checkPileLand(self, movePile(window(items, 3, 4), 4 - 2, -2, false, 0), 2);
	});

	it("a pile past a trimmed edge reads again from the name", _ {
		const std::array items{F, F, F};
		expect(std::holds_alternative<ReadAgain>(movePile(window(items, 0, 4), 1 + 5, 5, false, 0))).to_be_true();
		expect(std::holds_alternative<ReadAgain>(movePile(window(items, 4, 0), 1 - 5, -5, false, 0))).to_be_true();
	});

	it("a pile past the folder end reads from the far end", _ {
		// NAV23 landed in the window read from NAV22 (NAV23, NAV24; NAV00..NAV22 behind it): three on is past the end.
		const std::array items{F, F};
		const auto d = movePile(window(items, 23, 0), 0 + 3, 3, false, 0);
		expect(std::holds_alternative<FromEnd>(d)).to_be_true();
	});

	it("a pile past the end of a whole folder wraps to its start not on by the rest", _ {
		const std::array items{F, F, F, F};
		checkPileLand(self, movePile(window(items), 2 + 5, 5, false, 0), 0);
		checkPileLand(self, movePile(window(items), 1 - 5, -5, false, 0), 3);
	});

	it("a preset in the song is skipped by the whole pile again", _ {
		// From index 0, a pile of 2 lands on 2, which is in the song: main moves on by 2 again, to 4.
		const std::array items{F, F, S, F, F, F};
		checkPileLand(self, movePile(window(items), 0 + 2, 2, true, 0), 4);
	});

	it("a pile skips in song presets only until it has wrapped twice", _ {
		// 1, 2, past the end (one wrap), 0, 1, 2, past the end again (two): it stops at 0.
		const std::array items{S, S, S};
		checkPileLand(self, movePile(window(items), 1, 1, true, 0), 0);
	});

	it("a pile keeps the wraps its landing made in the same window", _ {
		// Main's `wrapped` is reset only by a read. From 2, past the end is a landing's second wrap when it had wrapped
		// once already, so the pile stops on 0 there; with none made it skips on to 1.
		const std::array items{S, F, S};
		checkPileLand(self, movePile(window(items), 2, 1, true, 1), 0);
		checkPileLand(self, movePile(window(items), 2, 1, true, 0), 1);
	});

	it("a read for a pile starts at the near end", _ {
		const std::array items{F, F, F};
		checkPileLand(self, startPileWindow(window(items, 2, 2), 4, false, true), 0);
		checkPileLand(self, startPileWindow(window(items, 2, 2), -4, false, true), 2);
	});

	it("an empty read for a pile reads from the far end once then gets out", _ {
		const std::array<NavigationItem, 0> items{};
		expect(std::holds_alternative<FromEnd>(startPileWindow(window(items), 3, false, true))).to_be_true();
		expect(std::holds_alternative<GetOut>(startPileWindow(window(items), 3, false, false))).to_be_true();
	});
});

DELUGE_SPEC(navigation_step_spec, navigation_step_group)
