/// @file
/// @brief Task 5's spec: the note editor and note-row editor menu trees
///        (`scenes/menu/nodes/note.h`), tested against `FakeNoteBridgeGuard`
///        (`support/fake_note_bridge.h`), and their shape against the live legacy objects
///        (`gui/ui/menus.cpp`) rather than a copied list.
///
/// Covered, per the brief's own list:
///
///   * every leaf's `bindings::read` calls its bridge entry;
///   * every `adjust` passes its offset through to the bridge unchanged;
///   * every value `set` (the two divisors, the sixteen step toggles) computes legacy's own mask;
///   * each guard (`permitted_note`/`permitted_row`) refuses and raises its literal popup;
///   * each step toggle's `relevant` follows the divisor, including the "OFF, index 0" case;
///   * `note_iterance`/`note_row_iterance`'s `select_chain` resolves only on
///     `kCustomIterancePreset`;
///   * the three `value_text` hooks produce the strings spec §5.3 transcribes, including
///     probability's latching and fill's strike;
///   * the shape: each root and custom root has legacy's own child count, read from the live
///     `noteEditorRootMenu`/`noteRowEditorRootMenu`/`noteCustomIteranceRootMenu`/
///     `noteRowCustomIteranceRootMenu` objects rather than a second copy of the list (the way
///     `tables_3_spec.cpp` does for `settingsRootMenu`);
///   * `validate_tree` over `kAllRoots` reports nothing about any node this file declares.
///
/// @note The spec may include `gui/menu_item/` and `gui/ui/menus.h` (for the legacy objects); the
///       library may not (`scripts/check_layer_rules.py`'s `scenes-includes-outside`). Same rule
///       `tables_2_spec.cpp` and `tables_3_spec.cpp` run under.

#include "scenes/menu/bindings.h"
#include "scenes/menu/context.h"
#include "scenes/menu/menu_scene.h"
#include "scenes/menu/node.h"
#include "scenes/menu/nodes/nodes.h"
#include "scenes/menu/nodes/note.h"
#include "scenes/menu/validate.h"

#include "notify/notice_queue.h"

#include "definitions_cxx.hpp"
#include "model/iterance/iterance.h"

#include "support/fake_note_bridge.h"

#include "gui/l10n/l10n.h"
#include "gui/l10n/strings.h"
#include "gui/menu_item/horizontal_menu.h"
#include "gui/menu_item/submenu.h"
// `gui/ui/menus.h` itself includes `note/iterance_preset.h` and `note_row/iterance_preset.h`
// (whose extern `noteCustomIteranceRootMenu`/`noteRowCustomIteranceRootMenu` this file reads), and
// `note/fill.h` (included earlier in `menus.h`'s own chain) is not self-contained -- it relies on
// something ahead of it in the *same translation unit* having already brought in
// `gui/menu_item/integer.h` for its `SelectedNote` base's base. Pre-including either iterance
// header standalone, ahead of `menus.h`, breaks that ordering; `gui/ui/menus.h` alone is enough.
#include "gui/ui/menus.h" // extern noteEditorRootMenu, noteRowEditorRootMenu, noteCustomIteranceRootMenu, noteRowCustomIteranceRootMenu

#include "cppspec.hpp"

#include <set>
#include <span>
#include <string>
#include <string_view>

#include "spec/support/spec_main.h"

namespace menu = deluge::scenes::menu;
namespace nodes = deluge::scenes::menu::nodes;
namespace bindings = deluge::scenes::menu::bindings;
using deluge::scenes::menu::Node;
using deluge::scenes::menu::ValueText;
using deluge::scenes::menu::ValueTextUse;
using deluge::scenes::spec::FakeNoteBridge;
using deluge::scenes::spec::FakeNoteBridgeGuard;
using String = deluge::l10n::String;
namespace legacy = deluge::gui::menu_item;

namespace {

void select_language() {
	deluge::l10n::chosenLanguage = &deluge::l10n::built_in::english;
}

/// @brief `Submenu::items` is protected with no public accessor; only `HorizontalMenu` exposes
///        `getItems()`. Reads it the same way `tables_2_spec.cpp`/`tables_3_spec.cpp` do.
struct SubmenuPeek : legacy::Submenu {
	static const deluge::vector<::MenuItem*>& items_of(legacy::Submenu& menu) { return menu.*(&SubmenuPeek::items); }
};

std::string drain_last_notice() {
	deluge::notify::Notice n;
	std::string text;
	while (deluge::notify::notices().pop(n)) {
		text = n.text;
	}
	return text;
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

		guard.fake.note_iterance_value = Iterance{2, 0b1}.toInt(); // preset 1, "1of2"
		expect(bindings::read(nodes::note_iterance, ctx)).to_equal(int32_t(1));

		guard.fake.note_iterance_value = Iterance{1, 1}.toInt(); // "1of1" is not itself a preset row
		expect(bindings::read(nodes::note_iterance, ctx)).to_equal(int32_t(kCustomIterancePreset));

		bindings::adjust(nodes::note_iterance, ctx, 4);
		expect(guard.fake.adjust_note_iterance_calls).to_equal(int32_t(1));
		expect(guard.fake.adjust_note_iterance_last_offset).to_equal(int32_t(4));
	});

	it("reads and adjusts note_fill through the bridge, offset unchanged", _ {
		FakeNoteBridgeGuard guard;
		menu::MenuContext ctx{};
		guard.fake.note_fill_value = FillMode::NOT_FILL;
		expect(bindings::read(nodes::note_fill, ctx)).to_equal(int32_t(FillMode::NOT_FILL));
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

		guard.fake.row_iterance_value = Iterance{3, 0b100}.toInt(); // preset 5, "3of3"
		expect(bindings::read(nodes::note_row_iterance, ctx)).to_equal(int32_t(5));
		bindings::adjust(nodes::note_row_iterance, ctx, -2);
		expect(guard.fake.adjust_row_iterance_last_offset).to_equal(int32_t(-2));

		guard.fake.row_fill_value = FillMode::FILL;
		expect(bindings::read(nodes::note_row_fill, ctx)).to_equal(int32_t(FillMode::FILL));
		bindings::adjust(nodes::note_row_fill, ctx, 1);
		expect(guard.fake.adjust_row_fill_last_offset).to_equal(int32_t(1));
	});
});

describe note_nodes_guards("scenes::menu note editor/note-row editor: the two guards", $ {
	it("permitted_note refuses and raises \"Select Note\" with nothing selected", _ {
		select_language();
		FakeNoteBridgeGuard guard;
		menu::MenuContext ctx{};
		(void)drain_last_notice();

		guard.fake.note_selected_value = false;
		expect(nodes::note_velocity.bind.permitted(ctx)).to_be_false();
		expect(drain_last_notice()).to_equal(std::string{"Select Note"});

		guard.fake.note_selected_value = true;
		expect(nodes::note_velocity.bind.permitted(ctx)).to_be_true();
		expect(drain_last_notice()).to_equal(std::string{}); // no notice on success
	});

	it("permitted_row refuses and raises \"Select Row\" while not auditioning", _ {
		select_language();
		FakeNoteBridgeGuard guard;
		menu::MenuContext ctx{};
		(void)drain_last_notice();

		guard.fake.row_auditioning_value = false;
		expect(nodes::note_row_probability.bind.permitted(ctx)).to_be_false();
		expect(drain_last_notice()).to_equal(std::string{"Select Row"});

		guard.fake.row_auditioning_value = true;
		expect(nodes::note_row_probability.bind.permitted(ctx)).to_be_true();
		expect(drain_last_notice()).to_equal(std::string{});
	});

	it("carries the guard on every note-editor and note-row-editor leaf but the step toggles", _ {
		expect(nodes::note_velocity.bind.permitted != nullptr).to_be_true();
		expect(nodes::note_editor_probability.bind.permitted != nullptr).to_be_true();
		expect(nodes::note_iterance.bind.permitted != nullptr).to_be_true();
		expect(nodes::note_fill.bind.permitted != nullptr).to_be_true();
		expect(nodes::note_iterance_divisor.bind.permitted != nullptr).to_be_true();
		expect(nodes::note_row_probability.bind.permitted != nullptr).to_be_true();
		expect(nodes::note_row_iterance.bind.permitted != nullptr).to_be_true();
		expect(nodes::note_row_fill.bind.permitted != nullptr).to_be_true();
		expect(nodes::note_row_iterance_divisor.bind.permitted != nullptr).to_be_true();

		expect(nodes::note_iterance_step1.bind.permitted == nullptr).to_be_true();
		expect(nodes::note_iterance_step8.bind.permitted == nullptr).to_be_true();
		expect(nodes::note_row_iterance_step1.bind.permitted == nullptr).to_be_true();
		expect(nodes::note_row_iterance_step8.bind.permitted == nullptr).to_be_true();
	});
});

describe note_nodes_value_text("scenes::menu note editor/note-row editor: value_text", $ {
	it("spells probability with the latching mask, column vs notification", _ {
		FakeNoteBridgeGuard guard;
		menu::MenuContext ctx{};
		const auto fn = nodes::note_editor_probability.bind.value_text;

		// 15 <= kNumProbabilityValues (20): not latching, shown = 15 * 5 = 75.
		expect(value_text_string(fn, ctx, 15, ValueTextUse::Column)).to_equal(std::string{"75%"});
		expect(value_text_string(fn, ctx, 15, ValueTextUse::Notification)).to_equal(std::string{"75%"});

		// 25 > kNumProbabilityValues (20): latching, masked to 25 & 127 = 25, shown = 125.
		expect(value_text_string(fn, ctx, 25, ValueTextUse::Column)).to_equal(std::string{"125L"});
		expect(value_text_string(fn, ctx, 25, ValueTextUse::Notification)).to_equal(std::string{"125% ltch"});
	});

	it("spells iterance's OFF and CUSTOM literals, and the bridge's own label otherwise", _ {
		FakeNoteBridgeGuard guard;
		menu::MenuContext ctx{};
		const auto fn = nodes::note_iterance.bind.value_text;

		expect(value_text_string(fn, ctx, 0, ValueTextUse::Column)).to_equal(std::string{"OFF"});
		expect(value_text_string(fn, ctx, kCustomIterancePreset, ValueTextUse::Notification))
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

		ValueText off_col = fn(ctx, FillMode::OFF, ValueTextUse::Column, std::span<char>(buffer, sizeof(buffer)));
		expect(std::string(buffer, off_col.length)).to_equal(std::string{"OFF"});
		expect(off_col.strikethrough).to_be_false();

		ValueText notfill_col =
		    fn(ctx, FillMode::NOT_FILL, ValueTextUse::Column, std::span<char>(buffer, sizeof(buffer)));
		expect(std::string(buffer, notfill_col.length)).to_equal(std::string{"FILL"});
		expect(notfill_col.strikethrough).to_be_true();

		ValueText fill_col = fn(ctx, FillMode::FILL, ValueTextUse::Column, std::span<char>(buffer, sizeof(buffer)));
		expect(std::string(buffer, fill_col.length)).to_equal(std::string{"FILL"});
		expect(fill_col.strikethrough).to_be_false();

		expect(value_text_string(fn, ctx, FillMode::OFF, ValueTextUse::Notification)).to_equal(std::string{"OFF"});
		expect(value_text_string(fn, ctx, FillMode::NOT_FILL, ValueTextUse::Notification))
		    .to_equal(std::string{"NOT FILL"});
		expect(value_text_string(fn, ctx, FillMode::FILL, ValueTextUse::Notification)).to_equal(std::string{"FILL"});
	});

	it("gives note_row's probability/iterance/fill the identical value_text hooks", _ {
		expect(nodes::note_row_probability.bind.value_text == nodes::note_editor_probability.bind.value_text)
		    .to_be_true();
		expect(nodes::note_row_iterance.bind.value_text == nodes::note_iterance.bind.value_text).to_be_true();
		expect(nodes::note_row_fill.bind.value_text == nodes::note_fill.bind.value_text).to_be_true();
	});
});

describe note_nodes_custom_iterance("scenes::menu note editor/note-row editor: custom iterance", $ {
	it("reads the divisor clamped 1..8, converting OFF to 1of1 first", _ {
		FakeNoteBridgeGuard guard;
		menu::MenuContext ctx{};

		guard.fake.note_iterance_value = Iterance{5, 0b10101}.toInt();
		expect(bindings::read(nodes::note_iterance_divisor, ctx)).to_equal(int32_t(5));

		guard.fake.note_iterance_value = 0; // OFF -> 1of1 -> divisor 1
		expect(bindings::read(nodes::note_iterance_divisor, ctx)).to_equal(int32_t(1));
	});

	it("writes the divisor with legacy's own mask, converting OFF to 1of1 first", _ {
		FakeNoteBridgeGuard guard;
		menu::MenuContext ctx{};

		// divisor 5, steps 0b10101 (bits 0, 2, 4). Narrowing to divisor 3 masks to bits 0..2: 0b101.
		guard.fake.note_iterance_value = Iterance{5, 0b10101}.toInt();
		bindings::write(nodes::note_iterance_divisor, ctx, 3);
		expect(guard.fake.set_note_iterance_calls).to_equal(int32_t(1));
		expect(guard.fake.set_note_iterance_last_value).to_equal(int32_t(Iterance{3, 0b101}.toInt()));

		// OFF -> 1of1 (divisor 1, steps 0b1) before the mask is computed.
		guard.fake.note_iterance_value = 0;
		bindings::write(nodes::note_iterance_divisor, ctx, 1);
		expect(guard.fake.set_note_iterance_last_value).to_equal(int32_t(Iterance{1, 0b1}.toInt()));
	});

	it("reads each step's bit, converting OFF to 1of1 first", _ {
		FakeNoteBridgeGuard guard;
		menu::MenuContext ctx{};

		guard.fake.note_iterance_value = Iterance{5, 0b10101}.toInt(); // bits 0, 2, 4 set
		expect(bindings::read(nodes::note_iterance_step1, ctx)).to_equal(int32_t(1)); // index 0
		expect(bindings::read(nodes::note_iterance_step2, ctx)).to_equal(int32_t(0)); // index 1
		expect(bindings::read(nodes::note_iterance_step3, ctx)).to_equal(int32_t(1)); // index 2

		guard.fake.note_iterance_value = 0; // OFF -> 1of1: divisor 1, step bit 0 only
		expect(bindings::read(nodes::note_iterance_step1, ctx)).to_equal(int32_t(1));
	});

	it("writes each step's bit with an OR or an AND-NOT, keeping the current divisor", _ {
		FakeNoteBridgeGuard guard;
		menu::MenuContext ctx{};

		// divisor 5, steps 0b10101; index 1 (bit 2) is currently clear -- set it.
		guard.fake.note_iterance_value = Iterance{5, 0b10101}.toInt();
		bindings::write(nodes::note_iterance_step2, ctx, 1);
		expect(guard.fake.set_note_iterance_last_value).to_equal(int32_t(Iterance{5, 0b10111}.toInt()));

		// Same starting value; index 0 (bit 1) is currently set -- clear it.
		guard.fake.note_iterance_value = Iterance{5, 0b10101}.toInt();
		bindings::write(nodes::note_iterance_step1, ctx, 0);
		expect(guard.fake.set_note_iterance_last_value).to_equal(int32_t(Iterance{5, 0b10100}.toInt()));
	});

	it("shows a step relevant only under the divisor, or index 0 while iterance is off", _ {
		FakeNoteBridgeGuard guard;
		menu::MenuContext ctx{};

		guard.fake.note_iterance_value = Iterance{3, 0b101}.toInt(); // divisor 3
		expect(nodes::note_iterance_step1.relevant(ctx)).to_be_true();  // index 0: 3 > 0
		expect(nodes::note_iterance_step2.relevant(ctx)).to_be_true();  // index 1: 3 > 1
		expect(nodes::note_iterance_step3.relevant(ctx)).to_be_true();  // index 2: 3 > 2
		expect(nodes::note_iterance_step4.relevant(ctx)).to_be_false(); // index 3: 3 > 3 is false

		guard.fake.note_iterance_value = 0; // OFF, unconverted for this test
		expect(nodes::note_iterance_step1.relevant(ctx)).to_be_true();  // index 0, OFF
		expect(nodes::note_iterance_step2.relevant(ctx)).to_be_false(); // index 1, OFF
	});

	it("mirrors every one of the above through the row half of the bridge", _ {
		FakeNoteBridgeGuard guard;
		menu::MenuContext ctx{};

		guard.fake.row_iterance_value = Iterance{4, 0b1010}.toInt();
		expect(bindings::read(nodes::note_row_iterance_divisor, ctx)).to_equal(int32_t(4));
		expect(bindings::read(nodes::note_row_iterance_step2, ctx)).to_equal(int32_t(1)); // bit 1 set
		expect(bindings::read(nodes::note_row_iterance_step1, ctx)).to_equal(int32_t(0)); // bit 0 clear

		bindings::write(nodes::note_row_iterance_divisor, ctx, 2);
		expect(guard.fake.set_row_iterance_last_value).to_equal(int32_t(Iterance{2, 0b10}.toInt()));

		bindings::write(nodes::note_row_iterance_step1, ctx, 1);
		expect(guard.fake.set_row_iterance_last_value).to_equal(int32_t(Iterance{4, 0b1011}.toInt()));

		guard.fake.row_iterance_value = 0;
		expect(nodes::note_row_iterance_step1.relevant(ctx)).to_be_true();
		expect(nodes::note_row_iterance_step2.relevant(ctx)).to_be_false();
	});
});

describe note_nodes_select_chain("scenes::menu note editor/note-row editor: the iterance chain", $ {
	it("chains to the custom-iterance root only on kCustomIterancePreset", _ {
		FakeNoteBridgeGuard guard;
		menu::MenuContext ctx{};
		menu::MenuState state{};

		guard.fake.note_iterance_value = Iterance{2, 0b1}.toInt(); // preset 1, not CUSTOM
		expect(nodes::note_iterance.select_chain(ctx, state) == nullptr).to_be_true();

		guard.fake.note_iterance_value = Iterance{1, 1}.toInt(); // maps to CUSTOM (36)
		expect(nodes::note_iterance.select_chain(ctx, state) == &nodes::note_custom_iterance_root).to_be_true();

		guard.fake.row_iterance_value = Iterance{2, 0b1}.toInt();
		expect(nodes::note_row_iterance.select_chain(ctx, state) == nullptr).to_be_true();

		guard.fake.row_iterance_value = Iterance{1, 1}.toInt();
		expect(nodes::note_row_iterance.select_chain(ctx, state) == &nodes::note_row_custom_iterance_root)
		    .to_be_true();
	});

	it("lists exactly its one chain target, matched with its own hook", _ {
		expect(nodes::note_iterance.chain_targets.size()).to_equal(size_t(1));
		expect(nodes::note_iterance.chain_targets[0] == &nodes::note_custom_iterance_root).to_be_true();
		expect(nodes::note_row_iterance.chain_targets.size()).to_equal(size_t(1));
		expect(nodes::note_row_iterance.chain_targets[0] == &nodes::note_row_custom_iterance_root).to_be_true();
	});
});

describe note_nodes_shape("scenes::menu note editor/note-row editor: shape against the live legacy objects", $ {
	it("gives note_editor_root legacy's own four children, in the horizontal menu itself", _ {
		expect(noteEditorRootMenu.getItems().size()).to_equal(size_t(4));
		expect(nodes::note_editor_root.children.size()).to_equal(size_t(4));
		expect(nodes::note_editor_root.layout == menu::Layout::Horizontal).to_be_true();
	});

	it("gives note_row_editor_root legacy's own four children, sequence direction first", _ {
		expect(noteRowEditorRootMenu.getItems().size()).to_equal(size_t(4));
		expect(nodes::note_row_editor_root.children.size()).to_equal(size_t(4));
		expect(nodes::note_row_editor_root.children[0] == &nodes::sequence_direction).to_be_true();
		expect(nodes::note_row_editor_root.layout == menu::Layout::Horizontal).to_be_true();
	});

	it("gives note_custom_iterance_root legacy's own nine children (divisor + 8 steps)", _ {
		expect(SubmenuPeek::items_of(noteCustomIteranceRootMenu).size()).to_equal(size_t(9));
		expect(nodes::note_custom_iterance_root.children.size()).to_equal(size_t(9));
		expect(nodes::note_custom_iterance_root.layout == menu::Layout::Vertical).to_be_true();
	});

	it("gives note_row_custom_iterance_root legacy's own nine children (divisor + 8 steps)", _ {
		expect(SubmenuPeek::items_of(noteRowCustomIteranceRootMenu).size()).to_equal(size_t(9));
		expect(nodes::note_row_custom_iterance_root.children.size()).to_equal(size_t(9));
		expect(nodes::note_row_custom_iterance_root.layout == menu::Layout::Vertical).to_be_true();
	});

	it("appends both editor roots to the end of kAllRootArray, after multi_range", _ {
		const size_t n = nodes::kAllRoots.size();
		expect(n >= size_t(2)).to_be_true();
		expect(nodes::kAllRoots[n - 2] == &nodes::note_editor_root).to_be_true();
		expect(nodes::kAllRoots[n - 1] == &nodes::note_row_editor_root).to_be_true();
		expect(nodes::kAllRoots[n - 3] == &nodes::multi_range).to_be_true();
	});

	it("does not add either custom-iterance root to kAllRootArray -- chain_targets only", _ {
		for (const Node* root : nodes::kAllRoots) {
			expect(root == &nodes::note_custom_iterance_root).to_be_false();
			expect(root == &nodes::note_row_custom_iterance_root).to_be_false();
		}
	});
});

describe note_nodes_validation("scenes::menu note editor/note-row editor: validate_tree", $ {
	it("reports nothing about any node this file declares", _ {
		select_language();
		const auto issues = menu::validate_tree(nodes::kAllRoots, {}, nodes::kRangeDependentNodes,
		                                        nodes::kHalfPrecisionNodes, menu::ValidateOptions{});
		const std::set<const Node*> ours = {
		    &nodes::note_editor_root,
		    &nodes::note_velocity,
		    &nodes::note_editor_probability,
		    &nodes::note_iterance,
		    &nodes::note_fill,
		    &nodes::note_custom_iterance_root,
		    &nodes::note_iterance_divisor,
		    &nodes::note_iterance_step1,
		    &nodes::note_iterance_step2,
		    &nodes::note_iterance_step3,
		    &nodes::note_iterance_step4,
		    &nodes::note_iterance_step5,
		    &nodes::note_iterance_step6,
		    &nodes::note_iterance_step7,
		    &nodes::note_iterance_step8,
		    &nodes::note_row_editor_root,
		    &nodes::note_row_probability,
		    &nodes::note_row_iterance,
		    &nodes::note_row_fill,
		    &nodes::note_row_custom_iterance_root,
		    &nodes::note_row_iterance_divisor,
		    &nodes::note_row_iterance_step1,
		    &nodes::note_row_iterance_step2,
		    &nodes::note_row_iterance_step3,
		    &nodes::note_row_iterance_step4,
		    &nodes::note_row_iterance_step5,
		    &nodes::note_row_iterance_step6,
		    &nodes::note_row_iterance_step7,
		    &nodes::note_row_iterance_step8,
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

/// Seven `describe` groups in one TU, so `DELUGE_SPEC` is spelled out here with `add_specs`
/// (`tables_3_spec.cpp`'s own precedent).
int note_nodes_spec(int argc, char** const argv) {
	return CppSpec::parse(argc, argv)
	               .add_specs(note_nodes_bridge, note_nodes_guards, note_nodes_value_text, note_nodes_custom_iterance,
	                          note_nodes_select_chain, note_nodes_shape, note_nodes_validation)
	               .exec()
	               .is_success()
	           ? EXIT_SUCCESS
	           : EXIT_FAILURE;
}
