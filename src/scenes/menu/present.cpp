/*
 * Copyright © 2026 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

/// @file
/// @brief How the menu spells and lays out what it shows. See `present.h`.

#include "scenes/menu/present.h"

#include "scenes/menu/bindings.h"
#include "scenes/menu/nodes/nodes.h"
#include "scenes/profile.h"

#include "gfx/oled/icons.h"
#include "gui/l10n/l10n.h"
#include "gui/l10n/short_label.h"
#include "gui/l10n/strings.h"
#include "model/song/song.h" // Song::getInputTickMagnitude() -- Kind::SyncLevel's value text
#include "modulation/sync.h" // syncValueToString -- Kind::SyncLevel's value text
#include "ui/core/metrics.h"
#include "ui/oled/columns.h"
#include "ui/oled/list.h"
#include "ui/oled/meters.h"
#include "ui/oled/text.h"
#include "ui/oled/values.h"

#include <algorithm>
#include <cstring>
#include <span>

namespace deluge::scenes::menu {

using deluge::ui::shell::TextScratch;

using namespace detail;

namespace {

/// @brief Render @p value into @p buf as decimal digits.
/// @return A view over @p buf; never longer than @p n.
std::string_view format_uint(uint32_t value, char* buf, size_t n) {
	char digits[12];
	size_t d = 0;
	do {
		digits[d++] = static_cast<char>('0' + value % 10);
		value /= 10;
	} while (value != 0 && d < sizeof(digits));
	size_t out = 0;
	while (d > 0 && out < n) {
		buf[out++] = digits[--d];
	}
	return {buf, out};
}

/// @brief `-value` as a magnitude, through `int64_t` so `INT32_MIN` does not overflow.
uint32_t magnitude_of(int32_t value) {
	return value < 0 ? static_cast<uint32_t>(-static_cast<int64_t>(value)) : static_cast<uint32_t>(value);
}

/// @brief Render @p value into @p buf as signed decimal digits.
/// @return A view over @p buf; never longer than @p n.
std::string_view format_int(int32_t value, char* buf, size_t n) {
	size_t out = 0;
	if (value < 0 && out < n) {
		buf[out++] = '-';
	}
	char digits[12];
	const std::string_view d = format_uint(magnitude_of(value), digits, sizeof(digits));
	for (const char c : d) {
		if (out >= n) {
			break;
		}
		buf[out++] = c;
	}
	return {buf, out};
}

/// @brief Render @p value into @p buf with a period @p places from the right.
///
/// Legacy's rule, from `Decimal::drawPixelsForOled` (`gui/menu_item/decimal.cpp:129-153`):
/// `intToString(value, buffer, numDecimalPlaces + 1)` zero-pads the digits to **at least
/// `places + 1`**, and the period is then drawn at `length - numDecimalPlaces`. So there is always
/// a whole part before the point, however small the value: 5 at 2 places is `0.05`, 0 is `0.00`,
/// 50 at 3 places is `0.050`, and a negative keeps its sign outside the padding (`-0.05`).
///
/// @param value  The value in the item's smallest unit.
/// @param places How many of its digits are after the point.
/// @param buf    Destination.
/// @param n      Size of @p buf.
/// @return A view over @p buf; never longer than @p n.
std::string_view format_decimal(int32_t value, uint8_t places, char* buf, size_t n) {
	if (places == 0) {
		return format_int(value, buf, n);
	}
	char digits[12];
	const std::string_view d = format_uint(magnitude_of(value), digits, sizeof(digits));
	// `intToString`'s minimum-width padding: at least `places + 1` digits, so the point always has
	// something to its left.
	const size_t total = std::max(d.size(), static_cast<size_t>(places) + 1);
	const size_t pad = total - d.size();

	size_t out = 0;
	if (value < 0 && out < n) {
		buf[out++] = '-';
	}
	for (size_t i = 0; i < total && out < n; i++) {
		if (i == total - places) {
			buf[out++] = '.';
			if (out >= n) {
				break;
			}
		}
		buf[out++] = i < pad ? '0' : d[i - pad];
	}
	return {buf, out};
}

/// @brief `norm` for a `MeterColumn`: where @p value sits in `[min, max]`.
float normalised(int32_t value, IntRange range) {
	const int32_t span = range.max - range.min;
	if (span == 0) {
		return 0.0f;
	}
	return static_cast<float>(value - range.min) / static_cast<float>(span);
}

} // namespace

namespace detail {

std::string_view text_of(uint16_t string_id, TextScratch& scratch) {
	return scenes::deluge_profile().text_or_digits(string_id, scratch);
}

} // namespace detail

// `Submenu::drawSubmenuItemsForOled` blits `renderSubmenuItemTypeForOled` on every row
// (`gui/menu_item/submenu.cpp:117`): the base arrow (`menu_item.cpp:69-75`), `Toggle`'s checkbox (`toggle.cpp:50-57`),
// `Selection`'s checkbox when `isToggle()` (`selection.cpp:38-56`), and the MIDI device-definition items' own (a
// `Kind::Custom` here).
const deluge::gfx::oled::Icon* row_icon(const Node& node, const MenuContext& ctx) {
	const auto checkbox = [&] {
		return bindings::read(node, ctx) != 0 ? &deluge::gfx::oled::icons::checkedBox
		                                      : &deluge::gfx::oled::icons::uncheckedBox;
	};
	switch (node.kind) {
	case Kind::Toggle:
		return checkbox();
	case Kind::Enum:
		return node.selection_is_toggle ? checkbox() : &deluge::gfx::oled::icons::submenuArrow;
	case Kind::Custom:
		// `node.custom` is never null for a `Kind::Custom` node -- `validate.h`'s `NullCustom` rule.
		return node.custom->row_icon(ctx);
	default:
		return &deluge::gfx::oled::icons::submenuArrow;
	}
}

std::string_view display_text(const Node& node, ValueTextUse use, const MenuContext& ctx, NodeText& scratch) {
	if (node.bind.name_text != nullptr) {
		const ValueText vt = node.bind.name_text(ctx, 0, use, std::span<char>{scratch.chars, sizeof(scratch.chars)});
		if (!vt.fallback) {
			return {scratch.chars, std::min(vt.length, sizeof(scratch.chars))};
		}
	}
	return node_text(use == ValueTextUse::Title ? node.title : node.name, node, ctx, scratch);
}

std::string_view column_label(const Node& node, const MenuContext& ctx, NodeText& scratch) {
	if (node.column_name != 0) {
		return node_text(node.column_name, node, ctx, scratch);
	}
	if (node.short_column_label) {
		if (const auto shortened = deluge::l10n::shortForm(static_cast<deluge::l10n::String>(node.name))) {
			return *shortened;
		}
	}
	return display_text(node, ValueTextUse::Name, ctx, scratch);
}

std::string_view node_text(uint16_t string_id, const Node& node, const MenuContext& ctx, NodeText& scratch) {
	const std::string_view text = text_of(string_id, scratch.digits);
	if (node.title_arg == 0) {
		return text;
	}
	const size_t star = text.find('*');
	if (star == std::string_view::npos) {
		return text;
	}
	const int32_t arg = node.title_arg == kTitleArgSourceIndex ? ctx.source_index + 1 : node.title_arg;
	size_t len = std::min(text.size(), sizeof(scratch.chars));
	std::copy_n(text.data(), len, scratch.chars);
	if (star + 1 < text.size() && text[star + 1] == '*') {
		// `intToString(i, str, 2)`: at least two digits, then the terminator.
		char digits[10];
		size_t n = 0;
		for (auto v = static_cast<uint32_t>(arg); (v != 0 || n < 2) && n < sizeof(digits); v /= 10) {
			digits[n++] = static_cast<char>('0' + (v % 10));
		}
		len = star;
		while (n > 0 && len < sizeof(scratch.chars)) {
			scratch.chars[len++] = digits[--n];
		}
	}
	else if (star < len) {
		scratch.chars[star] = static_cast<char>('0' + arg);
	}
	return std::string_view{scratch.chars, len};
}

int32_t input_tick_magnitude(const MenuContext& ctx) {
	return ctx.model != nullptr ? ctx.model->transport.inside_world_tick_magnitude
	                                  + ctx.model->transport.inside_world_tick_magnitude_offset_from_bpm
	                            : 0;
}

void option_list_column(deluge::ui::oled::OledFrame& slot_frame, bool /*focused*/, void* ctx) {
	const auto* call = static_cast<const ColumnSlot::OptionList*>(ctx);
	if (call == nullptr || call->node == nullptr || call->ctx == nullptr) {
		return;
	}
	deluge::ui::oled::OledFrame panel = deluge::ui::oled::panel_frame(slot_frame);
	const int32_t top = panel.canvas.geometry().visible_top;
	const int32_t width = panel.canvas.geometry().width;
	panel.hline(top + deluge::ui::kTitleSeparatorY, 0, width - 1);

	std::string_view names[kMaxDynamicNames];
	TextScratch scratch[kMaxDynamicNames];
	const auto n = static_cast<int32_t>(bindings::names(*call->node, *call->ctx, names, scratch));
	if (n == 0) {
		return;
	}
	// `Selection::drawPixelsForOled` (`selection.cpp:8-28`): the current option in the middle row where the list
	// allows, else as near it as the ends let it be.
	const int32_t visible = panel.style.menu_rows;
	const int32_t current = std::clamp(call->value, int32_t{0}, n - 1);
	const int32_t n_before = current - std::max<int32_t>(0, current - visible);
	const int32_t n_after = std::min<int32_t>(current + visible, n) - current;
	int32_t pos = (visible - 1) / 2;
	int32_t tail = visible - pos;
	if (n_before < pos) {
		pos = n_before;
	}
	else if (n_after < tail) {
		tail = n_after;
		pos = std::min<int32_t>(visible - tail, n_before);
	}
	deluge::ui::oled::ListRow rows[kMaxDynamicNames];
	for (int32_t i = 0; i < n; i++) {
		rows[i].label = names[i];
	}
	deluge::ui::ListState state{.cursor = static_cast<int16_t>(current), .scroll = static_cast<int16_t>(current - pos)};
	deluge::ui::ScrollText scroll{};
	const deluge::gfx::oled::Rect below_title{0, top, width, panel.canvas.geometry().height - top};
	panel.region(below_title, deluge::ui::InputMask::none(), [&](deluge::ui::oled::OledFrame& f) {
		(void)deluge::ui::oled::list(f, deluge::ui::oled::menu_rect(f), state,
		                             std::span<const deluge::ui::oled::ListRow>{rows, static_cast<size_t>(n)}, scroll,
		                             /*allow_motion=*/false);
	});
}

void present_column(const Node& node, const MenuContext& ctx, int32_t value, ColumnSlot& slot,
                    deluge::ui::oled::Column& col) {
	col.label = column_label(node, ctx, slot.label_scratch);
	col.show_label = node.show_column_label;
	if (node.bind.value_text != nullptr) {
		// A `getColumnLabel` that depends on the value or the context.
		const ValueText vt =
		    node.bind.value_text(ctx, value, ValueTextUse::Label, std::span<char>{slot.label, sizeof(slot.label)});
		if (!vt.fallback) {
			col.label = std::string_view{slot.label, std::min(vt.length, sizeof(slot.label))};
		}
	}

	const auto draw_art = [&](const deluge::ui::oled::ArtColumn& art) {
		slot.art = art;
		col.render = deluge::ui::oled::art_column;
		col.ctx = &slot.art;
	};
	const auto draw_text = [&](std::string_view text) {
		const size_t copied = std::min(text.size(), sizeof(slot.value));
		std::memcpy(slot.value, text.data(), copied);
		slot.text = deluge::ui::oled::TextColumn{.value = std::string_view{slot.value, copied}};
		col.render = deluge::ui::oled::text_column;
		col.ctx = &slot.text;
	};

	if (node.option_list_column) {
		slot.list = ColumnSlot::OptionList{.node = &node, .ctx = &ctx, .value = value};
		col.render = option_list_column;
		col.ctx = &slot.list;
		return;
	}

	if (node.bind.column_draw != nullptr) {
		if (const auto drawing = node.bind.column_draw(ctx, value)) {
			slot.draw = *drawing;
			col.render = deluge::ui::oled::draw_column;
			col.ctx = &slot.draw;
			col.span_next = drawing->span_next;
			return;
		}
	}

	if (node.bind.column_art != nullptr) {
		if (const auto art = node.bind.column_art(ctx, value)) {
			draw_art(*art);
			return;
		}
	}

	if (node.bind.value_text != nullptr) {
		// `Binding::value_text` (spec E-note-family §5.3): legacy's bespoke column text --
		// `35%`/`35L`, `3:4`, `FILL` struck through for `NOT_FILL` -- replaces whatever the
		// node's own `Kind` would otherwise have drawn, exactly as legacy's own
		// `renderInHorizontalMenu` override does for these items.
		const ValueText vt =
		    node.bind.value_text(ctx, value, ValueTextUse::Column, std::span<char>{slot.value, sizeof(slot.value)});
		if (!vt.fallback) {
			slot.text = deluge::ui::oled::TextColumn{
			    .value = std::string_view{slot.value, std::min(vt.length, sizeof(slot.value))},
			    .strikethrough = vt.strikethrough};
			col.render = deluge::ui::oled::text_column;
			col.ctx = &slot.text;
			return;
		}
	}

	// `Number::renderInHorizontalMenu` (`gui/menu_item/number.cpp:75-102`): the item's rendering style picks the meter,
	// and a plain number is a `meter_column` with `Hint::None` (`ui/oled/columns.h`'s note on the missing fifth arm).
	const auto draw_meter = [&] {
		const IntRange range = bindings::range(node, ctx);
		slot.meter.hint = node.hint;
		slot.meter.norm = normalised(value, range);
		slot.meter.raw = value;
		slot.meter.raw_max = range.max; // `Hint::Ducking` is the one hint that reads this
		col.render = deluge::ui::oled::meter_column;
		col.ctx = &slot.meter;
	};

	switch (node.kind) {
	case Kind::Submenu:
		col.render = deluge::ui::oled::submenu_column;
		break;
	case Kind::Toggle:
		slot.toggle.on = value != 0;
		col.render = deluge::ui::oled::toggle_column;
		col.ctx = &slot.toggle;
		break;
	case Kind::Enum: {
		std::array<std::string_view, kMaxDynamicNames> names{};
		std::array<TextScratch, kMaxDynamicNames> scratch{};
		const size_t count = bindings::names(node, ctx, names, scratch, bindings::NameForm::Short);
		draw_text(count == 0 ? std::string_view{} : names[static_cast<size_t>(clamp_to(value, 0, count - 1))]);
		break;
	}
	case Kind::SyncLevel: {
		// `SyncLevel::getColumnLabel` and `renderInHorizontalMenu` (`gui/menu_item/sync_level.cpp:40-77`): OFF in
		// the row face under the name, else the note length as the label over the sync type's icon.
		if (syncValueToSyncLevel(value) == SYNC_LEVEL_NONE) {
			draw_art({.text = deluge::l10n::getView(deluge::l10n::String::STRING_FOR_OFF)});
			break;
		}
		etl::string<30> label;
		syncValueToStringForHorzMenuLabel(syncValueToSyncType(value), syncValueToSyncLevel(value), label,
		                                  input_tick_magnitude(ctx));
		const size_t copied = std::min(label.size(), sizeof(slot.label));
		std::memcpy(slot.label, label.data(), copied);
		col.label = std::string_view{slot.label, copied};
		const deluge::gfx::oled::Icon* icon = &deluge::gfx::oled::icons::syncTypeEven;
		if (syncValueToSyncType(value) == SYNC_TYPE_DOTTED) {
			icon = &deluge::gfx::oled::icons::syncTypeDotted;
		}
		else if (syncValueToSyncType(value) == SYNC_TYPE_TRIPLET) {
			icon = &deluge::gfx::oled::icons::syncTypeTriplets;
		}
		draw_art({.icon = icon, .dy = -3});
		break;
	}
	case Kind::Decimal: {
		// `Decimal::renderInHorizontalMenu` (`gui/menu_item/decimal.cpp:176-191`): a rendering style other than NUMBER
		// draws as `Number` does; a NUMBER keeps three characters of a value at or below -10.00, which would not fit
		// the column.
		if (node.hint != Hint::None) {
			draw_meter();
			break;
		}
		char buf[16];
		std::string_view text = format_decimal(value, node.bind.decimal_places, buf, sizeof(buf));
		if (value <= -1000) {
			text = text.substr(0, 3);
		}
		draw_text(text);
		break;
	}
	default:
		draw_meter();
		break;
	}
}

std::string_view column_notification(const Node& column, const MenuContext& ctx, int32_t value, std::span<char> out) {
	if (column.bind.value_text != nullptr) {
		// `Binding::value_text` (spec E-note-family §5.3): the item's own `getNotificationValue`.
		const ValueText vt = column.bind.value_text(ctx, value, ValueTextUse::Notification, out);
		if (!vt.fallback) {
			return std::string_view{out.data(), std::min(vt.length, out.size())};
		}
	}
	const auto copy = [&](std::string_view from) {
		const size_t copied = std::min(from.size(), out.size());
		std::memcpy(out.data(), from.data(), copied);
		return std::string_view{out.data(), copied};
	};
	switch (column.kind) {
	case Kind::Decimal: {
		// `Decimal::getNotificationValue` (`decimal.h:31-33`), and what an override appends to it.
		const std::string_view base = format_decimal(value, column.bind.decimal_places, out.data(), out.size());
		if (column.bind.notification_suffix == nullptr) {
			return base;
		}
		const size_t added = column.bind.notification_suffix(ctx, out.subspan(base.size()));
		return std::string_view{out.data(), base.size() + added};
	}
	case Kind::Enum: {
		// `Selection::getNotificationValue` (`selection.cpp:62-64`): the option's full name.
		std::array<std::string_view, kMaxDynamicNames> names{};
		std::array<deluge::ui::shell::TextScratch, kMaxDynamicNames> scratch{};
		const size_t count = bindings::names(column, ctx, names, scratch);
		if (count == 0) {
			return {};
		}
		return copy(names[static_cast<size_t>(clamp_to(value, 0, static_cast<int32_t>(count) - 1))]);
	}
	case Kind::SyncLevel: {
		// `Enumeration::getNotificationValue` is `getShortOption` (`enumeration.h:25`), which `SyncLevel` spells as
		// the note length, or OFF (`sync_level.cpp:83-91`).
		if (value == 0) {
			return copy(deluge::l10n::getView(deluge::l10n::String::STRING_FOR_OFF));
		}
		etl::string<30> length;
		syncValueToString(static_cast<uint32_t>(value), length, input_tick_magnitude(ctx));
		return copy(std::string_view{length.data(), length.size()});
	}
	case Kind::Toggle:
		// `Toggle` has no `getNotificationValue`, so the base class's empty one stands (`menu_item.h:324`).
		return {};
	default:
		// `Number::getNotificationValue` (`number.cpp:429-431`): the value, doubled for the PERCENT style.
		return format_int(column.hint == Hint::Percent ? value * 2 : value, out.data(), out.size());
	}
}

} // namespace deluge::scenes::menu
