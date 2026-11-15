// clang-format off
#include "controls/knob_math.h"
#include "instruments/deluge/cables.h"
#include "support/printers/controls_printer.h"
#include <cppspec.hpp>

using namespace deluge;
using namespace deluge::controls;

namespace {
const project::ParamAddress kEngine{.space = project::ParamSpace::ENGINE, .id = plugin::ParamId{7}};
const project::ParamAddress kCable{.space = project::ParamSpace::ENGINE, .id = plugin::ParamId{instrument::kCableDepthFlag | 7}};
const project::ParamAddress kCc{.space = project::ParamSpace::MIDI_CC, .id = plugin::ParamId{21}};
} // namespace

describe knob_math("the gold knob's arithmetic", ${
	it("tells a cable's knob, a MIDI CC's and a param's apart", _{
		expect(knob_kind(kCable)).to_equal(KnobKind::CABLE);
		expect(knob_kind(kCc)).to_equal(KnobKind::MIDI_CC);
		expect(knob_kind(kEngine)).to_equal(KnobKind::PARAM);
	});
	it("reads a cable's depth as a position in steps of 2^23, and writes it back", _{
		expect(knob_pos_of(kCable, nullptr, 0)).to_equal(-64);
		expect(value_of_knob(kCable, nullptr, 0)).to_equal(64 << 23);
		expect(value_of_knob(kCable, nullptr, 64)).to_equal(1 << 30);
	});
	it("turns a cable's knob down to -192, a MIDI CC's up to 63, a param's within -64..64", _{
		expect(turn(KnobKind::CABLE, -190, -10)).to_equal(-192);
		expect(turn(KnobKind::MIDI_CC, 60, 10)).to_equal(63);
		expect(turn(KnobKind::PARAM, 60, 10)).to_equal(64);
		expect(turn(KnobKind::PARAM, -60, -10)).to_equal(-64);
	});
	it("folds a cable's position onto its indicator, reaching the centre only at 0", _{
		expect(fold_cable(0)).to_equal(64);
		expect(fold_cable(1)).to_equal(65);
		expect(fold_cable(-1)).to_equal(63);
	});
});
// clang-format on

CPPSPEC_SPEC(knob_math)
