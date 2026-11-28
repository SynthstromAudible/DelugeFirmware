/// @file
/// @brief The note editor and note-row editor menu trees
///        (`ui/scenes/menu/nodes/note.h`), tested against `FakeNoteBridgeGuard`
///        (`support/fake_note_bridge.h`), and their shape against main's note editors.
///
/// Covered:
///
///   * every leaf's `bindings::read` calls its bridge entry;
///   * every `adjust` passes its offset through to the bridge unchanged;
///   * every value `set` (the two divisors, the sixteen step toggles) computes legacy's own mask;
///   * each guard (`permitted_note`/`permitted_row`) refuses and raises its literal popup;
///   * each step toggle's `relevant` follows the divisor, including the "OFF, index 0" case;
///   * `note_iterance`/`note_row_iterance`'s `select_chain` resolves only on
///     `deluge::project::kCustomIterancePreset`;
///   * the three `value_text` hooks produce legacy's strings, including
///     probability's latching and fill's strike;
///   * the shape: each root and custom root has the child count main's note editors have (4, 4, 9, 9);
///   * `validate_tree` over `kAllRoots` reports nothing about any node this file declares.
///
#include "gui/ui/menu_view_of.h"
#include "project/note_probability.h"
#include "ui/scenes/menu/bindings.h"
#include "ui/scenes/menu/context.h"
#include "ui/scenes/menu/menu_scene.h"
#include "ui/scenes/menu/node.h"
#include "ui/scenes/menu/nodes/nodes.h"
#include "ui/scenes/menu/nodes/note.h"
#include "ui/scenes/menu/validate.h"

#include "base/notify/notice_queue.h"

#include "definitions_cxx.hpp"
#include "project/iterance.h"

#include "support/fake_note_bridge.h"
#include "support/popup_capture.h"
#include "support/song_fixture.h"

#include "gui/l10n/l10n.h"
#include "gui/l10n/strings.h"

#include "cppspec.hpp"
#include "support/matchers/address_of.h"
#include "support/matchers/value_matchers.h"

#include <algorithm>
#include <iterator>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "support/menu_context.h"
#include "support/spec_main.h"

using deluge::project::Iterance;

namespace menu = deluge::ui::scenes::menu;
namespace nodes = deluge::ui::scenes::menu::nodes;
namespace bindings = deluge::ui::scenes::menu::bindings;
namespace spec = deluge::ui::scenes::spec;
using deluge::ui::scenes::menu::Node;
using deluge::ui::scenes::menu::ValueText;
using deluge::ui::scenes::menu::ValueTextUse;
using deluge::ui::scenes::spec::FakeNoteBridge;
using deluge::ui::scenes::spec::FakeNoteBridgeGuard;
using String = deluge::l10n::String;
using deluge::test::matchers::address_of;

namespace {

void select_language() {
	deluge::l10n::chosenLanguage = &deluge::l10n::built_in::english;
}

std::string value_text_string(const menu::ValueTextFn fn, const menu::MenuContext& ctx, int32_t value,
                              ValueTextUse use) {
	char buffer[32] = {};
	const ValueText t = fn(ctx, value, use, std::span<char>(buffer, sizeof(buffer)));
	return std::string(buffer, t.length);
}

} // namespace

// clang-format off
describe note_nodes_bridge("scenes::menu note editor/note-row editor: bridge wiring", $ {
	it("reads and adjusts note_velocity through the bridge, offset unchanged", _ {
		FakeNoteBridgeGuard guard;
		menu::MenuContext ctx{};
		guard.fake.note_velocity_value = 42;
		expect(bindings::read(nodes::note_velocity, ctx)).to_equal(int32_t(42));
		expect(guard.fake.note_velocity_calls).to_equal(int32_t(1));

		bindings::adjust(nodes::note_velocity, ctx, 7);
		expect(guard.fake.adjust_note_velocity_calls).to_equal(int32_t(1));
		expect(guard.fake.adjust_note_velocity_last_offset).to_equal(int32_t(7));

		bindings::adjust(nodes::note_velocity, ctx, -3);
		expect(guard.fake.adjust_note_velocity_last_offset).to_equal(int32_t(-3));
	});

	it("reads and adjusts note_editor_probability through the bridge, offset unchanged", _ {
		FakeNoteBridgeGuard guard;
		menu::MenuContext ctx{};
		guard.fake.note_probability_value = 15;
		expect(bindings::read(nodes::note_editor_probability, ctx)).to_equal(int32_t(15));
		expect(guard.fake.note_probability_calls).to_equal(int32_t(1));

		bindings::adjust(nodes::note_editor_probability, ctx, 2);
		expect(guard.fake.adjust_note_probability_last_offset).to_equal(int32_t(2));
	});

	it("reads note_iterance as a preset index, converting the bridge's raw int itself", _ {
		FakeNoteBridgeGuard guard;
		menu::MenuContext ctx{};

		guard.fake.note_iterance_value = 0; // OFF
		expect(bindings::read(nodes::note_iterance, ctx)).to_equal(int32_t(0));

		guard.fake.note_iterance_value = Iterance{2, 0b1}.packed(); // preset 3, "1of2" (after FIRST and LAST)
		expect(bindings::read(nodes::note_iterance, ctx)).to_equal(int32_t(3));

		guard.fake.note_iterance_value = Iterance{1, 1}.packed(); // "1of1" is not itself a preset row
		expect(bindings::read(nodes::note_iterance, ctx)).to_equal(int32_t(deluge::project::kCustomIterancePreset));

		bindings::adjust(nodes::note_iterance, ctx, 4);
		expect(guard.fake.adjust_note_iterance_calls).to_equal(int32_t(1));
		expect(guard.fake.adjust_note_iterance_last_offset).to_equal(int32_t(4));
	});

	it("reads and adjusts note_fill through the bridge, offset unchanged", _ {
		FakeNoteBridgeGuard guard;
		menu::MenuContext ctx{};
		guard.fake.note_fill_value = std::to_underlying(deluge::project::FillMode::NOT_FILL);
		expect(bindings::read(nodes::note_fill, ctx)).to_equal(int32_t(std::to_underlying(deluge::project::FillMode::NOT_FILL)));
		bindings::adjust(nodes::note_fill, ctx, 1);
		expect(guard.fake.adjust_note_fill_last_offset).to_equal(int32_t(1));
	});

	it("reads and adjusts the note-row leaves through the row half of the bridge", _ {
		FakeNoteBridgeGuard guard;
		menu::MenuContext ctx{};

		guard.fake.row_probability_value = 11;
		expect(bindings::read(nodes::note_row_probability, ctx)).to_equal(int32_t(11));
		bindings::adjust(nodes::note_row_probability, ctx, 6);
		expect(guard.fake.adjust_row_probability_last_offset).to_equal(int32_t(6));

		guard.fake.row_iterance_value = Iterance{3, 0b100}.packed(); // preset 7, "3of3"
		expect(bindings::read(nodes::note_row_iterance, ctx)).to_equal(int32_t(7));
		bindings::adjust(nodes::note_row_iterance, ctx, -2);
		expect(guard.fake.adjust_row_iterance_last_offset).to_equal(int32_t(-2));

		guard.fake.row_fill_value = std::to_underlying(deluge::project::FillMode::FILL);
		expect(bindings::read(nodes::note_row_fill, ctx)).to_equal(int32_t(std::to_underlying(deluge::project::FillMode::FILL)));
		bindings::adjust(nodes::note_row_fill, ctx, 1);
		expect(guard.fake.adjust_row_fill_last_offset).to_equal(int32_t(1));
	});
});

describe note_nodes_guards("scenes::menu note editor/note-row editor: the two guards", $ {
	it("permitted_note refuses and raises \"Select Note\" with nothing selected", _ {
		select_language();
		FakeNoteBridgeGuard guard;
		menu::MenuContext ctx{};
		deluge::ui::scenes::spec::PopupCapture popups;

		guard.fake.note_selected_value = false;
		expect(nodes::note_velocity.bind.permitted(ctx)).to_be_false();
		expect(popups.seen()).to_equal(std::vector<std::string>{"popup: Select Note"});
		popups.clear();

		guard.fake.note_selected_value = true;
		expect(nodes::note_velocity.bind.permitted(ctx)).to_be_true();
		expect(popups.seen()).to_equal(std::vector<std::string>{}); // no popup on success
	});

	it("permitted_row refuses and raises \"Select Row\" while not auditioning", _ {
		select_language();
		FakeNoteBridgeGuard guard;
		menu::MenuContext ctx{};
		deluge::ui::scenes::spec::PopupCapture popups;

		guard.fake.row_auditioning_value = false;
		expect(nodes::note_row_probability.bind.permitted(ctx)).to_be_false();
		expect(popups.seen()).to_equal(std::vector<std::string>{"popup: Select Row"});
		popups.clear();

		guard.fake.row_auditioning_value = true;
		expect(nodes::note_row_probability.bind.permitted(ctx)).to_be_true();
		expect(popups.seen()).to_equal(std::vector<std::string>{});
	});

	it("carries exactly the right guard on every note-editor and note-row-editor leaf, and none on "
	   "the step toggles", _ {
		// The exact function, not merely non-null: a leaf gated on the wrong family's guard would
		// still pass a `!= nullptr` check.
		expect(address_of(nodes::note_velocity.bind.permitted)).to_equal(address_of(&nodes::permitted_note));
		expect(address_of(nodes::note_editor_probability.bind.permitted)).to_equal(address_of(&nodes::permitted_note));
		expect(address_of(nodes::note_iterance.bind.permitted)).to_equal(address_of(&nodes::permitted_note));
		expect(address_of(nodes::note_fill.bind.permitted)).to_equal(address_of(&nodes::permitted_note));
		expect(address_of(nodes::note_iterance_divisor.bind.permitted)).to_equal(address_of(&nodes::permitted_note));
		expect(address_of(nodes::note_row_probability.bind.permitted)).to_equal(address_of(&nodes::permitted_row));
		expect(address_of(nodes::note_row_iterance.bind.permitted)).to_equal(address_of(&nodes::permitted_row));
		expect(address_of(nodes::note_row_fill.bind.permitted)).to_equal(address_of(&nodes::permitted_row));
		expect(address_of(nodes::note_row_iterance_divisor.bind.permitted)).to_equal(address_of(&nodes::permitted_row));

		expect(nodes::note_iterance_step_1.bind.permitted).to_be_null();
		expect(nodes::note_iterance_step_8.bind.permitted).to_be_null();
		expect(nodes::note_row_iterance_step_1.bind.permitted).to_be_null();
		expect(nodes::note_row_iterance_step_8.bind.permitted).to_be_null();
	});
});

describe note_nodes_value_text("scenes::menu note editor/note-row editor: value_text", $ {
	it("spells probability with the latching mask, column vs notification", _ {
		FakeNoteBridgeGuard guard;
		menu::MenuContext ctx{};
		const auto fn = nodes::note_editor_probability.bind.value_text;

		// 15 <= deluge::project::kProbabilityValueCount (20): not latching, shown = 15 * 5 = 75.
		expect(value_text_string(fn, ctx, 15, ValueTextUse::Column)).to_equal(std::string{"75%"});
		expect(value_text_string(fn, ctx, 15, ValueTextUse::Notification)).to_equal(std::string{"75%"});

		// 25 > deluge::project::kProbabilityValueCount (20): latching, masked to 25 & 127 = 25, shown = 125.
		expect(value_text_string(fn, ctx, 25, ValueTextUse::Column)).to_equal(std::string{"125L"});
		expect(value_text_string(fn, ctx, 25, ValueTextUse::Notification)).to_equal(std::string{"125% ltch"});
	});

	it("spells iterance's OFF and CUSTOM literals, and the bridge's own label otherwise", _ {
		FakeNoteBridgeGuard guard;
		menu::MenuContext ctx{};
		const auto fn = nodes::note_iterance.bind.value_text;

		expect(value_text_string(fn, ctx, 0, ValueTextUse::Column)).to_equal(std::string{"OFF"});
		expect(value_text_string(fn, ctx, deluge::project::kCustomIterancePreset, ValueTextUse::Notification))
		    .to_equal(std::string{"CUSTOM"});

		guard.fake.iterance_preset_label_text = "2:3";
		expect(value_text_string(fn, ctx, 5, ValueTextUse::Column)).to_equal(std::string{"2:3"});
		expect(guard.fake.iterance_preset_label_last_preset).to_equal(int32_t(5));
		expect(guard.fake.iterance_preset_label_last_compact).to_be_true();

		guard.fake.iterance_preset_label_text = "2 of 3";
		expect(value_text_string(fn, ctx, 5, ValueTextUse::Notification)).to_equal(std::string{"2 of 3"});
		expect(guard.fake.iterance_preset_label_last_compact).to_be_false();
	});

	it("spells fill's column pair and notification triple, striking NOT_FILL only in the column", _ {
		FakeNoteBridgeGuard guard;
		menu::MenuContext ctx{};
		const auto fn = nodes::note_fill.bind.value_text;
		char buffer[32];

		ValueText off_col = fn(ctx, std::to_underlying(deluge::project::FillMode::OFF), ValueTextUse::Column, std::span<char>(buffer, sizeof(buffer)));
		expect(std::string(buffer, off_col.length)).to_equal(std::string{"OFF"});
		expect(off_col.strikethrough).to_be_false();

		ValueText notfill_col =
		    fn(ctx, std::to_underlying(deluge::project::FillMode::NOT_FILL), ValueTextUse::Column, std::span<char>(buffer, sizeof(buffer)));
		expect(std::string(buffer, notfill_col.length)).to_equal(std::string{"FILL"});
		expect(notfill_col.strikethrough).to_be_true();

		ValueText fill_col = fn(ctx, std::to_underlying(deluge::project::FillMode::FILL), ValueTextUse::Column, std::span<char>(buffer, sizeof(buffer)));
		expect(std::string(buffer, fill_col.length)).to_equal(std::string{"FILL"});
		expect(fill_col.strikethrough).to_be_false();

		expect(value_text_string(fn, ctx, std::to_underlying(deluge::project::FillMode::OFF), ValueTextUse::Notification)).to_equal(std::string{"OFF"});
		expect(value_text_string(fn, ctx, std::to_underlying(deluge::project::FillMode::NOT_FILL), ValueTextUse::Notification))
		    .to_equal(std::string{"NOT FILL"});
		expect(value_text_string(fn, ctx, std::to_underlying(deluge::project::FillMode::FILL), ValueTextUse::Notification)).to_equal(std::string{"FILL"});
	});

	it("gives note_row's probability/iterance/fill the identical value_text hooks", _ {
		expect(address_of(nodes::note_row_probability.bind.value_text))
		    .to_equal(address_of(nodes::note_editor_probability.bind.value_text));
		expect(address_of(nodes::note_row_iterance.bind.value_text))
		    .to_equal(address_of(nodes::note_iterance.bind.value_text));
		expect(address_of(nodes::note_row_fill.bind.value_text)).to_equal(address_of(nodes::note_fill.bind.value_text));
	});
});

describe note_nodes_custom_iterance("scenes::menu note editor/note-row editor: custom iterance", $ {
	it("reads the divisor clamped 1..8, converting OFF to 1of1 first", _ {
		FakeNoteBridgeGuard guard;
		menu::MenuContext ctx{};

		guard.fake.note_iterance_value = Iterance{5, 0b10101}.packed();
		expect(bindings::read(nodes::note_iterance_divisor, ctx)).to_equal(int32_t(5));

		guard.fake.note_iterance_value = 0; // OFF -> 1of1 -> divisor 1
		expect(bindings::read(nodes::note_iterance_divisor, ctx)).to_equal(int32_t(1));
	});

	it("writes the divisor with legacy's own mask, converting OFF to 1of1 first", _ {
		FakeNoteBridgeGuard guard;
		menu::MenuContext ctx{};

		// divisor 5, steps 0b10101 (bits 0, 2, 4). Narrowing to divisor 3 masks to bits 0..2: 0b101.
		guard.fake.note_iterance_value = Iterance{5, 0b10101}.packed();
		bindings::write(nodes::note_iterance_divisor, ctx, 3);
		expect(guard.fake.set_note_iterance_calls).to_equal(int32_t(1));
		expect(guard.fake.set_note_iterance_last_value).to_equal(int32_t(Iterance{3, 0b101}.packed()));

		// OFF -> 1of1 (divisor 1, steps 0b1) before the mask is computed.
		guard.fake.note_iterance_value = 0;
		bindings::write(nodes::note_iterance_divisor, ctx, 1);
		expect(guard.fake.set_note_iterance_last_value).to_equal(int32_t(Iterance{1, 0b1}.packed()));
	});

	it("reads each step's bit, converting OFF to 1of1 first", _ {
		FakeNoteBridgeGuard guard;
		menu::MenuContext ctx{};

		guard.fake.note_iterance_value = Iterance{5, 0b10101}.packed(); // bits 0, 2, 4 set
		expect(bindings::read(nodes::note_iterance_step_1, ctx)).to_equal(int32_t(1)); // index 0
		expect(bindings::read(nodes::note_iterance_step_2, ctx)).to_equal(int32_t(0)); // index 1
		expect(bindings::read(nodes::note_iterance_step_3, ctx)).to_equal(int32_t(1)); // index 2

		guard.fake.note_iterance_value = 0; // OFF -> 1of1: divisor 1, step bit 0 only
		expect(bindings::read(nodes::note_iterance_step_1, ctx)).to_equal(int32_t(1));
	});

	it("writes each step's bit with an OR or an AND-NOT, keeping the current divisor", _ {
		FakeNoteBridgeGuard guard;
		menu::MenuContext ctx{};

		// divisor 5, steps 0b10101; index 1 (bit 2) is currently clear -- set it.
		guard.fake.note_iterance_value = Iterance{5, 0b10101}.packed();
		bindings::write(nodes::note_iterance_step_2, ctx, 1);
		expect(guard.fake.set_note_iterance_last_value).to_equal(int32_t(Iterance{5, 0b10111}.packed()));

		// Same starting value; index 0 (bit 1) is currently set -- clear it.
		guard.fake.note_iterance_value = Iterance{5, 0b10101}.packed();
		bindings::write(nodes::note_iterance_step_1, ctx, 0);
		expect(guard.fake.set_note_iterance_last_value).to_equal(int32_t(Iterance{5, 0b10100}.packed()));
	});

	it("shows a step relevant only under the divisor, or index 0 while iterance is off", _ {
		FakeNoteBridgeGuard guard;
		menu::MenuContext ctx{};

		guard.fake.note_iterance_value = Iterance{3, 0b101}.packed(); // divisor 3
		expect(nodes::note_iterance_step_1.relevant(ctx)).to_be_true();  // index 0: 3 > 0
		expect(nodes::note_iterance_step_2.relevant(ctx)).to_be_true();  // index 1: 3 > 1
		expect(nodes::note_iterance_step_3.relevant(ctx)).to_be_true();  // index 2: 3 > 2
		expect(nodes::note_iterance_step_4.relevant(ctx)).to_be_false(); // index 3: 3 > 3 is false

		guard.fake.note_iterance_value = 0; // OFF, unconverted for this test
		expect(nodes::note_iterance_step_1.relevant(ctx)).to_be_true();  // index 0, OFF
		expect(nodes::note_iterance_step_2.relevant(ctx)).to_be_false(); // index 1, OFF
	});

	it("mirrors every one of the above through the row half of the bridge", _ {
		FakeNoteBridgeGuard guard;
		guard.fake.row_auditioning_value = true; // "a row is selected" -- see the OFF-only example below
		menu::MenuContext ctx{};

		guard.fake.row_iterance_value = Iterance{4, 0b1010}.packed();
		expect(bindings::read(nodes::note_row_iterance_divisor, ctx)).to_equal(int32_t(4));
		expect(bindings::read(nodes::note_row_iterance_step_2, ctx)).to_equal(int32_t(1)); // bit 1 set
		expect(bindings::read(nodes::note_row_iterance_step_1, ctx)).to_equal(int32_t(0)); // bit 0 clear

		bindings::write(nodes::note_row_iterance_divisor, ctx, 2);
		expect(guard.fake.set_row_iterance_last_value).to_equal(int32_t(Iterance{2, 0b10}.packed()));

		bindings::write(nodes::note_row_iterance_step_1, ctx, 1);
		expect(guard.fake.set_row_iterance_last_value).to_equal(int32_t(Iterance{4, 0b1011}.packed()));

		// Beyond the OFF case: an ordinary divisor, same as the note family's own check above.
		guard.fake.row_iterance_value = Iterance{3, 0b101}.packed(); // divisor 3
		expect(nodes::note_row_iterance_step_1.relevant(ctx)).to_be_true();  // index 0: 3 > 0
		expect(nodes::note_row_iterance_step_3.relevant(ctx)).to_be_true();  // index 2: 3 > 2
		expect(nodes::note_row_iterance_step_4.relevant(ctx)).to_be_false(); // index 3: 3 > 3 is false

		guard.fake.row_iterance_value = 0;
		expect(nodes::note_row_iterance_step_1.relevant(ctx)).to_be_true();
		expect(nodes::note_row_iterance_step_2.relevant(ctx)).to_be_false();
	});

	it("returns false for every step with a bridge installed but nothing selected, both families", _ {
		// Main's own no-note (`note::IteranceStepToggle::isRelevant` in
		// `origin/main:src/deluge/gui/menu_item/note/iterance_step_toggle.h`) / no-row
		// (`note_row::IteranceStepToggle::isRelevant` in
		// `origin/main:src/deluge/gui/menu_item/note_row/iterance_step_toggle.h`) arm: a bridge that answers "nothing
		// selected" must report every step not relevant, not just index 0's "OFF, divisor 0" case (a raw iterance of 0
		// is indistinguishable, on its own, from "no note"/"no row", so index 0 would read as relevant).
		FakeNoteBridgeGuard guard;
		guard.fake.note_selected_value = false;
		guard.fake.row_auditioning_value = false;
		menu::MenuContext ctx{};

		expect(nodes::note_iterance_step_1.relevant(ctx)).to_be_false();
		expect(nodes::note_iterance_step_2.relevant(ctx)).to_be_false();
		expect(nodes::note_row_iterance_step_1.relevant(ctx)).to_be_false();
		expect(nodes::note_row_iterance_step_2.relevant(ctx)).to_be_false();
	});
});

describe note_nodes_no_bridge("scenes::menu note editor/note-row editor: no bridge installed at all", $ {
	it("answers false/nullptr rather than crashing, for relevant and select_chain", _ {
		// The whole-tree sweeps this guards against: `tables_2_spec.cpp`'s/`tables_3_spec.cpp`'s
		// own `EmptySubmenu`/`DuplicateName` checks and its "never lets a select_chain hook name a
		// node outside its own chain_targets" sweep (`tables_3_spec.cpp`) call these hooks
		// over the whole tree, including this one, with no bridge installed at all -- boot's real one
		// taken down for the scope, and no fake put up.
		const deluge::ui::scenes::spec::NoNoteBridge no_bridge;
		expect(menu::note_bridge()).to_be_null();
		menu::MenuContext ctx{};
		menu::MenuState state{};

		expect(nodes::note_iterance_step_1.relevant(ctx)).to_be_false();
		expect(nodes::note_row_iterance_step_1.relevant(ctx)).to_be_false();
		expect(nodes::note_iterance.select_chain(ctx, state)).to_be_null();
		expect(nodes::note_row_iterance.select_chain(ctx, state)).to_be_null();
	});
	it("reads zero and refuses every edit on the note leaves", _ {
		// A harness that opens a note root without `wireShellNoteBridge()`: every read through the bridge answers as if
		// nothing were selected, both guards refuse, and the iterance label falls back.
		select_language();
		const deluge::ui::scenes::spec::NoNoteBridge no_bridge;
		menu::MenuContext ctx{};
		deluge::ui::scenes::spec::PopupCapture popups;

		expect(nodes::get_note_velocity(ctx)).to_equal(int32_t(0));
		expect(nodes::get_note_probability(ctx)).to_equal(int32_t(0));
		expect(nodes::get_note_iterance(ctx)).to_equal(int32_t(0));
		expect(nodes::get_note_fill(ctx)).to_equal(int32_t(0));
		expect(nodes::get_note_row_probability(ctx)).to_equal(int32_t(0));
		expect(nodes::get_note_row_iterance(ctx)).to_equal(int32_t(0));
		expect(nodes::get_note_row_fill(ctx)).to_equal(int32_t(0));

		expect(nodes::note_velocity.bind.permitted(ctx)).to_be_false();
		expect(popups.seen()).to_equal(std::vector<std::string>{"popup: Select Note"});
		popups.clear();
		expect(nodes::note_row_probability.bind.permitted(ctx)).to_be_false();
		expect(popups.seen()).to_equal(std::vector<std::string>{"popup: Select Row"});
		expect(value_text_string(nodes::note_iterance.bind.value_text, ctx, 5, ValueTextUse::Column))
		    .to_equal(std::string{});
	});
	// Each write below has its own guard on the bridge: it goes nowhere, and the value it would change reads the same.
	it("leaves the note velocity as it was on a turn", _ {
		const deluge::ui::scenes::spec::NoNoteBridge no_bridge;
		menu::MenuContext ctx{};
		nodes::adjust_note_velocity(ctx, 1);
		expect(nodes::get_note_velocity(ctx)).to_equal(int32_t(0));
	});
	it("leaves the note probability as it was on a turn", _ {
		const deluge::ui::scenes::spec::NoNoteBridge no_bridge;
		menu::MenuContext ctx{};
		nodes::adjust_note_probability(ctx, 1);
		expect(nodes::get_note_probability(ctx)).to_equal(int32_t(0));
	});
	it("leaves the note iterance, its divisor and its steps as they were on a turn or a set", _ {
		const deluge::ui::scenes::spec::NoNoteBridge no_bridge;
		menu::MenuContext ctx{};
		const int32_t divisor = nodes::get_note_iterance_divisor(ctx);
		const int32_t step = nodes::get_note_iterance_step_1(ctx);
		nodes::adjust_note_iterance(ctx, 1);
		nodes::set_note_iterance_divisor(ctx, 2);
		nodes::set_note_iterance_step_1(ctx, 1 - step);
		expect(nodes::get_note_iterance(ctx)).to_equal(int32_t(0));
		expect(nodes::get_note_iterance_divisor(ctx)).to_equal(divisor);
		expect(nodes::get_note_iterance_step_1(ctx)).to_equal(step);
	});
	it("leaves the note fill as it was on a turn", _ {
		const deluge::ui::scenes::spec::NoNoteBridge no_bridge;
		menu::MenuContext ctx{};
		nodes::adjust_note_fill(ctx, 1);
		expect(nodes::get_note_fill(ctx)).to_equal(int32_t(0));
	});
	it("leaves the row probability as it was on a turn", _ {
		const deluge::ui::scenes::spec::NoNoteBridge no_bridge;
		menu::MenuContext ctx{};
		nodes::adjust_note_row_probability(ctx, 1);
		expect(nodes::get_note_row_probability(ctx)).to_equal(int32_t(0));
	});
	it("leaves the row iterance, its divisor and its steps as they were on a turn or a set", _ {
		const deluge::ui::scenes::spec::NoNoteBridge no_bridge;
		menu::MenuContext ctx{};
		const int32_t divisor = nodes::get_note_row_iterance_divisor(ctx);
		const int32_t step = nodes::get_note_row_iterance_step_1(ctx);
		nodes::adjust_note_row_iterance(ctx, 1);
		nodes::set_note_row_iterance_divisor(ctx, 2);
		nodes::set_note_row_iterance_step_1(ctx, 1 - step);
		expect(nodes::get_note_row_iterance(ctx)).to_equal(int32_t(0));
		expect(nodes::get_note_row_iterance_divisor(ctx)).to_equal(divisor);
		expect(nodes::get_note_row_iterance_step_1(ctx)).to_equal(step);
	});
	it("leaves the row fill as it was on a turn", _ {
		const deluge::ui::scenes::spec::NoNoteBridge no_bridge;
		menu::MenuContext ctx{};
		nodes::adjust_note_row_fill(ctx, 1);
		expect(nodes::get_note_row_fill(ctx)).to_equal(int32_t(0));
	});
});

describe note_nodes_select_chain("scenes::menu note editor/note-row editor: the iterance chain", $ {
	it("chains to the custom-iterance root only on deluge::project::kCustomIterancePreset", _ {
		FakeNoteBridgeGuard guard;
		menu::MenuContext ctx{};
		menu::MenuState state{};

		guard.fake.note_iterance_value = Iterance{2, 0b1}.packed(); // preset 1, not CUSTOM
		expect(nodes::note_iterance.select_chain(ctx, state)).to_be_null();

		guard.fake.note_iterance_value = Iterance{1, 1}.packed(); // maps to CUSTOM (36)
		expect(nodes::note_iterance.select_chain(ctx, state)).to_equal(&nodes::note_custom_iterance_root);

		guard.fake.row_iterance_value = Iterance{2, 0b1}.packed();
		expect(nodes::note_row_iterance.select_chain(ctx, state)).to_be_null();

		guard.fake.row_iterance_value = Iterance{1, 1}.packed();
		expect(nodes::note_row_iterance.select_chain(ctx, state)).to_equal(&nodes::note_row_custom_iterance_root);
	});

	it("lists exactly its one chain target, matched with its own hook", _ {
		expect(nodes::note_iterance.chain_targets.size()).to_equal(size_t(1));
		expect(nodes::note_iterance.chain_targets[0]).to_equal(&nodes::note_custom_iterance_root);
		expect(nodes::note_row_iterance.chain_targets.size()).to_equal(size_t(1));
		expect(nodes::note_row_iterance.chain_targets[0]).to_equal(&nodes::note_row_custom_iterance_root);
	});
});

describe note_nodes_shape("scenes::menu note editor/note-row editor: shape, as main's note editors have it", $ {
	it("gives note_editor_root main's four children, in the horizontal menu itself", _ {
		expect(nodes::note_editor_root.children.size()).to_equal(size_t(4));
		expect(nodes::note_editor_root.layout).to_equal(menu::Layout::Horizontal);
	});

	it("gives note_row_editor_root main's four children, sequence direction first", _ {
		expect(nodes::note_row_editor_root.children.size()).to_equal(size_t(4));
		expect(nodes::note_row_editor_root.children[0]).to_equal(&nodes::sequence_direction);
		expect(nodes::note_row_editor_root.layout).to_equal(menu::Layout::Horizontal);
	});

	it("gives note_custom_iterance_root main's nine children (divisor + 8 steps)", _ {
		expect(nodes::note_custom_iterance_root.children.size()).to_equal(size_t(9));
		expect(nodes::note_custom_iterance_root.layout).to_equal(menu::Layout::Vertical);
	});

	it("gives note_row_custom_iterance_root main's nine children (divisor + 8 steps)", _ {
		expect(nodes::note_row_custom_iterance_root.children.size()).to_equal(size_t(9));
		expect(nodes::note_row_custom_iterance_root.layout).to_equal(menu::Layout::Vertical);
	});

	it("lists both editor roots in kAllRootArray right after multi_range", _ {
		const auto at = std::ranges::find(nodes::kAllRoots, &nodes::multi_range);
		expect(nodes::kAllRoots).to_contain(&nodes::multi_range);
		to_be_at_least(expect(std::distance(at, nodes::kAllRoots.end())), 3);
		expect(*(at + 1)).to_equal(&nodes::note_editor_root);
		expect(*(at + 2)).to_equal(&nodes::note_row_editor_root);
	});

	it("does not add either custom-iterance root to kAllRootArray -- chain_targets only", _ {
		for (const Node* root : nodes::kAllRoots) {
			expect(root).not_().to_equal(&nodes::note_custom_iterance_root);
			expect(root).not_().to_equal(&nodes::note_row_custom_iterance_root);
		}
	});
});

describe note_nodes_validation("scenes::menu note editor/note-row editor: validate_tree", $ {
	it("reports nothing about any node this file declares", _ {
		select_language();
		// A real context, the way `tables_2_spec.cpp`'s own whole-tree `validate_tree` calls do
		// (`fixture().ctx`), rather than an empty span -- so the `EmptySubmenu`/`DuplicateName`
		// sweeps actually call `relevant()` on these nodes instead of skipping relevance filtering
		// entirely. A `FakeNoteBridgeGuard`, with both families' "something is selected" answer set
		// true, so the two custom-iterance roots' `EmptySubmenu` check finds a visible step (the
		// same reason `note_selected_value`/`row_auditioning_value` matter to `iterance_has_target`).
		FakeNoteBridgeGuard guard;
		guard.fake.note_selected_value = true;
		guard.fake.row_auditioning_value = true;
		spec::SongFixture song = spec::make_synth_song();
		const menu::MenuContext ctx =
		    spec::ctx_of(*song.song, deluge::gui::menuViewOf(song.clip), 0, false, false, false, false);
		const menu::MenuContext contexts[] = {ctx};
		const auto issues = menu::validate_tree(nodes::kAllRoots, contexts, nodes::kRangeSpecificNodes,
		                                        nodes::kHalfPrecisionNodes, menu::ValidateOptions{});
		const std::set<const Node*> ours = {
		    &nodes::note_editor_root,
		    &nodes::note_velocity,
		    &nodes::note_editor_probability,
		    &nodes::note_iterance,
		    &nodes::note_fill,
		    &nodes::note_custom_iterance_root,
		    &nodes::note_iterance_divisor,
		    &nodes::note_iterance_step_1,
		    &nodes::note_iterance_step_2,
		    &nodes::note_iterance_step_3,
		    &nodes::note_iterance_step_4,
		    &nodes::note_iterance_step_5,
		    &nodes::note_iterance_step_6,
		    &nodes::note_iterance_step_7,
		    &nodes::note_iterance_step_8,
		    &nodes::note_row_editor_root,
		    &nodes::note_row_probability,
		    &nodes::note_row_iterance,
		    &nodes::note_row_fill,
		    &nodes::note_row_custom_iterance_root,
		    &nodes::note_row_iterance_divisor,
		    &nodes::note_row_iterance_step_1,
		    &nodes::note_row_iterance_step_2,
		    &nodes::note_row_iterance_step_3,
		    &nodes::note_row_iterance_step_4,
		    &nodes::note_row_iterance_step_5,
		    &nodes::note_row_iterance_step_6,
		    &nodes::note_row_iterance_step_7,
		    &nodes::note_row_iterance_step_8,
		};
		expect(ours.size()).to_equal(size_t(29));

		size_t ours_issues = 0;
		for (const menu::Issue& i : issues) {
			if (ours.contains(i.node)) {
				ours_issues++;
			}
		}
		expect(ours_issues).to_equal(size_t(0));
	});
});
// clang-format on

DELUGE_SPEC(note_nodes_spec, note_nodes_bridge, note_nodes_guards, note_nodes_value_text, note_nodes_custom_iterance,
            note_nodes_no_bridge, note_nodes_select_chain, note_nodes_shape, note_nodes_validation)
