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

} // namespace deluge::scenes::menu::nodes
