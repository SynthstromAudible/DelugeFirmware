#pragma once

/// @brief Internal helpers shared by the `nodes/*.cpp` tables. Not part of any public interface.
///
/// The string-id cast (`sid`), the target of a node on a DEVICE row (`device_row`, `device_output_row`), the words a
/// toggle lists for its two states, and the `constexpr` post-hoc modifiers, in one place rather than copied into each
/// table. The modifiers exist because `node.h`'s builders take the arguments the common case needs and nothing more (a
/// deliberate choice, see `with_range_dependent`); the legacy items that also need a distinct title on a non-`param`
/// kind, a meter hint on a `Decimal`, or more than one horizontal slot say so here, at the call site, in the shape
/// `with_range_dependent` already established.

#include "scenes/bind/target.h"
#include "scenes/menu/node.h"

#include "deluge-project/device_settings.h"
#include "deluge-project/setting_group.h"
#include "gui/l10n/strings.h" // the only `gui/` the scenes layer may reach (see CMakeLists.txt)

#include <cstdint>
#include <string_view>

namespace deluge::scenes::menu::nodes {

/// @brief An `l10n::String` as the plain `uint16_t` a `Node` carries (`node.h`: string ids cross
///        as numbers so the data model stays free of `gui/l10n`).
constexpr uint16_t sid(deluge::l10n::String s) {
	return static_cast<uint16_t>(s);
}

/// @brief What a node on DEVICE row @p path edits: the row, through the device port.
/// @param path A DEVICE row's path.
/// @return The node's target.
constexpr bind::Target device_row(project::DevicePath path) {
	return {.kind = bind::Target::Kind::MODEL, .path = path, .group = project::SettingGroupId::DEVICE};
}

/// @brief What a node on one of a set of per-output DEVICE rows edits: the row of the output the menu's selector chose
///        (`MenuContext::source_index`).
/// @param pattern The rows' pattern, its `*` standing for the output's index (`project::device_path::kCvTranspose`).
/// @return The node's target.
constexpr bind::Target device_output_row(std::string_view pattern) {
	return {.kind = bind::Target::Kind::MODEL,
	        .path = pattern,
	        .source_indexed = true,
	        .group = project::SettingGroupId::DEVICE};
}

/// @brief The words a toggle's own page lists for its two states, off then on (`MenuView::render_leaf_toggle`):
///        main's `Toggle::getNameFor`, DISABLED and ENABLED.
///
/// The menu dump spells every toggle with them, and a toggle drawn as a custom checkbox row names them as its values.
inline constexpr uint16_t kToggleWords[] = {sid(deluge::l10n::String::STRING_FOR_DISABLED),
                                            sid(deluge::l10n::String::STRING_FOR_ENABLED)};

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
/// `Number::renderInHorizontalMenu` whatever the item's kind (`origin/main:src/deluge/gui/menu_item/number.h`), so the
/// fact belongs on the node even where this generation's interpreter does not yet read it.
constexpr Node with_hint(Node n, Hint hint) {
	n.hint = hint;
	return n;
}

/// @brief @p n occupying @p slots of a horizontal page's four -- `MenuItem::getOccupiedSlots()`
///        (`origin/main:src/deluge/gui/menu_item/menu_item.h`).
/// @param n     The node.
/// @param slots How many of the page's four slots it occupies.
/// @return @p n with its slot count set.
constexpr Node with_slots(Node n, uint8_t slots) {
	n.slots = slots;
	return n;
}

/// @brief @p n with literal bounds, for a `ParamRef` node whose legacy class overrode
///        `getMinValue`/`getMaxValue` away from the param default.
///
/// `param()` takes no range: the overwhelming majority of param items keep `Param::getMaxValue`'s
/// `kMinMenuValue`..`kMaxMenuValue` (`Param::getMinValue` and `getMaxValue` in
/// `origin/main:src/deluge/gui/menu_item/param.h`), which `bindings::range` already supplies. The handful that do not
/// say so here. `bindings::range` prefers `fixed_range` over the param default (`node.h`'s `range` vs `fixed_range`
/// note), so this is all such a node needs.
constexpr Node with_range(Node n, IntRange r) {
	n.bind.fixed_range = r;
	return n;
}

/// @brief @p n with an explicit `Binding::style` -- the unit suffix, or "OFF" in place of 0.
///
/// `int_value`/`decimal_value` take a `ValueStyle` (`ui/oled/values.h`) and
/// `MenuView::render_leaf_int` hands it `node.bind.style`, but no builder has an argument for one.
/// The port of legacy's presentation *subclasses* of `Integer`: `IntegerWithOff` draws 0 as "OFF"
/// (`IntegerWithOff::drawPixelsForOled` in `origin/main:src/deluge/gui/menu_item/integer.cpp`), which is `off_at_zero`.
///
/// @note Legacy's five `IntegerWithOff` subclasses all carry it: `nodes::device_default_velocity_to_level`,
///       `MpeZoneChannelsItem` (which draws its own), `nodes::record_count_in`, `nodes::saturation` and
///       `nodes::voice_count`. `ValueStyle` is the leaf's drawing only: `voice::VoiceCount`'s infinity at 0 in the
///       horizontal menu (`VoiceCount::renderInHorizontalMenu` in
///       `origin/main:src/deluge/gui/menu_item/voice/polyphony.h`) is its `Binding::column_art`.
/// @param n     The node.
/// @param style How the leaf draws its value.
/// @return @p n with its value style set.
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

/// @brief @p n with a **learn-only** `ParamRef` beside its `get`/`set` hooks.
///
/// For legacy's four `menu_item::Transpose` derivatives, which read and write outside the param set but still expose
/// `getParamIndex()` / `getLearningThing()` (`origin/main:src/deluge/gui/menu_item/transpose.h`,
/// `origin/main:src/deluge/gui/menu_item/source/transpose.h`). `node.h`'s `Binding` precedence note is what makes this
/// safe: `bindings::read`/`write` prefer `get`/`set`, so the reference is only ever read by `bindings::learn_target`.
constexpr Node with_learn_param(Node n, ParamRef p) {
	n.bind.param = p;
	return n;
}

/// @brief @p n, whose target writes also repaint the pad grid (`Binding::repaints_grid`).
constexpr Node with_grid_repaint(Node n) {
	n.bind.repaints_grid = true;
	return n;
}

} // namespace deluge::scenes::menu::nodes
