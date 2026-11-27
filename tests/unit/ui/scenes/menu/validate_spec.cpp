/// @file
/// @brief `validate_tree` over hand-built fake trees and fake contexts.
///
/// Pure: no model, no `Song`, no `gui/`. The real tables live behind `deluge_ui_scenes`, which this
/// host suite deliberately never builds (`tests/trees/host/CMakeLists.txt`); `validate_tree` over the
/// real roots is `tests/integration/ui/scenes/tables_1_spec.cpp`'s job, where a real `MenuContext` can
/// be resolved, and the real shortcut tables are cross-checked in
/// `tests/integration/ui/scenes/shortcuts_spec.cpp`. What is checked here is the rule engine itself -- one fake
/// tree that breaks four rules at once, and one that breaks none -- with the shortcut lookup the
/// `ShortcutToNothing` rule sweeps supplied by `tests/unit/ui/scenes/menu/support/fake_shortcuts.h`.

#include "modulation/params/param.h"
#include "ui/scenes/menu/context.h"
#include "ui/scenes/menu/node.h"
#include "ui/scenes/menu/node_id.h"
#include "ui/scenes/menu/validate.h"
#include "ui/shell/scene.h" // ui::shell::Result -- CustomItem::handle returns one by value

#include "support/fake_shortcuts.h"

#include "cppspec.hpp"

#include <array>
#include <cstdint>
#include <vector>

#include "support/spec_main.h"

using namespace deluge::ui::scenes::menu;
namespace params = deluge::modulation::params;

namespace {

/// @brief A `CustomItem` that renders nothing -- the spec only ever counts these, never draws one.
class NullItem final : public CustomItem {
public:
	void render(deluge::ui::widgets::oled::OledFrame&, const MenuContext&, MenuState&) const override {}
	deluge::ui::shell::Result handle(const deluge::ui::shell::UiEvent&, const deluge::ui::shell::UiContext&,
	                                 const MenuContext&, MenuState&) const override {
		return {};
	}
};

/// @brief The **one** shared `CustomItem` every custom node below points at.
///
/// The cap counts *nodes*, not distinct instances, so the case that must trip it
/// is exactly the one a distinct-instance count could not see -- many nodes behind one
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
void discard_offset(const MenuContext&, int32_t) {
}

/// @brief A `SelectChainFn` the validator never calls -- the rules are about the *data*
///        (`Node::select_chain` beside `Node::chain_targets`), which is the whole reason the static
///        span exists: no walker can supply a live `MenuContext`.
const Node* chain_hook(const MenuContext&, const MenuState&) {
	return nullptr;
}

size_t count_of(const deluge::base::FixedVector<Issue, 64>& issues, Issue::Kind kind) {
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

/// A value node on a relevance hook, as a hooked menu item is: the rules read its hook and its ParamRef, not a target.
constexpr Node hooked(uint16_t name, ParamRef p, Hint h, RelevantFn rel) {
	Node n = param(name, p, h);
	n.bind.target = {};
	n.relevant = rel;
	return n;
}

constexpr uint16_t kEnumNames[] = {10, 11};

constexpr Node good_param = hooked(101, sound_param(deluge::plugin::ParamId{3}), Hint::Bar, always);
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

constexpr Node hidden_child = hooked(201, sound_param(deluge::plugin::ParamId{4}), Hint::Bar, never);
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
constexpr Node stray = param(999, sound_param(deluge::plugin::ParamId{5}));
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
		expect(node_id(good_root)).not_().to_equal(uint16_t(0));
		expect(node_id(good_param)).not_().to_equal(uint16_t(0));
		expect(node_id(good_enum)).not_().to_equal(uint16_t(0));
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
		expect(count_of(issues, Issue::Kind::ShortcutToNothing)).to_equal(size_t(2) * size_t(MenuContext::Root::Count));

		expect(count_of(issues, Issue::Kind::Unreachable)).to_equal(size_t(0));
		expect(count_of(issues, Issue::Kind::BadParamRef)).to_equal(size_t(0));
		expect(count_of(issues, Issue::Kind::DuplicateName)).to_equal(size_t(0));
		expect(count_of(issues, Issue::Kind::MissingTitle)).to_equal(size_t(0));
		expect(count_of(issues, Issue::Kind::NullCustom)).to_equal(size_t(0));
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
		expect(named).to_equal(&bad_empty);
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

	it("flags a ParamRef whose index is outside its kind's parameter space", _ {
		set_shortcut_table_for_test({}, 0, 0);
		static constexpr Node bad = param(402, sound_param(deluge::plugin::ParamId{254}));
		static constexpr const Node* kids[] = {&bad};
		static constexpr Node root = submenu(403, kids);
		static constexpr const Node* roots[] = {&root};
		const auto issues = validate_tree(roots, {});
		expect(count_of(issues, Issue::Kind::BadParamRef)).to_equal(size_t(1));
	});

	it("flags two siblings that share a name", _ {
		set_shortcut_table_for_test({}, 0, 0);
		// Hook nodes sharing a predicate show together wherever either does; target nodes need a context to tell.
		static constexpr Node a = hooked(404, sound_param(deluge::plugin::ParamId{1}), Hint::Knob, always);
		static constexpr Node b = hooked(404, sound_param(deluge::plugin::ParamId{2}), Hint::Knob, always);
		static constexpr const Node* kids[] = {&a, &b};
		static constexpr Node root = submenu(405, kids);
		static constexpr const Node* roots[] = {&root};
		const auto issues = validate_tree(roots, {});
		expect(count_of(issues, Issue::Kind::DuplicateName)).to_equal(size_t(1));
	});

	it("flags a node with no title at all", _ {
		set_shortcut_table_for_test({}, 0, 0);
		static constexpr Node untitled = param(0, sound_param(deluge::plugin::ParamId{1}));
		static constexpr const Node* kids[] = {&untitled};
		static constexpr Node root = submenu(406, kids);
		static constexpr const Node* roots[] = {&root};
		const auto issues = validate_tree(roots, {});
		expect(count_of(issues, Issue::Kind::MissingTitle)).to_equal(size_t(1));
	});

	it("flags a range_dependent node that the expected list does not name, and vice versa", _ {
		set_shortcut_table_for_test({}, 0, 0);
		static constexpr Node flagged = with_range_dependent(param(407, sound_param(deluge::plugin::ParamId{1})));
		static constexpr Node plain = param(408, sound_param(deluge::plugin::ParamId{2}));
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

	it("walks chain targets, so a chain-only page is validated rather than reported unreachable", _ {
		// `collect` follows `chain_targets` as well as `children`, matching `assign_node_ids`. If it
		// did not, the two counts would disagree and every clean tree with a chain would report
		// `Unreachable`; and the chain-only page's own rules (here, a missing `names` on an enum)
		// would never be checked at all.
		set_shortcut_table_for_test({}, 0, 0);
		static constexpr Node chain_target = enumeration(420, {}, zero, discard); // no names -> one issue
		static constexpr const Node* targets[] = {&chain_target};
		static constexpr Node source =
		    with_select_chain(param(421, sound_param(deluge::plugin::ParamId{1}), Hint::Bar), chain_hook, targets);
		static constexpr const Node* kids[] = {&source};
		static constexpr Node root = submenu(422, kids);
		static constexpr const Node* roots[] = {&root};

		const auto issues = validate_tree(roots, {});
		expect(count_of(issues, Issue::Kind::Unreachable)).to_equal(size_t(0));
		expect(count_of(issues, Issue::Kind::EnumWithoutNames)).to_equal(size_t(1));
		expect(node_id(chain_target)).not_().to_equal(uint16_t(0));
	});

	it("flags a select_chain hook without targets, and targets without a hook", _ {
		// `Flag::ChainTargetsMismatch`. A hook with no targets is a page nothing can number; targets
		// with no hook is a page nothing can open.
		set_shortcut_table_for_test({}, 0, 0);
		static constexpr Node target = param(430, sound_param(deluge::plugin::ParamId{1}), Hint::Bar);
		static constexpr const Node* targets[] = {&target};

		static Node hook_only = param(431, sound_param(deluge::plugin::ParamId{1}), Hint::Bar);
		hook_only.select_chain = chain_hook; // no `chain_targets`
		static Node targets_only = param(432, sound_param(deluge::plugin::ParamId{1}), Hint::Bar);
		targets_only.chain_targets = targets; // no `select_chain`

		static const Node* kids[] = {&hook_only, &targets_only};
		static Node root = submenu(433, kids);
		static const Node* roots[] = {&root};

		const auto issues = validate_tree(roots, {});
		size_t mismatches = 0;
		for (const Issue& i : issues) {
			if (i.kind == Issue::Kind::FlagMismatch && i.detail == static_cast<uint16_t>(Flag::ChainTargetsMismatch)) {
				mismatches++;
			}
		}
		expect(mismatches).to_equal(size_t(2));
	});

	it("flags a half-precision binding the expected list does not name", _ {
		set_shortcut_table_for_test({}, 0, 0);
		static constexpr Node half = param(410, sound_param(deluge::plugin::ParamId{1}), Hint::Bar, nullptr, 0,
		                                   Scaling::HalfPrecision);
		static constexpr const Node* kids[] = {&half};
		static constexpr Node root = submenu(411, kids);
		static constexpr const Node* roots[] = {&root};

		expect(count_of(validate_tree(roots, {}), Issue::Kind::FlagMismatch)).to_equal(size_t(1));
		static constexpr const Node* expected[] = {&half};
		expect(count_of(validate_tree(roots, {}, {}, expected), Issue::Kind::FlagMismatch)).to_equal(size_t(0));
	});

	it("flags Binding::adjust on a node with no get -- it would read through param and write "
	   "through the hook", _ {
		// `Flag::AdjustWithoutGet`. A `param()` node has `bind.param` but
		// no `bind.get`, so pairing it with `adjust` leaves `bindings::read` resolving through the
		// param while `bindings::adjust` writes through the hook -- the two disagreeing about where
		// the value lives.
		set_shortcut_table_for_test({}, 0, 0);
		static constexpr Node no_get =
		    with_adjust(param(440, sound_param(deluge::plugin::ParamId{1}), Hint::Bar), discard_offset);
		static constexpr const Node* kids[] = {&no_get};
		static constexpr Node root = submenu(441, kids);
		static constexpr const Node* roots[] = {&root};

		size_t mismatches = 0;
		for (const Issue& i : validate_tree(roots, {})) {
			if (i.kind == Issue::Kind::FlagMismatch && i.detail == static_cast<uint16_t>(Flag::AdjustWithoutGet)) {
				mismatches++;
			}
		}
		expect(mismatches).to_equal(size_t(1));
	});

	it("does not flag Binding::adjust on a node that also has get", _ {
		// The shipped shape: `read` stays `get` even for an offset-writing node, so a
		// `get`/`set` node with `adjust` added is well-formed and must stay silent.
		set_shortcut_table_for_test({}, 0, 0);
		static constexpr Node with_get = with_adjust(
		    int_item(442, zero, discard, deluge::ui::widgets::oled::Range{0, 50, 1}), discard_offset);
		static constexpr const Node* kids[] = {&with_get};
		static constexpr Node root = submenu(443, kids);
		static constexpr const Node* roots[] = {&root};

		size_t mismatches = 0;
		for (const Issue& i : validate_tree(roots, {})) {
			if (i.kind == Issue::Kind::FlagMismatch && i.detail == static_cast<uint16_t>(Flag::AdjustWithoutGet)) {
				mismatches++;
			}
		}
		expect(mismatches).to_equal(size_t(0));
	});

	it("flags a target node that also sets a hook, and passes a bare one and one whose relevance narrows it", _ {
		// `Flag::TargetWithHooks`: a target's descriptor gives its value, range, names and relevance, so a hook
		// beside it is a second source of the same thing. A `relevant` beside it narrows where it applies, and is
		// asked too.
		set_shortcut_table_for_test({}, 0, 0);
		static constexpr deluge::ui::scenes::bind::Target kTarget{.kind = deluge::ui::scenes::bind::Target::Kind::SOUND,
		                                                       .path = "polyphonic"};
		static constexpr Node bare = setting_item(444, Kind::Enum, kTarget);
		static constexpr Node hooked = [] {
			Node n = setting_item(445, Kind::Enum, kTarget);
			n.bind.get = zero;
			return n;
		}();
		static constexpr Node adjusted = with_adjust(setting_item(447, Kind::Int, kTarget), discard_offset);
		static constexpr Node narrowed = [] {
			Node n = setting_item(448, Kind::Enum, kTarget);
			n.relevant = [](const MenuContext&) { return true; };
			return n;
		}();
		static constexpr const Node* kids[] = {&bare, &hooked, &adjusted, &narrowed};
		static constexpr Node root = submenu(446, kids);
		static constexpr const Node* roots[] = {&root};

		std::vector<uint16_t> flagged;
		for (const Issue& i : validate_tree(roots, {})) {
			if (i.kind == Issue::Kind::FlagMismatch && i.detail == static_cast<uint16_t>(Flag::TargetWithHooks)) {
				flagged.push_back(i.node->name);
			}
			expect(i.node).not_().to_equal(&bare);
			expect(i.node).not_().to_equal(&narrowed);
		}
		expect(flagged).to_equal(std::vector<uint16_t>{445, 447});
	});

	it("flags a range whose lower edge is no DEVICE row's, and passes one on a DEVICE row", _ {
		// `Issue::Kind::RangeWithoutSpec`: a range's upper edge is written to the device port alone, so its lower edge
		// must be a DEVICE row too.
		set_shortcut_table_for_test({}, 0, 0);
		static constexpr deluge::ui::scenes::bind::Target kSound{.kind = deluge::ui::scenes::bind::Target::Kind::SOUND,
		                                                      .path = "polyphonic"};
		static constexpr deluge::ui::scenes::bind::Target kDevice{
		    .kind = deluge::ui::scenes::bind::Target::Kind::MODEL,
		    .path = deluge::project::device_path::kTempoLower,
		    .group = deluge::project::SettingGroupId::DEVICE};
		static constexpr RangeSpec kSpec{.upper_path = deluge::project::device_path::kTempoUpper,
		                                 .format = [](int32_t, const void*, std::span<char>) {
			                                 return std::string_view{"0"};
		                                 }};
		static constexpr Node on_device = range_item(454, 0, kDevice, &kSpec);
		static constexpr Node on_sound = range_item(455, 0, kSound, &kSpec);
		static constexpr const Node* kids[] = {&on_device, &on_sound};
		static constexpr Node root = submenu(453, kids);
		static constexpr const Node* roots[] = {&root};

		std::vector<uint16_t> flagged;
		for (const Issue& i : validate_tree(roots, {})) {
			if (i.kind == Issue::Kind::RangeWithoutSpec) {
				flagged.push_back(i.node->name);
			}
		}
		expect(flagged).to_equal(std::vector<uint16_t>{455});
	});

	it("flags an after-write on a node that is no DEVICE row's, and passes one on a DEVICE row", _ {
		// `Flag::AfterWriteOffDevice`: only a device write runs a node's after-write.
		set_shortcut_table_for_test({}, 0, 0);
		static constexpr deluge::ui::scenes::bind::Target kSound{.kind = deluge::ui::scenes::bind::Target::Kind::SOUND,
		                                                      .path = "polyphonic"};
		static constexpr deluge::ui::scenes::bind::Target kDevice{.kind = deluge::ui::scenes::bind::Target::Kind::MODEL,
		                                                       .path = "pads/playCursor",
		                                                       .group = deluge::project::SettingGroupId::DEVICE};
		static constexpr Node on_device = with_after_write(setting_item(450, Kind::Enum, kDevice), discard);
		static constexpr Node on_sound = with_after_write(setting_item(451, Kind::Enum, kSound), discard);
		static constexpr Node hooked = with_after_write(toggle_item(452, zero, discard), discard);
		static constexpr const Node* kids[] = {&on_device, &on_sound, &hooked};
		static constexpr Node root = submenu(449, kids);
		static constexpr const Node* roots[] = {&root};

		std::vector<uint16_t> flagged;
		for (const Issue& i : validate_tree(roots, {})) {
			if (i.kind == Issue::Kind::FlagMismatch && i.detail == static_cast<uint16_t>(Flag::AfterWriteOffDevice)) {
				flagged.push_back(i.node->name);
			}
		}
		expect(flagged).to_equal(std::vector<uint16_t>{451, 452});
	});
});
// clang-format on

/// Two `describe` groups in one TU, so `DELUGE_SPEC` (one spec per entry point) is spelled out here
/// with `add_specs` instead -- as `tests/integration/ui/scenes/menu_scene_spec.cpp` does.
int validate_spec(int argc, char** const argv) {
	return CppSpec::parse(argc, argv).add_specs(scenes_validate, scenes_validate_rules).exec().is_success()
	           ? EXIT_SUCCESS
	           : EXIT_FAILURE;
}
