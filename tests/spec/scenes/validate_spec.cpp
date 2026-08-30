/// @file
/// @brief `validate_tree` over hand-built fake trees and fake contexts (spec E §4.4).
///
/// Pure: no model, no `Song`, no `gui/`. The real tables live behind `deluge_scenes`, which this
/// host suite deliberately never builds (`tests/spec/CMakeLists.txt`); `validate_tree` over the
/// real batch-1 roots is `sim/scenes_spec/tables_1_spec.cpp`'s job, where a real `MenuContext` can
/// be resolved, and the real shortcut tables are cross-checked in
/// `sim/scenes_spec/shortcuts_spec.cpp`. What is checked here is the rule engine itself -- one fake
/// tree that breaks four rules at once, and one that breaks none -- with the shortcut lookup the
/// `ShortcutToNothing` rule sweeps supplied by `support/fake_shortcuts.h`.

#include "modulation/params/param.h"
#include "scenes/menu/context.h"
#include "scenes/menu/node.h"
#include "scenes/menu/node_id.h"
#include "scenes/menu/validate.h"
#include "shell/scene.h" // ui::shell::Result -- CustomItem::handle returns one by value

#include "support/fake_shortcuts.h"

#include "cppspec.hpp"

#include <array>
#include <cstdint>

#include "../support/spec_main.h"

using namespace deluge::scenes::menu;
namespace params = deluge::modulation::params;

namespace {

/// @brief A `CustomItem` that renders nothing -- the spec only ever counts these, never draws one.
class NullItem final : public CustomItem {
public:
	void render(deluge::ui::oled::OledFrame&, const MenuContext&, MenuState&) const override {}
	deluge::ui::shell::Result handle(const deluge::ui::shell::UiEvent&, const deluge::ui::shell::UiContext&,
	                                 const MenuContext&, MenuState&) const override {
		return {};
	}
};

/// @brief The **one** shared `CustomItem` every custom node below points at.
///
/// Fix round 2, I4: the cap counts *nodes*, not distinct instances, so the case that must trip it
/// is exactly the one the old distinct-instance count could not see -- many nodes behind one
/// implementation, which is what the real tree does with `nodes::kPending`. Sharing one item here
/// means this example fails if the rule ever goes back to counting instances.
const NullItem kSharedItem;

bool always(const MenuContext&) {
	return true;
}
bool never(const MenuContext&) {
	return false;
}

int32_t zero(const MenuContext&) {
	return 0;
}
void discard(const MenuContext&, int32_t) {
}

size_t count_of(const deluge::ui::shell::FixedVector<Issue, 64>& issues, Issue::Kind kind) {
	size_t n = 0;
	for (const Issue& i : issues) {
		if (i.kind == kind) {
			n++;
		}
	}
	return n;
}

// -------------------------------------------------------------------------------------------
// The clean tree: a vertical submenu over one param and one enum, both relevant, both titled.

constexpr uint16_t kEnumNames[] = {10, 11};

constexpr Node good_param = param(101, ParamRef{params::Kind::PATCHED, 3}, Hint::Bar, always);
constexpr Node good_enum = enumeration(102, kEnumNames, zero, discard);
constexpr const Node* kGoodKids[] = {&good_param, &good_enum};
constexpr Node good_root = submenu(100, kGoodKids);
constexpr const Node* kGoodRoots[] = {&good_root};

// -------------------------------------------------------------------------------------------
// The broken tree. One violation of each of four rules and nothing else:
//   * `EmptySubmenu`  -- `bad_empty` declares a child that no context admits.
//   * `EnumWithoutNames` -- `bad_enum` has neither `names` nor `dynamic_names`.
//   * `TooManyCustom` -- `kMaxCustomNodes + 1` custom *nodes*, all behind one `CustomItem`.
//   * `ShortcutToNothing` -- a table cell pointing at `stray`, which no root reaches.
//
// Everything else is deliberately well formed: distinct names per parent, non-zero titles, a
// valid `ParamRef` or a `get` on every value node, and a non-null `custom` on every custom node.

constexpr Node hidden_child = param(201, ParamRef{params::Kind::PATCHED, 4}, Hint::Bar, never);
constexpr const Node* kEmptyKids[] = {&hidden_child};
constexpr Node bad_empty = submenu(200, kEmptyKids);

constexpr Node bad_enum = enumeration(202, {}, zero, discard);

/// One more custom node than the cap allows, each with its own name (so `DuplicateName` stays
/// silent) and all sharing `kSharedItem`.
constexpr auto kCustomNodes = [] {
	std::array<Node, kMaxCustomNodes + 1> out{};
	for (size_t i = 0; i < out.size(); i++) {
		out[i] = custom(static_cast<uint16_t>(300 + i), &kSharedItem);
	}
	return out;
}();

constexpr auto kBadKids = [] {
	std::array<const Node*, kCustomNodes.size() + 2> out{};
	out[0] = &bad_empty;
	out[1] = &bad_enum;
	for (size_t i = 0; i < kCustomNodes.size(); i++) {
		out[i + 2] = &kCustomNodes[i];
	}
	return out;
}();
constexpr Node bad_root = submenu(199, kBadKids);
constexpr const Node* kBadRoots[] = {&bad_root};

/// Never a child of anything: the dangling shortcut target.
constexpr Node stray = param(999, ParamRef{params::Kind::PATCHED, 5});
constexpr const Node* kStrayTable[] = {&stray};

/// @brief One fake context: "a synth with an LPF". Nothing in this spec reads a model pointer --
///        only the `relevant` hooks above see it, and they ignore it -- so the flags are all this
///        needs to carry.
MenuContext fake_context() {
	MenuContext ctx;
	ctx.resolved = true;
	ctx.root = MenuContext::Root::Sound;
	return ctx;
}

} // namespace

// clang-format off
describe scenes_validate("scenes::menu::validate_tree", $ {
	it("passes a well-formed tree with no issues at all", _ {
		set_shortcut_table_for_test({}, 0, 0);
		const MenuContext contexts[] = {fake_context()};
		const auto issues = validate_tree(kGoodRoots, contexts);
		expect(issues.size()).to_equal(size_t(0));
	});

	it("assigns an id to every reachable node before it checks anything", _ {
		set_shortcut_table_for_test({}, 0, 0);
		const MenuContext contexts[] = {fake_context()};
		(void)validate_tree(kGoodRoots, contexts);
		expect(node_id(good_root) != uint16_t(0)).to_be_true();
		expect(node_id(good_param) != uint16_t(0)).to_be_true();
		expect(node_id(good_enum) != uint16_t(0)).to_be_true();
	});

	it("reports exactly the four rules the broken tree breaks, and no others", _ {
		set_shortcut_table_for_test(kStrayTable, 1, 1);
		const MenuContext contexts[] = {fake_context()};
		const auto issues = validate_tree(kBadRoots, contexts);
		set_shortcut_table_for_test({}, 0, 0);

		expect(count_of(issues, Issue::Kind::EmptySubmenu)).to_equal(size_t(1));
		expect(count_of(issues, Issue::Kind::EnumWithoutNames)).to_equal(size_t(1));
		expect(count_of(issues, Issue::Kind::TooManyCustom)).to_equal(size_t(1));
		// One cell per (root, shift layer) the sweep visits, all naming the same stray node.
		expect(count_of(issues, Issue::Kind::ShortcutToNothing)
		       == size_t(2) * size_t(MenuContext::Root::Count)).to_be_true();

		expect(count_of(issues, Issue::Kind::Unreachable)).to_equal(size_t(0));
		expect(count_of(issues, Issue::Kind::BadParamRef)).to_equal(size_t(0));
		expect(count_of(issues, Issue::Kind::DuplicateName)).to_equal(size_t(0));
		expect(count_of(issues, Issue::Kind::MissingTitle)).to_equal(size_t(0));
		expect(count_of(issues, Issue::Kind::NullCustom)).to_equal(size_t(0));
		expect(count_of(issues, Issue::Kind::NullInfo)).to_equal(size_t(0));
		expect(count_of(issues, Issue::Kind::FlagMismatch)).to_equal(size_t(0));
	});

	it("names the empty submenu, not one of its parents", _ {
		set_shortcut_table_for_test({}, 0, 0);
		const MenuContext contexts[] = {fake_context()};
		const auto issues = validate_tree(kBadRoots, contexts);
		const Node* named = nullptr;
		for (const Issue& i : issues) {
			if (i.kind == Issue::Kind::EmptySubmenu) {
				named = i.node;
			}
		}
		expect(named == &bad_empty).to_be_true();
	});
});

describe scenes_validate_rules("scenes::menu::validate_tree individual rules", $ {
	it("flags a Kind::Custom with no implementation", _ {
		set_shortcut_table_for_test({}, 0, 0);
		static constexpr Node null_custom = custom(400, nullptr);
		static constexpr const Node* kids[] = {&null_custom};
		static constexpr Node root = submenu(401, kids);
		static constexpr const Node* roots[] = {&root};
		const auto issues = validate_tree(roots, {});
		expect(count_of(issues, Issue::Kind::NullCustom)).to_equal(size_t(1));
	});

	it("flags a Kind::Info with no implementation, as NullInfo rather than NullCustom", _ {
		// The pre-flight ruling: a null `info` hook must not reuse `NullCustom` -- the two kinds'
		// null checks are about different fields, and conflating them would read as a copy-paste bug
		// at this exact call site.
		set_shortcut_table_for_test({}, 0, 0);
		static constexpr Node null_info = info_item(404, nullptr);
		static constexpr const Node* kids[] = {&null_info};
		static constexpr Node root = submenu(405, kids);
		static constexpr const Node* roots[] = {&root};
		const auto issues = validate_tree(roots, {});
		expect(count_of(issues, Issue::Kind::NullInfo)).to_equal(size_t(1));
		expect(count_of(issues, Issue::Kind::NullCustom)).to_equal(size_t(0));
	});

	it("flags a ParamRef whose index is outside its kind's parameter space", _ {
		set_shortcut_table_for_test({}, 0, 0);
		static constexpr Node bad = param(402, ParamRef{params::Kind::PATCHED, 254});
		static constexpr const Node* kids[] = {&bad};
		static constexpr Node root = submenu(403, kids);
		static constexpr const Node* roots[] = {&root};
		const auto issues = validate_tree(roots, {});
		expect(count_of(issues, Issue::Kind::BadParamRef)).to_equal(size_t(1));
	});

	it("flags two siblings that share a name", _ {
		set_shortcut_table_for_test({}, 0, 0);
		static constexpr Node a = param(404, ParamRef{params::Kind::PATCHED, 1});
		static constexpr Node b = param(404, ParamRef{params::Kind::PATCHED, 2});
		static constexpr const Node* kids[] = {&a, &b};
		static constexpr Node root = submenu(405, kids);
		static constexpr const Node* roots[] = {&root};
		const auto issues = validate_tree(roots, {});
		expect(count_of(issues, Issue::Kind::DuplicateName)).to_equal(size_t(1));
	});

	it("flags a node with no title at all", _ {
		set_shortcut_table_for_test({}, 0, 0);
		static constexpr Node untitled = param(0, ParamRef{params::Kind::PATCHED, 1});
		static constexpr const Node* kids[] = {&untitled};
		static constexpr Node root = submenu(406, kids);
		static constexpr const Node* roots[] = {&root};
		const auto issues = validate_tree(roots, {});
		expect(count_of(issues, Issue::Kind::MissingTitle)).to_equal(size_t(1));
	});

	it("flags a range_dependent node that the expected list does not name, and vice versa", _ {
		set_shortcut_table_for_test({}, 0, 0);
		static constexpr Node flagged = with_range_dependent(param(407, ParamRef{params::Kind::PATCHED, 1}));
		static constexpr Node plain = param(408, ParamRef{params::Kind::PATCHED, 2});
		static constexpr const Node* kids[] = {&flagged, &plain};
		static constexpr Node root = submenu(409, kids);
		static constexpr const Node* roots[] = {&root};

		// Nothing expected -> the one flagged node is an issue.
		expect(count_of(validate_tree(roots, {}), Issue::Kind::FlagMismatch)).to_equal(size_t(1));

		// The right node expected -> clean.
		static constexpr const Node* expected_ok[] = {&flagged};
		expect(count_of(validate_tree(roots, {}, expected_ok), Issue::Kind::FlagMismatch)).to_equal(size_t(0));

		// The wrong node expected -> two issues: one set-but-unexpected, one expected-but-unset.
		static constexpr const Node* expected_wrong[] = {&plain};
		expect(count_of(validate_tree(roots, {}, expected_wrong), Issue::Kind::FlagMismatch)).to_equal(size_t(2));
	});

	it("flags a half-precision binding the expected list does not name", _ {
		set_shortcut_table_for_test({}, 0, 0);
		static constexpr Node half = param(410, ParamRef{params::Kind::PATCHED, 1}, Hint::Bar, nullptr, 0,
		                                   Scaling::HalfPrecision);
		static constexpr const Node* kids[] = {&half};
		static constexpr Node root = submenu(411, kids);
		static constexpr const Node* roots[] = {&root};

		expect(count_of(validate_tree(roots, {}), Issue::Kind::FlagMismatch)).to_equal(size_t(1));
		static constexpr const Node* expected[] = {&half};
		expect(count_of(validate_tree(roots, {}, {}, expected), Issue::Kind::FlagMismatch)).to_equal(size_t(0));
	});
});
// clang-format on

/// Two `describe` groups in one TU, so `DELUGE_SPEC` (one spec per entry point) is spelled out here
/// with `add_specs` instead -- as `sim/scenes_spec/menu_scene_spec.cpp` does.
int scenes_validate_spec(int argc, char** const argv) {
	return CppSpec::parse(argc, argv).add_specs(scenes_validate, scenes_validate_rules).exec().is_success()
	           ? EXIT_SUCCESS
	           : EXIT_FAILURE;
}
