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
/// @brief `MultiRangeItem`'s members and the picker's chain hook. See `multi_range.h`.

#include "scenes/menu/nodes/multi_range.h"

#include "scenes/menu/context.h"
#include "scenes/menu/menu_scene.h"
#include "scenes/profile.h"

#include "ui/oled/list.h"
#include "ui/oled/text.h"

#include "definitions_cxx.hpp"
#include "processing/source.h"
#include "storage/flash_storage.h"

#include <cstddef>
#include <cstdint>
#include <string_view>

// `util/lookuptables/lookuptables.h` is outside this layer's include allowance
// (`scripts/check_layer_rules.py`'s `scenes-includes-outside`), so the three note-name tables
// `noteCodeToString` reads are declared here at the global scope where they are defined
// (`util/lookuptables/lookuptables.cpp:487-490`) -- the same spelling `sound_sidechain.cpp` uses for
// the sidechain rate tables.
extern const uint8_t noteCodeToNoteLetter[];
extern const uint8_t noteCodeToNoteLetterFlats[];
extern const bool noteCodeIsSharp[];

namespace deluge::scenes::menu::nodes {

namespace {

using deluge::l10n::String;

/// @brief `noteCodeToString(noteCode, buffer, nullptr, /*appendOctaveNo=*/true)`
///        (`util/functions.cpp:1678-1710`), transcribed and appending into a caller-owned buffer.
///
/// The sharps-vs-flats choice really is a global preference read
/// (`FlashStorage::defaultUseSharps`), not menu state, so it is read here exactly as legacy reads it.
void append_note_name(char* out, size_t& len, size_t cap, int32_t note_code) {
	const int32_t octave = note_code / 12 - 2;
	const int32_t within = static_cast<uint16_t>(note_code + 120) % uint8_t{12};
	const bool sharps = FlashStorage::defaultUseSharps;

	const auto put = [&](char c) {
		if (len + 1 < cap) {
			out[len++] = c;
		}
	};
	put(static_cast<char>(sharps ? noteCodeToNoteLetter[within] : noteCodeToNoteLetterFlats[within]));
	if (noteCodeIsSharp[within]) {
		put(sharps ? '#' : static_cast<char>(FLAT_CHAR));
	}
	// `intToString(octave, thisChar, 1)`: at least one digit, with a leading '-' for a negative
	// octave (a note code below 24 -- the bottom two octaves of the MIDI range).
	int32_t magnitude = octave;
	if (magnitude < 0) {
		put('-');
		magnitude = -magnitude;
	}
	char digits[4];
	size_t d = 0;
	do {
		digits[d++] = static_cast<char>('0' + magnitude % 10);
		magnitude /= 10;
	} while (magnitude != 0 && d < sizeof(digits));
	while (d > 0) {
		put(digits[--d]);
	}
}

void append(char* out, size_t& len, size_t cap, std::string_view s) {
	for (const char c : s) {
		if (len + 1 < cap) {
			out[len++] = c;
		}
	}
}

size_t depth_of(const MenuState& state) {
	return state.depth < kMenuDepth ? state.depth : 0;
}

} // namespace

std::string_view range_row_label(const MenuContext& ctx, int32_t index, char* buf, size_t cap,
                                 deluge::ui::shell::TextScratch& scratch) {
	const int32_t count = range_count(ctx);
	size_t len = 0;
	if (index == 0) { // `:299-304`
		append(buf, len, cap, deluge_profile().text_or_digits(sid(String::STRING_FOR_BOTTOM), scratch));
	}
	else { // `:305-308`
		append_note_name(buf, len, cap, range_top_note(ctx, index - 1) + 1);
	}
	// `:310-315` -- pad the lower end out to column 7, so the two halves line up down the list.
	// Reproduced rather than replaced by a single separator, because it is what makes this function
	// byte-for-byte comparable against `MultiRange::getText` in `tables_1_spec`.
	while (len < 7 && len + 1 < cap) {
		buf[len++] = ' ';
	}
	append(buf, len, cap, "- ");
	if (index == count - 1) { // `:317-326`
		append(buf, len, cap, "top");
	}
	else { // `:327-340`
		append_note_name(buf, len, cap, range_top_note(ctx, index));
	}
	return {buf, len};
}

void MultiRangeItem::on_focus(const MenuContext& ctx, MenuState& state) const {
	// `MultiRange::beginSession` (`multi_range.cpp:37-65`).
	const int32_t count = range_count(ctx);
	if (ctx.source != nullptr) {
		if (ctx.range != nullptr) { // `:40-42` -- a range is already selected, so it is the default
			ctx.source->defaultRangeI = ctx.range_index;
		}
		if (ctx.source->defaultRangeI < 0 || ctx.source->defaultRangeI >= count) { // `:45-49`
			ctx.source->defaultRangeI = count >> 1;
		}
	}
	const int32_t cursor = ctx.source != nullptr ? ctx.source->defaultRangeI : 0;

	state.list[depth_of(state)] = deluge::ui::ListState{};
	state.list[depth_of(state)].cursor = static_cast<int16_t>(cursor);
	state.scroll[depth_of(state)] = deluge::ui::ScrollText{};

	// `:53` -- `soundEditor.setCurrentMultiRange(this->getValue())`. A `CustomItem` gets a
	// `const MenuContext&`, so the choice is recorded and `MenuScene::after_render` applies it
	// through `menu::select_range` (see `MenuState::range_choice`).
	//
	// `:52`'s `getOrCreateFirstRange()` is *not* repeated here: `MenuScene::enter_child` already
	// runs it for every range-dependent descent, which is where legacy runs it too
	// (`checkPermissionToBeginSessionForRangeSpecificParam:1938`, ahead of every arm).
	state.range_choice = static_cast<int16_t>(cursor);
}

void MultiRangeItem::render(deluge::ui::oled::OledFrame& f, const MenuContext& ctx, MenuState& state) const {
	const Node* node = state.depth < kMenuDepth ? state.path[state.depth] : nullptr;
	deluge::ui::shell::TextScratch title_scratch;
	deluge::ui::oled::title(f, node != nullptr ? deluge_profile().text_or_digits(node->title, title_scratch)
	                                           : std::string_view{});

	// `drawPixelsForOled` (`:377-402`) builds `kOLEDMenuNumOptionsVisible` labels around its own
	// `currentScroll` and calls `drawItemsForOled`. `ui::oled::list` owns the window and the scroll,
	// so this builds every row and lets the widget pick the slice -- the same simplification
	// `PatchCablesItem` makes, and the reason legacy's `currentScroll` member has no port (its own
	// header calls it "not strictly speaking needed").
	constexpr size_t kMaxRows = 64;
	constexpr size_t kRowChars = 24;
	const int32_t count = std::min<int32_t>(range_count(ctx), static_cast<int32_t>(kMaxRows));
	char bufs[kMaxRows][kRowChars];
	deluge::ui::shell::TextScratch scratch[kMaxRows];
	deluge::ui::oled::ListRow rows[kMaxRows];
	for (int32_t i = 0; i < count; i++) {
		const auto row = static_cast<size_t>(i);
		rows[row].label = range_row_label(ctx, i, bufs[row], kRowChars, scratch[row]);
		rows[row].selectable = true;
	}

	state.render_resp = deluge::ui::oled::list(
	    f, deluge::ui::oled::menu_rect(f), state.list[depth_of(state)],
	    std::span<const deluge::ui::oled::ListRow>{rows, static_cast<size_t>(count)}, state.scroll[depth_of(state)]);

	// `selectEncoderAction`'s "normal thing of just flicking through existing ranges" arm
	// (`:216-231`): the cursor IS the selection, and `setCurrentMultiRange` follows it. Republished
	// every frame rather than only on a move, for the same reason `SourceSelectionItem` does.
	const int32_t cursor = state.list[depth_of(state)].cursor;
	if (count > 0 && cursor >= 0 && cursor < count) {
		state.range_choice = static_cast<int16_t>(cursor);
	}
	if (count == 0 && f.select_pressed()) {
		state.render_resp.entered = true; // nothing to pick: the press ascends
	}
}

deluge::ui::shell::Result MultiRangeItem::handle(const deluge::ui::shell::UiEvent& /*event*/,
                                                 const deluge::ui::shell::UiContext& /*ui_ctx*/,
                                                 const MenuContext& /*ctx*/, MenuState& /*state*/) const {
	return {};
}

const Node* multi_range_chain(const MenuContext& /*ctx*/, const MenuState& state) {
	return state.pending_after_range; // `multi_range.cpp:343-345`
}

} // namespace deluge::scenes::menu::nodes
