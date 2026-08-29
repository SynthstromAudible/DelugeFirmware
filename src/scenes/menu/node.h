#pragma once

/// @brief The declarative menu data model (spec E §4, amended by
///        `.superpowers/sdd/2026-08-27-ui-scenes-menus-1/task-2-brief.md`): `Node`, `Binding`,
///        `ParamRef`, and the `constexpr` builders that assemble a menu tree as plain data.
///
/// @note **This header is model-free and host-testable.** Its only model include is
///       `modulation/params/param.h`, for `params::Kind` -- pulling in `definitions_cxx.hpp` is
///       accepted collateral (see the brief). Nothing here reaches into `gui/`: l10n string ids
///       are carried as plain `uint16_t` rather than `l10n::String`, so a caller resolves them
///       through whatever string table it likes. `MenuContext` and `MenuState` are forward
///       declared only -- their definitions belong to later tasks (`context.h`, the interpreter's
///       own state) and are never needed to lay out a `Node` tree as data.
///
/// @note `OledFrame`, `UiEvent`, `UiContext` and `Result` are likewise forward declared rather than
///       pulled in via `ui/oled/frame.h` / `shell/scene.h`: `CustomItem::render`/`handle` only ever
///       *declare* those types here (as references, or as a return type never invoked from this
///       header), which never requires a complete type, so this header stays free of the shell's
///       and the oled toolkit's own dependency closures.
#include "modulation/params/param.h"
#include "ui/oled/meters.h"
#include "ui/oled/values.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace deluge::ui::oled {
struct OledFrame;
} // namespace deluge::ui::oled

namespace deluge::ui::shell {
struct UiEvent;
struct UiContext;
struct Result;
} // namespace deluge::ui::shell

namespace deluge::scenes::menu {

/// @brief What a `Node` is: which builder made it, and (with `Hint`) which widget renders it.
///
/// A `Kind` exists only if at least 3 items in E1's own tables use it (spec E §4's rule, checked
/// in Task 8); `Bipolar`/`Range`/`MultiRange`/`Colour` are declared now for the tree shape but
/// their interpreters land in E2.
enum class Kind : uint8_t {
	Submenu,
	Toggle,
	Enum,
	Int,
	Decimal,
	Bipolar,
	Range,
	MultiRange,
	Colour,
	SyncLevel,
	Action,
	Custom,
};

/// @brief How a `Submenu`'s children lay out: a vertical list, or a horizontal strip of columns.
enum class Layout : uint8_t { Vertical, Horizontal };

/// @brief The horizontal-menu meter style a node's value draws with -- an alias, not a new type
///        (`ui/oled/meters.h:41`). `Enum`/`Toggle` are toolkit-only slot styles `columns` uses to
///        pick a slot renderer; every other enumerator maps 1:1 onto legacy's `Number::RenderingStyle`.
using Hint = deluge::ui::oled::Hint;

/// @brief An edited value's bounds and coarse step -- an alias, not a new type
///        (`ui/oled/values.h:18`).
using IntRange = deluge::ui::oled::Range;

/// @brief Which arithmetic converts a `ParamRef`'s stored `int32_t` to and from the menu units the
///        user sees, i.e. which of legacy's `readCurrentValue`/`getFinalValue` pairs a param item
///        used (`gui/menu_item/value_scaling.cpp`).
///
/// Legacy encoded this in the *class* of the menu item; as plain data it is one enumerator on the
/// `Binding`. Only a `ParamRef` binding is scaled at all -- a `get`/`set` hook owns its own units --
/// so only `param()` takes one.
///
///   * `Standard` -- INT32 range <-> 0..50. `patched_param::Integer` (`patched_param/integer.cpp:33,79`),
///     `menu_item::UnpatchedParam` (`unpatched_param.cpp:36,84`); the overwhelming majority.
///   * `HalfPrecision` -- 0..INT32_MAX <-> 0..50, for params that "aren't set up for negative
///     inputs" (`value_scaling.cpp:35-36`): `osc::PulseWidth` (`osc/pulse_width.h:32,34-37`) and
///     `audio_compressor::CompParam` (`audio_compressor/compressor_params.h:29-33`).
///   * `Pan` -- INT32 range <-> -25..25, and a -25..25 range rather than 0..50:
///     `patched_param::Pan` (`patched_param/pan.cpp:28-34`, `pan.h:26-27`) and
///     `unpatched_param::Pan` (`unpatched_param/pan.cpp:63-70`, `pan.h:28-29`).
enum class Scaling : uint8_t { Standard, HalfPrecision, Pan };

/// @brief Which model parameter a `Binding` reads and writes through `ModelStack`, or an invalid
///        (unset) reference for a `Binding` driven entirely by `get`/`set` instead.
struct ParamRef {
	deluge::modulation::params::Kind kind = deluge::modulation::params::Kind::NONE;
	uint8_t index = 255;
	constexpr bool valid() const { return kind != deluge::modulation::params::Kind::NONE; }
};

/// @brief Forward declared only -- see the file-level note. Defined in `context.h` (a later task).
struct MenuContext;

using GetFn = int32_t (*)(const MenuContext&);
using SetFn = void (*)(const MenuContext&, int32_t);
using RangeFn = IntRange (*)(const MenuContext&);
using RelevantFn = bool (*)(const MenuContext&);
using DynamicNamesFn = size_t (*)(const MenuContext&, std::span<std::string_view> out);

struct Node;
using DynamicChildrenFn = size_t (*)(const MenuContext&, std::span<const Node*> out);

/// @brief The largest window `dynamic_children`/`dynamic_names` may fill on one call, and the
///        largest node-id the registry (`node_id.h`) will assign.
inline constexpr size_t kMaxDynamicChildren = 64;
inline constexpr size_t kMaxDynamicNames = 64;
inline constexpr size_t kMaxNodes = 1024;

/// @brief How a node reads, writes, and (for `Enum`) names its value.
///
/// Normally exactly one of `param` or `get`+`set` is set; `names`/`dynamic_names` only matter for
/// `Kind::Enum`. `names` holds l10n string ids (`uint16_t`), not `l10n::String`, so this header
/// stays free of `gui/l10n`.
///
/// @note **Precedence when both are set: `get`/`set` wins, and `param` is then a *learn* target
///       only.** Legacy has four items that are simultaneously a `Decimal` with their own
///       read/write and a `PatchedParam` with a real parameter id -- `menu_item::Transpose`
///       (`gui/menu_item/transpose.h:26`), which the two oscillator and two FM-modulator transpose
///       menus derive from. Their value does not live in the param (it is
///       `sampleHolder.transpose`/`cents`, or `Sound::modulatorTranspose`/`Cents`), so
///       `bindings::read`/`write` must take the hooks; but `getParamIndex()` (`transpose.h:37`) and
///       `getLearningThing()` -> `setToHaveParamOnly(getP())` (`source/transpose.h:28-32`) are real,
///       so MIDI/CC learn must still bind to `LOCAL_OSC_A/B_PITCH_ADJUST` and
///       `LOCAL_MODULATOR_0/1_PITCH_ADJUST`. Declaring both on the `Binding` is how that survives:
///       `bindings::read`/`write` test `get`/`set` first, `bindings::learn_target` reads `param`,
///       and `bindings::range` already prefers `fixed_range` over the param's default.
///
/// @note **`range` vs `fixed_range`.** Spec E §4 gives `range` as a function of `MenuContext` --
///       genuinely dynamic bounds (e.g. a rate whose ceiling depends on sync mode) -- documented
///       there as "optional; default from ParamRef" for a bound item. That leaves no field for the
///       common case the brief's own `int_item`/`decimal_item` builders take a *literal* `IntRange`
///       argument: a plain function pointer cannot close over an arbitrary runtime value, so a
///       literal range cannot be smuggled through `range` without one hand-written trampoline
///       function per distinct range (impractical, and not what those builders' signatures ask
///       for). `fixed_range` is the addition that resolves this: it holds the literal bounds those
///       two builders are given, engaged only when `range == nullptr`. Task 6+'s interpreter reads
///       in this order: `range(ctx)` if set, else `fixed_range` if engaged, else derive from
///       `param` (model code, out of reach here).
struct Binding {
	ParamRef param{};
	GetFn get = nullptr;
	SetFn set = nullptr;
	RangeFn range = nullptr;
	std::optional<IntRange> fixed_range{};
	std::span<const uint16_t> names{};
	DynamicNamesFn dynamic_names = nullptr;
	deluge::ui::oled::ValueStyle style{};
	uint8_t decimal_places = 0;

	/// @brief Which value scaling `param` is read and written through (`Scaling` above). Meaningful
	///        only when `param.valid()`; ignored for a `get`/`set` binding.
	Scaling scaling = Scaling::Standard;

	/// @brief Does committing this value oblige the audio engine to recompute its reverb
	///        parameters before the next render?
	///
	/// The port of `unpatched_param::UpdatingReverbParams::writeCurrentValue`
	/// (`gui/menu_item/unpatched_param/updating_reverb_params.h:26-29`): an ordinary
	/// `UnpatchedParam` write, then `AudioEngine::mustUpdateReverbParamsBeforeNextRender = true`.
	/// It is a flag on the `Binding` rather than a `set` hook because a `set` hook would *replace*
	/// the param write (`bindings::write` prefers hooks), and because two legacy items share the
	/// behaviour and neither can express it any other way: `sidechain::Shape`
	/// (`gui/menu_item/sidechain/shape.h:6`, this batch's `sidechain_shape`) and
	/// `sidechain::GlobalVolume` (`sidechain/volume_global.h:6`, the song and global-FX roots task
	/// 8 declares). Meaningful only for a `ParamRef` binding -- a `get`/`set` item that needs the
	/// flag sets it in its own hook, as the sidechain attack/release/sync ones do.
	bool updates_reverb_params = false;
};

/// @brief Forward declared only -- see the file-level note. Defined alongside the interpreter's own
///        per-instance state (a later task).
struct MenuState;

/// @brief The escape hatch (spec E §4: at most 10 across the whole tree) for a bespoke item that
///        doesn't fit any `Kind`: the multi-range editor, patch cables, the file selector, and the
///        handful of others spec E §4 names.
struct CustomItem {
	virtual ~CustomItem() = default;
	virtual void render(deluge::ui::oled::OledFrame&, const MenuContext&, MenuState&) const = 0;
	virtual deluge::ui::shell::Result handle(const deluge::ui::shell::UiEvent&, const deluge::ui::shell::UiContext&,
	                                         const MenuContext&, MenuState&) const = 0;

	/// @brief Is this a stand-in for an item E2 still owes, rather than the real thing?
	///
	/// E1's tables have to name every legacy bespoke item so the tree's *shape* is complete, but the
	/// implementations of the file selector, the audio recorder, the loop points, the name editors,
	/// the patch-cable pages and the multi-range picker all belong to E2. Those nodes point at a
	/// shared placeholder (`nodes::kPending`) that draws the item's title and consumes nothing.
	///
	/// A null `custom` would be worse -- nothing would render, and `validate.h`'s `NullCustom` rule
	/// exists precisely to catch that -- but a placeholder is still not a finished item, so
	/// `validate_tree` reports one (`Issue::Kind::PlaceholderCustom`) unless the caller opts in via
	/// `ValidateOptions::allow_placeholder_custom`. That keeps "the tree is complete" checkable by
	/// default and lets an E1 spec say, once and visibly, that it is not yet.
	virtual bool is_placeholder() const { return false; }
};

/// @brief One menu item or submenu, as plain data.
///
/// `children` is only ever populated by `submenu()` -- a `dynamic_children`/`dynamic_names` node
/// has no *static* children, so `assign_node_ids` (which walks `children` only) never reaches, and
/// can never address by id, whatever those hooks produce at render time (spec E §4's dynamic
/// submenus are resolved then, not ahead of time).
struct Node {
	uint16_t name = 0;
	uint16_t title = 0;
	Kind kind = Kind::Submenu;
	Hint hint = Hint::None;
	Layout layout = Layout::Vertical;
	Binding bind{};
	std::span<const Node* const> children{};
	RelevantFn relevant = nullptr;
	DynamicChildrenFn dynamic_children = nullptr;
	const CustomItem* custom = nullptr;
	uint8_t slots = 1;

	/// @brief Does this item edit something that belongs to one `MultiRange` rather than to the
	///        `Sound` as a whole? The port of `MenuItem::isRangeDependent()`
	///        (`gui/menu_item/menu_item.h:202`, "only relevant to some note ranges").
	///
	/// This, not "has a `bind.param`", is what makes spec E §4.1's permission rule fire
	/// (`menu::permission_for`). The distinction matters: an ordinary patched param such as LPF
	/// frequency lives on the `Sound` and is editable on a multisampled synth without picking a
	/// range first, while a range-dependent item's value lives in the range itself
	/// (`sample/transpose.h:139-165` writes `sampleHolder.transpose` / `source.transpose`) and has
	/// nowhere to go until one is chosen.
	///
	/// The legacy items that override `isRangeDependent()` to true, and so are the ones that set
	/// this when Tasks 6-8 declare them: `sample/transpose.h:137`, `sample/loop_point.h:31`, and
	/// `multi_range.h:32` (the picker itself). `file_selector` and `osc/audio_recorder` reach the
	/// same rule through `checkPermissionToBeginSessionForRangeSpecificParam` without the override,
	/// and set this too.
	bool range_dependent = false;
};

/// @brief `n` with `range_dependent` set -- a `constexpr` post-hoc modifier rather than a trailing
///        argument on all nine builders.
///
/// Only a handful of nodes in the whole tree are range-dependent (the five named on
/// `Node::range_dependent`), so paying for the flag in every builder signature would be the wrong
/// trade: `with_range_dependent(param(...))` reads as the exception it is, and no existing call site or
/// `static_assert` changes.
constexpr Node with_range_dependent(Node n) {
	n.range_dependent = true;
	return n;
}

// ---------------------------------------------------------------------------------------------
// Builders. All `constexpr`; `title` defaults to `name` (0 means "use name").

/// @param scaling Which value scaling this param uses (`Scaling`). The default matches legacy's
///                `patched_param::Integer` / `menu_item::UnpatchedParam`; the two exceptions
///                (`Scaling::HalfPrecision` for pulse width and the compressor params,
///                `Scaling::Pan` for the two pan params) name themselves at the call site.
constexpr Node param(uint16_t name, ParamRef p, Hint h = Hint::Knob, RelevantFn rel = nullptr, uint16_t title = 0,
                     Scaling scaling = Scaling::Standard) {
	Node n{};
	n.name = name;
	n.title = title != 0 ? title : name;
	n.kind = Kind::Int;
	n.hint = h;
	n.bind.param = p;
	n.bind.scaling = scaling;
	n.relevant = rel;
	return n;
}

constexpr Node int_item(uint16_t name, GetFn get, SetFn set, IntRange r, Hint h = Hint::None,
                        RelevantFn rel = nullptr) {
	Node n{};
	n.name = name;
	n.title = name;
	n.kind = Kind::Int;
	n.hint = h;
	n.bind.get = get;
	n.bind.set = set;
	n.bind.fixed_range = r;
	n.relevant = rel;
	return n;
}

constexpr Node decimal_item(uint16_t name, GetFn get, SetFn set, IntRange r, uint8_t places, RelevantFn rel = nullptr) {
	Node n{};
	n.name = name;
	n.title = name;
	n.kind = Kind::Decimal;
	n.bind.get = get;
	n.bind.set = set;
	n.bind.fixed_range = r;
	n.bind.decimal_places = places;
	n.relevant = rel;
	return n;
}

constexpr Node toggle_item(uint16_t name, GetFn get, SetFn set, RelevantFn rel = nullptr) {
	Node n{};
	n.name = name;
	n.title = name;
	n.kind = Kind::Toggle;
	n.bind.get = get;
	n.bind.set = set;
	n.relevant = rel;
	return n;
}

constexpr Node enumeration(uint16_t name, std::span<const uint16_t> names, GetFn get, SetFn set,
                           RelevantFn rel = nullptr) {
	Node n{};
	n.name = name;
	n.title = name;
	n.kind = Kind::Enum;
	n.bind.names = names;
	n.bind.get = get;
	n.bind.set = set;
	n.relevant = rel;
	return n;
}

constexpr Node dynamic_enumeration(uint16_t name, DynamicNamesFn names, GetFn get, SetFn set,
                                   RelevantFn rel = nullptr) {
	Node n{};
	n.name = name;
	n.title = name;
	n.kind = Kind::Enum;
	n.bind.dynamic_names = names;
	n.bind.get = get;
	n.bind.set = set;
	n.relevant = rel;
	return n;
}

constexpr Node submenu(uint16_t name, std::span<const Node* const> children, Layout layout = Layout::Vertical,
                       RelevantFn rel = nullptr, uint16_t title = 0) {
	Node n{};
	n.name = name;
	n.title = title != 0 ? title : name;
	n.kind = Kind::Submenu;
	n.layout = layout;
	n.children = children;
	n.relevant = rel;
	return n;
}

constexpr Node dynamic_submenu(uint16_t name, DynamicChildrenFn children, RelevantFn rel = nullptr) {
	Node n{};
	n.name = name;
	n.title = name;
	n.kind = Kind::Submenu;
	n.dynamic_children = children;
	n.relevant = rel;
	return n;
}

/// @brief A `Kind::SyncLevel` leaf -- legacy's `menu_item::SyncLevel` (`gui/menu_item/sync_level.h:27`).
///
/// The value is legacy's own menu-option index, `SyncLevel::syncTypeAndLevelToMenuOption`
/// (`gui/menu_item/sync_level.cpp:79-81`), so a hook pair is always what reads and writes it -- no
/// `ParamRef` spelling exists for a `{syncType, syncLevel}` pair.
///
/// @param r The option range. Normally `{0, NUM_SYNC_VALUES - 1, 1}` (`sync_level.h:31` over
///          `model/sync.h:37`), but `sidechain::Sync` overrides `size()` to 10
///          (`gui/menu_item/sidechain/sync.h:24`), so the bounds are an argument rather than baked
///          in -- and baking them in would drag `model/sync.h` into this deliberately model-light
///          header.
constexpr Node sync_level_item(uint16_t name, uint16_t title, GetFn get, SetFn set, IntRange r,
                               RelevantFn rel = nullptr) {
	Node n{};
	n.name = name;
	n.title = title != 0 ? title : name;
	n.kind = Kind::SyncLevel;
	n.bind.get = get;
	n.bind.set = set;
	n.bind.fixed_range = r;
	n.relevant = rel;
	return n;
}

constexpr Node action(uint16_t name, SetFn run, RelevantFn rel = nullptr) {
	Node n{};
	n.name = name;
	n.title = name;
	n.kind = Kind::Action;
	n.bind.set = run;
	n.relevant = rel;
	return n;
}

constexpr Node custom(uint16_t name, const CustomItem* item, RelevantFn rel = nullptr) {
	Node n{};
	n.name = name;
	n.title = name;
	n.kind = Kind::Custom;
	n.custom = item;
	n.relevant = rel;
	return n;
}

} // namespace deluge::scenes::menu
