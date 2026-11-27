#include "modulation/params/param.h"
#include "ui/scenes/menu/node.h"
#include "ui/shell/scene.h" // for the CustomItem concrete override in this file only -- node.h never
                            // includes this itself, see its file-level note.
#include "ui/widgets/oled/meters.h"
#include "ui/widgets/oled/values.h"

#include "cppspec.hpp"
#include "support/matchers/address_of.h"

#include <type_traits>

#include "support/spec_main.h"

using namespace deluge::ui::scenes::menu;
namespace params = deluge::modulation::params;

// clang-format off

// `param()` -- a ParamRef-bound item builder: `title` defaults to `name`, `kind` is always
// `Kind::Int` (the generic ParamRef-bound number editor), and `hint` is carried through as-is.
constexpr Node node_spec_a = param(1, sound_param(deluge::plugin::ParamId{5}), Hint::Lpf);
static_assert(node_spec_a.kind == Kind::Int);
static_assert(param_id_of(node_spec_a.bind.param).value == 5);
static_assert(is_patched(node_spec_a.bind.param));
static_assert(node_spec_a.title == 1);
static_assert(node_spec_a.hint == deluge::ui::widgets::oled::Hint::Lpf);

// `submenu()` -- children span through as-is, layout carried through.
constexpr const Node* node_spec_kids[] = {&node_spec_a};
constexpr Node node_spec_s = submenu(2, node_spec_kids, Layout::Horizontal);
static_assert(node_spec_s.children.size() == 1);
static_assert(node_spec_s.layout == Layout::Horizontal);
static_assert(node_spec_s.kind == Kind::Submenu);
static_assert(node_spec_s.children[0] == &node_spec_a);

// `IntRange` is an alias of `ui::widgets::oled::Range`, not a new type.
static_assert(std::is_same_v<IntRange, deluge::ui::widgets::oled::Range>);

// `Binding::scaling`: `Scaling::Standard` unless the call site
// names one of the two legacy exceptions. Only `param()` takes it -- a `get`/`set` binding owns its
// own units, so nothing scales it.
static_assert(node_spec_a.bind.scaling == Scaling::Standard);
constexpr Node node_spec_half = param(27, sound_param(deluge::plugin::ParamId{20}), Hint::Knob, nullptr, /*title=*/0,
                                      Scaling::HalfPrecision);
static_assert(node_spec_half.bind.scaling == Scaling::HalfPrecision);
static_assert(node_spec_half.name == 27 && node_spec_half.title == 27 && node_spec_half.kind == Kind::Int);

// A title override flows through unchanged; where the node shows is its target's, never a hook.
constexpr Node node_spec_titled = param(3, sound_param(deluge::instruments::classic::param_id({deluge::instruments::classic::RuntimeSet::UNPATCHED, static_cast<uint8_t>(7)})), Hint::Knob, nullptr, /*title=*/9);
static_assert(node_spec_titled.title == 9);
static_assert(node_spec_titled.relevant == nullptr && node_spec_titled.bind.target.valid());
// A sound's param names its id in a sound's table, a global effectable's in a chain's.
static_assert(node_spec_titled.bind.target.param_table == deluge::ui::scenes::bind::ParamTable::SOUND);
static_assert(param(4, chain_param(deluge::plugin::ParamId{deluge::host::chains::kParamBlock.first.value + static_cast<uint32_t>(7)})).bind.target.param_table
              == deluge::ui::scenes::bind::ParamTable::CHAIN);

// `int_item()`/`decimal_item()` carry a literal range through `bind.fixed_range` (see node.h's
// note on why `range`, the dynamic-range function pointer, cannot hold a literal).
int32_t node_spec_get(const MenuContext&) { return 0; }
void node_spec_set(const MenuContext&, int32_t) {}
constexpr Node node_spec_int = int_item(4, node_spec_get, node_spec_set, IntRange{0, 100, 5}, Hint::Slider);
static_assert(node_spec_int.kind == Kind::Int);
static_assert(node_spec_int.hint == deluge::ui::widgets::oled::Hint::Slider);
static_assert(node_spec_int.bind.fixed_range.has_value());
static_assert(node_spec_int.bind.fixed_range->min == 0 && node_spec_int.bind.fixed_range->max == 100);
static_assert(node_spec_int.bind.scaling == Scaling::Standard); // a get/set binding is never scaled

constexpr Node node_spec_decimal = decimal_item(5, node_spec_get, node_spec_set, IntRange{-10, 10}, 2);
static_assert(node_spec_decimal.kind == Kind::Decimal);
static_assert(node_spec_decimal.bind.decimal_places == 2);

// A no-op CustomItem, at namespace scope so `&node_spec_null_item`'s address is usable in a
// constexpr initializer below (address-of a static-storage-duration object is a constant
// expression regardless of the object's own type or how it was constructed -- this does not
// require CustomItem itself to be a literal type).
struct NodeSpecNullItem : CustomItem {
	void render(deluge::ui::widgets::oled::OledFrame&, const MenuContext&, MenuState&) const override {}
	deluge::ui::shell::Result handle(const deluge::ui::shell::UiEvent&, const deluge::ui::shell::UiContext&,
	                                  const MenuContext&, MenuState&) const override {
		return {};
	}
};
const NodeSpecNullItem node_spec_null_item;

// Every builder proven `constexpr`-usable, not just labelled so -- one field
// check each, closing the gap against the runtime-only `it`s below (which still exercise the
// full field set at runtime; these additionally prove compile-time usability).
constexpr Node node_spec_toggle = toggle_item(20, node_spec_get, node_spec_set);
static_assert(node_spec_toggle.kind == Kind::Toggle);

constexpr uint16_t node_spec_enum_names[] = {30, 31, 32};
constexpr Node node_spec_enum = enumeration(21, node_spec_enum_names, node_spec_get, node_spec_set);
static_assert(node_spec_enum.bind.names.size() == 3);

constexpr DynamicNamesFn node_spec_dyn_names_fn = +[](const MenuContext&, std::span<std::string_view>) -> size_t { return 0; };
constexpr Node node_spec_dyn_enum = dynamic_enumeration(22, node_spec_dyn_names_fn, node_spec_get, node_spec_set);
static_assert(node_spec_dyn_enum.bind.dynamic_names == node_spec_dyn_names_fn);

constexpr Node node_spec_custom = custom(24, &node_spec_null_item);
static_assert(node_spec_custom.custom == &node_spec_null_item);

constexpr DynamicChildrenFn node_spec_children_fn = +[](const MenuContext&, std::span<const Node*>) -> size_t { return 0; };
constexpr Node node_spec_dyn_submenu = dynamic_submenu(25, node_spec_children_fn);
static_assert(node_spec_dyn_submenu.dynamic_children == node_spec_children_fn);

// `with_range_dependent()` -- the post-hoc modifier that marks the handful of items whose value
// lives in a MultiRange rather than on the Sound. It changes exactly one
// field and leaves the builder's own output otherwise untouched.
constexpr Node node_spec_range_dep = with_range_dependent(param(26, sound_param(deluge::plugin::ParamId{4}), Hint::Knob));
static_assert(node_spec_range_dep.range_dependent);
static_assert(node_spec_range_dep.name == 26 && node_spec_range_dep.kind == Kind::Int);
static_assert(param_id_of(node_spec_range_dep.bind.param).value == 4);
static_assert(!node_spec_a.range_dependent); // every builder leaves it false by default

namespace {
using deluge::test::matchers::address_of;
} // namespace

describe scenes_node("scenes::menu::Node builders", $ {
	it("toggle_item fills bind.get/set and nothing else", _ {
		Node t = toggle_item(6, node_spec_get, node_spec_set);
		expect(t.kind).to_equal(Kind::Toggle);
		expect(address_of(t.bind.get)).to_equal(address_of(node_spec_get));
		expect(address_of(t.bind.set)).to_equal(address_of(node_spec_set));
		expect(t.bind.names.empty()).to_be_true();
	});
	it("enumeration fills bind.get/set/names", _ {
		static constexpr uint16_t names[] = {10, 11, 12};
		Node e = enumeration(7, names, node_spec_get, node_spec_set);
		expect(e.kind).to_equal(Kind::Enum);
		expect(address_of(e.bind.get)).to_equal(address_of(node_spec_get));
		expect(address_of(e.bind.set)).to_equal(address_of(node_spec_set));
		expect(e.bind.names.size()).to_equal(size_t(3));
		expect(e.bind.names[1]).to_equal(uint16_t(11));
	});
	it("dynamic_enumeration fills bind.dynamic_names instead of bind.names", _ {
		auto names_fn = +[](const MenuContext&, std::span<std::string_view>) -> size_t { return 0; };
		Node e = dynamic_enumeration(8, names_fn, node_spec_get, node_spec_set);
		expect(e.kind).to_equal(Kind::Enum);
		expect(address_of(e.bind.dynamic_names)).to_equal(address_of(names_fn));
		expect(e.bind.names.empty()).to_be_true();
	});
	it("custom fills the custom pointer", _ {
		// (There is no `action()` builder or `Kind::Action`: no table declares one. See `node.h`'s
		// `Kind`.)
		Node c = custom(10, &node_spec_null_item);
		expect(c.kind).to_equal(Kind::Custom);
		expect(c.custom).to_equal(&node_spec_null_item);
	});
	it("dynamic_submenu has no static children", _ {
		auto children_fn = +[](const MenuContext&, std::span<const Node*>) -> size_t { return 0; };
		Node d = dynamic_submenu(11, children_fn);
		expect(d.kind).to_equal(Kind::Submenu);
		expect(address_of(d.dynamic_children)).to_equal(address_of(children_fn));
		expect(d.children.empty()).to_be_true();
	});
});
// clang-format on

DELUGE_SPEC(node_spec, scenes_node)
