#include "gui/ui/load/confirm_step.h"

#include <array>
#include <string_view>
#include <variant>

#include "cppspec.hpp"
#include "support/spec_main.h"

using namespace deluge::gui;

namespace {
template <size_t N>
ConfirmWindow window(const std::array<std::string_view, N>& names, bool popped, int32_t shown, int32_t atStart = 0,
                     int32_t atEnd = 0) {
	return {.readNames = names,
	        .poppedLast = popped,
	        .shownCount = shown,
	        .deletedAtStart = atStart,
	        .deletedAtEnd = atEnd};
}
constexpr std::array<std::string_view, 0> kNone{};
constexpr std::array<std::string_view, 3> kThree{"NAV03.XML", "NAV04.XML", "NAV05.XML"};
} // namespace

// clang-format off
describe confirm_step_group("confirm step", ${
	// ── An empty read ─────────────────────────────────────────────────────────────
	it("empty read while grabbing goes anywhere", _ {
		expect(std::holds_alternative<ConfirmAnyPreset>(decideConfirm(window(kNone, false, 0, 3), true))).to_be_true();
	});

	it("empty read trimmed at start reads from the start", _ {
		expect(std::holds_alternative<ConfirmFromStart>(decideConfirm(window(kNone, false, 0, 3), false))).to_be_true();
	});

	it("empty read otherwise goes anywhere", _ {
		expect(std::holds_alternative<ConfirmAnyPreset>(decideConfirm(window(kNone, false, 0), false))).to_be_true();
	});

	// ── Nothing available after filtering ────────────────────────────────────────
	it("nothing available pages on from before the popped entry", _ {
		// Pages on from NAV04, the entry before the popped one: NAV05 was popped off the window unoffered, and the next
		// read excludes it.
		const auto d = decideConfirm(window(kThree, true, 0, 0, 5), false);
		expect(std::holds_alternative<ConfirmPageOn>(d)).to_be_true();
		expect(std::get<ConfirmPageOn>(d).from).to_equal(std::string{"NAV04.XML"});
	});

	it("nothing available nothing popped pages from the last", _ {
		const auto d = decideConfirm(window(kThree, false, 0, 0, 5), false);
		expect(std::holds_alternative<ConfirmPageOn>(d)).to_be_true();
		expect(std::get<ConfirmPageOn>(d).from).to_equal(std::string{"NAV05.XML"});
	});

	it("nothing available trimmed at end pages even while grabbing", _ {
		expect(std::holds_alternative<ConfirmPageOn>(decideConfirm(window(kThree, true, 0, 0, 5), true))).to_be_true();
	});

	it("nothing available while grabbing goes anywhere", _ {
		expect(std::holds_alternative<ConfirmAnyPreset>(decideConfirm(window(kThree, false, 0, 2), true))).to_be_true();
	});

	it("nothing available trimmed at start reads from the start", _ {
		expect(std::holds_alternative<ConfirmFromStart>(decideConfirm(window(kThree, false, 0, 2), false))).to_be_true();
	});

	it("nothing available otherwise goes anywhere", _ {
		expect(std::holds_alternative<ConfirmAnyPreset>(decideConfirm(window(kThree, false, 0), false))).to_be_true();
	});

	// ── Something available ──────────────────────────────────────────────────────
	it("lands on the first shown", _ {
		expect(std::holds_alternative<ConfirmLand>(decideConfirm(window(kThree, true, 2, 0, 5), false))).to_be_true();
	});
});

DELUGE_SPEC(confirm_step_spec, confirm_step_group)
