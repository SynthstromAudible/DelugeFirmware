#pragma once

/// @file
/// @brief How the menu spells and lays out what it shows: node text, horizontal columns, notifications, row icons.

#include "gfx/oled/icon.h"
#include "scenes/menu/context.h"
#include "scenes/menu/node.h"
#include "shell/profile.h"
#include "ui/oled/columns.h"

#include <cstdint>
#include <span>
#include <string_view>

namespace deluge::scenes::menu {

/// @brief Storage for one node's name or title with its placeholder filled; keep it alive as long
///        as the view `node_text` returns.
struct NodeText {
	/// The filled text. Longer than any title the OLED can show; a longer one is cut short.
	char chars[40];
	/// The digits fallback for an id with no text (`BoardProfile::text_or_digits`).
	deluge::ui::shell::TextScratch digits;
};

/// @brief The storage one horizontal column's drawing points into; it outlives the `columns` call that draws it.
struct ColumnSlot {
	deluge::ui::oled::MeterColumn meter;
	deluge::ui::oled::TextColumn text;
	deluge::ui::oled::ToggleColumn toggle;
	deluge::ui::oled::ArtColumn art;
	deluge::ui::oled::DrawColumn draw;
	char value[16]{};
	char label[16]{};
	NodeText label_scratch;
	/// `option_list_column`'s call: the node, its context and the value.
	struct OptionList {
		const Node* node = nullptr;
		const MenuContext* ctx = nullptr;
		int32_t value = 0;
	} list;
};

/// @brief `Column::render` for a `Node::option_list_column`: `drawHorizontalLine(kScreenTitleSeparatorY, ...)` and
///        `Selection::drawPixelsForOled` across the panel (`filter_route.h:75-78`, `mod_fx/type.h:73-76`).
/// @param slot_frame The column's value region; the drawing covers the panel below the title.
/// @param focused    Unused.
/// @param ctx        Must point at a `ColumnSlot::OptionList`.
void option_list_column(deluge::ui::oled::OledFrame& slot_frame, bool focused, void* ctx);

/// @brief The icon legacy's `renderSubmenuItemTypeForOled` draws at the right of @p node's row: the arrow, or a
///        checkbox for a toggle.
/// @param node The row's node.
/// @param ctx  The context its value is read in.
/// @return The icon, or null for none.
const deluge::gfx::oled::Icon* row_icon(const Node& node, const MenuContext& ctx);

/// @brief @p string_id's text, with `node.title_arg` written into its first `*` the way legacy's
///        `FormattedTitle` writes its argument (`asterixToInt`, `util/functions.h`): a single `*`
///        becomes one digit, `**` becomes the number in at least two digits and ends the text.
/// @param string_id One of @p node's l10n ids, its `name` or its `title`.
/// @param node      The node the text is for; its `title_arg` is the number.
/// @param ctx       The menu context, for a `kTitleArgSourceIndex` argument.
/// @param scratch   Storage the returned view may point into.
/// @return The text, unchanged when @p node has no argument or the text no `*`.
std::string_view node_text(uint16_t string_id, const Node& node, const MenuContext& ctx, NodeText& scratch);

/// @brief The label a horizontal column draws for @p node: legacy's `getColumnLabel` -- its own string
///        (`Node::column_name`), the short form of the name on a node that shortens it (`Node::short_column_label`),
///        or the name.
/// @param node    The column's node.
/// @param ctx     The menu context.
/// @param scratch Storage the returned view may point into.
/// @return The label text.
std::string_view column_label(const Node& node, const MenuContext& ctx, NodeText& scratch);

/// @brief @p node's name or title as legacy's `getName()`/`getTitle()` answer: its `Binding::name_text`, else the
///        static string (with any `title_arg`).
/// @param node    The node.
/// @param use     `ValueTextUse::Name` or `ValueTextUse::Title`.
/// @param ctx     The menu context.
/// @param scratch Storage the returned view may point into.
/// @return The text.
std::string_view display_text(const Node& node, ValueTextUse use, const MenuContext& ctx, NodeText& scratch);

/// @brief The song's input tick magnitude, which legacy's sync-level text is spelled against
///        (`Song::getInputTickMagnitude`); 0 with no model.
/// @param ctx The menu context.
/// @return The magnitude.
int32_t input_tick_magnitude(const MenuContext& ctx);

/// @brief Fill @p col's label and value drawing for @p node showing @p value: legacy's `getColumnLabel`,
///        `showColumnLabel` and `renderInHorizontalMenu` for that item.
/// @param node  The column's node.
/// @param ctx   The menu context.
/// @param value The value to show, in the node's own units (a toggle's is 0 or 1).
/// @param slot  Storage @p col's label and `ctx` point into.
/// @param col   The column; its label, `show_label`, `render` and `ctx` are written.
void present_column(const Node& node, const MenuContext& ctx, int32_t value, ColumnSlot& slot,
                    deluge::ui::oled::Column& col);

/// @brief The value text of the notification an edit on @p column raises: legacy's `getNotificationValue` for that
///        item -- its own (`Binding::value_text`), or its kind's.
/// @param column The column's node.
/// @param ctx    The menu context.
/// @param value  The value to spell, in the node's own units.
/// @param out    Storage the returned view may point into.
/// @return The text; empty for a `Toggle`, whose legacy item spells none.
std::string_view column_notification(const Node& column, const MenuContext& ctx, int32_t value, std::span<char> out);

/// @brief Helpers the menu's presentation and its scene share.
namespace detail {

/// @brief Resolve a `Node`'s l10n id to text through the one sanctioned path
///        (`shell/profile.h:147`, which already implements the digits fallback).
/// @param string_id The l10n id.
/// @param scratch   Storage the returned view may point into.
/// @return The text.
std::string_view text_of(uint16_t string_id, deluge::ui::shell::TextScratch& scratch);

/// @brief @p v clamped into `[lo, hi]`.
inline int32_t clamp_to(int32_t v, int32_t lo, int32_t hi) {
	return v < lo ? lo : (v > hi ? hi : v);
}

/// @brief Does this kind draw a value screen rather than a list of children?
inline bool is_leaf(Kind k) {
	return k != Kind::Submenu;
}

} // namespace detail

} // namespace deluge::scenes::menu
