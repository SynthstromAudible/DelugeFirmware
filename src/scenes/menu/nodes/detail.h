#pragma once

/// @brief Internal helpers shared by the `nodes/*.cpp` tables. Not part of any public interface.
///
/// Three `constexpr` post-hoc modifiers and one string-id cast, in one place rather than copied
/// into six translation units. They exist because `node.h`'s builders take the arguments the
/// common case needs and nothing more (Task 2's deliberate choice, see `with_range_dependent`);
/// the handful of legacy items that also need a distinct title on a non-`param` kind, a meter hint
/// on a `Decimal`, or more than one horizontal slot say so here, at the call site, in the shape
/// `with_range_dependent` already established.

#include "scenes/menu/node.h"

#include "gui/l10n/strings.h" // the only `gui/` the scenes layer may reach (see CMakeLists.txt)

#include <cstdint>

namespace deluge::scenes::menu::nodes {

/// @brief An `l10n::String` as the plain `uint16_t` a `Node` carries (`node.h`: string ids cross
///        as numbers so the data model stays free of `gui/l10n`).
constexpr uint16_t sid(deluge::l10n::String s) {
	return static_cast<uint16_t>(s);
}

/// @brief @p n with a title that differs from its name.
///
/// `param()` and `submenu()` take a trailing `title`; `enumeration()`, `toggle_item()`,
/// `int_item()`, `decimal_item()` and `custom()` do not, and several legacy items in these tables
/// carry a distinct `title` anyway (`lpfModeMenu`'s `STRING_FOR_LPF_MODE`,
/// `numUnisonMenu`'s `STRING_FOR_UNISON_NUMBER_MENU_TITLE`, ...).
constexpr Node with_title(Node n, uint16_t title) {
	n.title = title;
	return n;
}

/// @brief @p n with an explicit horizontal-slot meter.
///
/// The port of `Number::getRenderingStyle()` for a kind whose builder takes no `Hint`
/// (`decimal_item`, `toggle_item`, `enumeration`). Legacy dispatches on that style inside
/// `Number::renderInHorizontalMenu` whatever the item's kind (`gui/menu_item/number.h:48`), so the
/// fact belongs on the node even where this generation's interpreter does not yet read it.
constexpr Node with_hint(Node n, Hint hint) {
	n.hint = hint;
	return n;
}

/// @brief @p n occupying @p slots of a horizontal page's four -- `MenuItem::getOccupiedSlots()`
///        (`gui/menu_item/menu_item.h:304`).
constexpr Node with_slots(Node n, uint8_t slots) {
	n.slots = slots;
	return n;
}

/// @brief @p n with literal bounds, for a `ParamRef` node whose legacy class overrode
///        `getMinValue`/`getMaxValue` away from the param default.
///
/// `param()` takes no range: the overwhelming majority of param items keep `Param::getMaxValue`'s
/// `kMinMenuValue`..`kMaxMenuValue` (`gui/menu_item/param.h:34-35`), which `bindings::range` already
/// supplies. The handful that do not say so here. `bindings::range` prefers `fixed_range` over the
/// param default (`node.h`'s `range` vs `fixed_range` note), so this is all such a node needs.
constexpr Node with_range(Node n, IntRange r) {
	n.bind.fixed_range = r;
	return n;
}

/// @brief @p n with an explicit `Binding::style` -- the unit suffix, or "OFF" in place of 0.
///
/// `int_value`/`decimal_value` take a `ValueStyle` (`ui/oled/values.h:23-28`) and
/// `MenuScene::render_leaf_int` hands it `node.bind.style`, but no builder has an argument for one.
/// The port of legacy's presentation *subclasses* of `Integer`: `IntegerWithOff` draws 0 as "OFF"
/// (`gui/menu_item/integer.cpp:41-50`), which is `off_at_zero`.
///
/// @note **Three shipped nodes still owe this, and they are a family sweep rather than a one-liner**
///       (E3 task 8 review, Important 2). Legacy has five `IntegerWithOff` subclasses; E3 task 8
///       ports two of them *with* `off_at_zero` (`nodes::device_default_velocity_to_level` from
///       `midi/default_velocity_to_level.h:24`, and `MpeZoneChannelsItem`, which draws its own).
///       The other three were declared before this modifier existed and draw "0" where legacy draws
///       "OFF":
///
///         * `nodes::record_count_in` -- `record::CountIn` (`gui/menu_item/record/countin.h:22`).
///         * `nodes::saturation` -- `fx::Clipping` (`gui/menu_item/fx/clipping.h:29`).
///         * `nodes::voice_count` -- `voice::VoiceCount` (`gui/menu_item/voice/polyphony.h:31`), and
///           this one is **not** just `off_at_zero`: it also overrides `getValueString` to spell OFF
///           in the horizontal menu (`:79-86`) and `renderInHorizontalMenu` to draw
///           `OLED::infinityIcon` instead of a number at 0 (`:88-94`). `ValueStyle` has no icon
///           field, so that half needs the toolkit, not this modifier.
///
///       Adding the flag moves a golden for each, so it is one deliberate sweep with its own
///       re-record, not three incidental edits. Recorded on the E3 carry list
///       (`src/scenes/README.md`).
constexpr Node with_value_style(Node n, deluge::ui::oled::ValueStyle style) {
	n.bind.style = style;
	return n;
}

/// @brief @p n with a relevance predicate, for a builder whose signature has no room for one.
///
/// `sync_level_item()` and the `with_*` chain cover most cases; this exists for the few nodes built
/// by a helper that fixes the other arguments (e.g. the two bend-range leaves, which share a range
/// and differ only in hook and relevance).
constexpr Node with_relevant(Node n, RelevantFn rel) {
	n.relevant = rel;
	return n;
}

/// @brief @p n with `Binding::updates_reverb_params` set -- the port of
///        `unpatched_param::UpdatingReverbParams` (`gui/menu_item/unpatched_param/updating_reverb_params.h:26-29`).
///
/// A post-hoc modifier rather than an argument on `param()` for the same reason
/// `with_range_dependent` is one: two nodes in the whole tree need it.
constexpr Node with_reverb_update(Node n) {
	n.bind.updates_reverb_params = true;
	return n;
}

/// @brief @p n with a **learn-only** `ParamRef` beside its `get`/`set` hooks.
///
/// For legacy's four `menu_item::Transpose` derivatives, which read and write outside the param set
/// but still expose `getParamIndex()` / `getLearningThing()` (`gui/menu_item/transpose.h:37`,
/// `source/transpose.h:28-32`). `node.h`'s `Binding` precedence note is what makes this safe:
/// `bindings::read`/`write` prefer `get`/`set`, so the reference is only ever read by
/// `bindings::learn_target`.
constexpr Node with_learn_param(Node n, ParamRef p) {
	n.bind.param = p;
	return n;
}

} // namespace deluge::scenes::menu::nodes
