// clang-format off
#include "board_controls.hpp"
#include "controls/board.h"
#include <cppspec.hpp>

using namespace deluge::controls;

namespace {
constexpr Board kSmall{.sections = 3, .slots = 1};

bool is(const std::optional<Logical>& got, Logical::Kind kind, uint8_t index) {
	return got.has_value() && got->kind == kind && got->index == index;
}
} // namespace

describe board("a board's map from physical controls to logical ones", ${
	it("takes the Deluge's mod buttons as its sections, one to one", _{
		for (uint8_t n = 0; n < kDelugeControls.sections; n++) {
			expect(is(map_input(kDelugeControls, {Physical::Kind::SECTION_BUTTON, n}), Logical::Kind::SECTION, n)).to_be_true();
		}
	});
	it("takes the Deluge's gold knobs as its slots and their pushes as slot presses, one to one", _{
		for (uint8_t k = 0; k < kDelugeControls.slots; k++) {
			expect(is(map_input(kDelugeControls, {Physical::Kind::SLOT_ENCODER, k}), Logical::Kind::SLOT, k)).to_be_true();
			expect(is(map_input(kDelugeControls, {Physical::Kind::SLOT_PRESS, k}), Logical::Kind::SLOT_PRESS, k)).to_be_true();
		}
	});
	it("drops a control past the Deluge's buttons and knobs", _{
		expect(map_input(kDelugeControls, {Physical::Kind::SECTION_BUTTON, 8}).has_value()).to_be_false();
		expect(map_input(kDelugeControls, {Physical::Kind::SLOT_ENCODER, 2}).has_value()).to_be_false();
		expect(map_input(kDelugeControls, {Physical::Kind::SLOT_PRESS, 2}).has_value()).to_be_false();
	});
	it("maps a smaller board by its own counts", _{
		expect(is(map_input(kSmall, {Physical::Kind::SECTION_BUTTON, 2}), Logical::Kind::SECTION, 2)).to_be_true();
		expect(map_input(kSmall, {Physical::Kind::SECTION_BUTTON, 3}).has_value()).to_be_false();
		expect(is(map_input(kSmall, {Physical::Kind::SLOT_ENCODER, 0}), Logical::Kind::SLOT, 0)).to_be_true();
		expect(map_input(kSmall, {Physical::Kind::SLOT_ENCODER, 1}).has_value()).to_be_false();
	});
	it("drops a press on a board whose knobs do not press", _{
		expect(map_input(kSmall, {Physical::Kind::SLOT_PRESS, 0}).has_value()).to_be_false();
	});
});
// clang-format on

CPPSPEC_SPEC(board)
