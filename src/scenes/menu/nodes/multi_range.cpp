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
#include "deluge-project/song.h"

#include "scenes/menu/context.h"
#include "scenes/menu/menu_scene.h"
#include "scenes/profile.h"

#include "gfx/oled/font.h"
#include "notify/notice_queue.h"
#include "ui/oled/list.h"
#include "ui/oled/text.h"

#include "definitions_cxx.hpp"
#include "processing/source.h"
#include "storage/flash_storage.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

// `util/lookuptables/lookuptables.h` is outside this layer's include allowance
// (`scripts/check_layer_rules.py`'s `scenes-includes-outside`), so the three note-name tables
// `noteCodeToString` reads are declared here at the global scope where they are defined
// (`util/lookuptables/lookuptables.cpp`) -- the same spelling `sound_sidechain.cpp` uses for
// the sidechain rate tables.
extern const uint8_t noteCodeToNoteLetter[];
extern const uint8_t noteCodeToNoteLetterFlats[];
extern const bool noteCodeIsSharp[];

namespace deluge::scenes::menu::nodes {

namespace {

using deluge::l10n::String;

/// @brief `noteCodeToString(noteCode, buffer, nullptr, /*appendOctaveNo=*/true)`
///        (`noteCodeToString` in `util/functions.cpp`), transcribed and appending into a caller-owned buffer.
///
/// The sharps-vs-flats choice really is a global preference read
/// (`FlashStorage::defaultUseSharps`), not menu state, so it is read here exactly as legacy reads it.
void append_note_name(char* out, size_t& len, size_t cap, int32_t note_code) {
	// The floor of the twelfths, so a note code below 0 lands in the octave under it
	int32_t within = note_code % 12;
	int32_t octave = note_code / 12 - 2;
	if (within < 0) {
		within += 12;
		octave--;
	}
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
	// octave (a note code below 24 -- the bottom two octaves of the MIDI range, and below).
	int64_t magnitude = octave;
	if (magnitude < 0) {
		put('-');
		magnitude = -magnitude;
	}
	char digits[10];
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
	if (index == 0) { // `:290-295`
		append(buf, len, cap, deluge_profile().text_or_digits(sid(String::STRING_FOR_BOTTOM), scratch));
	}
	else { // `:296-299`
		append_note_name(buf, len, cap, range_top_note(ctx, index - 1) + 1);
	}
	// `:301-306` -- pad the lower end out to column 7, so the two halves line up down the list.
	// Reproduced rather than replaced by a single separator, because it is what makes this function
	// byte-for-byte comparable against `MultiRange::getText` in `tables_1_spec`.
	while (len < 7 && len + 1 < cap) {
		buf[len++] = ' ';
	}
	append(buf, len, cap, "- ");
	if (index == count - 1) { // `:309-319`
		append(buf, len, cap, "top");
	}
	else { // `:320-331`
		append_note_name(buf, len, cap, range_top_note(ctx, index));
	}
	return {buf, len};
}

namespace {

size_t transpose_range_suffix(const MenuContext& ctx, int32_t source, std::span<char> out) {
	// `sample::Transpose::getNotificationValue` (`sample/transpose.h:158-191`): only for the chosen range of a sample
	// source with several.
	const int32_t count = range_count(ctx);
	const int32_t index = ctx.range_index;
	if (ctx.source_index != source || index < 0 || count <= 1 || index >= count) {
		return 0;
	}
	const instrument::SoundState* sound =
	    ctx.facts != nullptr && ctx.addresses.slot ? ctx.facts->sound_state(*ctx.addresses.slot) : nullptr;
	if (sound == nullptr || sound->sources[source].osc_type != OscType::SAMPLE) {
		return 0;
	}
	char* buf = out.data();
	const size_t cap = out.size() + 1;
	size_t len = 0;
	deluge::ui::shell::TextScratch scratch;
	append(buf, len, cap, " (");
	if (index == 0) {
		append(buf, len, cap, deluge_profile().text_or_digits(sid(String::STRING_FOR_BOTTOM), scratch));
	}
	else {
		append_note_name(buf, len, cap, range_top_note(ctx, index - 1) + 1);
	}
	append(buf, len, cap, "-");
	if (index == count - 1) {
		append(buf, len, cap, "top");
	}
	else {
		append_note_name(buf, len, cap, range_top_note(ctx, index));
	}
	append(buf, len, cap, ")");
	return len;
}

} // namespace

size_t transpose0_range_suffix(const MenuContext& ctx, std::span<char> out) {
	return transpose_range_suffix(ctx, 0, out);
}

size_t transpose1_range_suffix(const MenuContext& ctx, std::span<char> out) {
	return transpose_range_suffix(ctx, 1, out);
}

void MultiRangeItem::on_focus(const MenuContext& ctx, MenuState& state) const {
	// `MultiRange::beginSession` (`multi_range.cpp:40-67`), on the source's remembered range (`Source::defaultRangeI`,
	// which every played note moves too).
	const int32_t count = range_count(ctx);
	int32_t cursor = 0;
	if (ctx.addresses.slot) {
		const auto source = static_cast<uint8_t>(ctx.source_index);
		std::optional<int32_t> remembered;
		if (ctx.range_index >= 0) { // `:43-45` -- a range is already selected, so it is the default
			remembered = ctx.range_index;
			if (ctx.sink != nullptr) {
				ctx.sink->set_default_range(*ctx.addresses.slot, source, static_cast<uint16_t>(ctx.range_index));
			}
		}
		else if (ctx.facts != nullptr) {
			if (const std::optional<uint16_t> held = ctx.facts->default_range(*ctx.addresses.slot, source)) {
				remembered = *held;
			}
		}
		const bool valid = remembered && *remembered >= 0 && *remembered < count;
		cursor = valid ? *remembered : count >> 1;
		if (!valid && count > 0 && ctx.sink != nullptr) { // `:48-51`: an invalid default is worked out afresh
			ctx.sink->set_default_range(*ctx.addresses.slot, source, static_cast<uint16_t>(cursor));
		}
	}

	state.list[depth_of(state)] = deluge::ui::ListState{};
	state.list[depth_of(state)].cursor = static_cast<int16_t>(cursor);
	// `:58-65`: the window opens with the range on its last row; it keeps its own scroll from then on.
	state.list[depth_of(state)].keep_scroll = true;
	state.list[depth_of(state)].scroll = -1;
	state.edit_edge = deluge::ui::oled::RangeEdge::Off; // `:66`, `Range::beginSession`
	state.scroll[depth_of(state)] = deluge::ui::ScrollText{};

	// `:56` -- `soundEditor.setCurrentMultiRange(this->getValue())`. A `CustomItem` gets a
	// `const MenuContext&`, so the choice is recorded and `MenuScene::after_render` applies it
	// through `menu::select_range` (see `MenuWalkState::range_choice`).
	//
	// `:54`'s `getOrCreateFirstRange()` is *not* repeated here: `MenuScene::enter_child` already
	// runs it for every range-dependent descent, which is where legacy runs it too
	// (`checkPermissionToBeginSessionForRangeSpecificParam:1938`, ahead of every arm).
	state.walk.range_choice = static_cast<int16_t>(cursor);
}

void MultiRangeItem::render(deluge::ui::oled::OledFrame& f, const MenuContext& ctx, MenuState& state) const {
	const Node* node = state.depth < kMenuDepth ? state.path[state.depth] : nullptr;
	deluge::ui::shell::TextScratch title_scratch;
	deluge::ui::oled::title(f, node != nullptr ? deluge_profile().text_or_digits(node->title, title_scratch)
	                                           : std::string_view{});

	deluge::ui::ListState& list_state = state.list[depth_of(state)];
	const int32_t index = list_state.cursor;
	// `SoundEditor::buttonAction`'s BACK arm asks a range item to cancel its edge first (`Range::cancelEditingIfItsOn`,
	// `range.cpp:87-95`), and goes up only when there was none.
	// At the root the menu leaves BACK to its own handling (closing the editor) unless an item claims it, so the press
	// is read here as well as from the menu's own read below the root. A BACK the host's UI mode refuses is not read.
	if (state.edit_edge != deluge::ui::oled::RangeEdge::Off && state.tick.back_acts
	    && (state.tick.back_claimed || f.back_pressed())) {
		state.edit_edge = deluge::ui::oled::RangeEdge::Off;
		state.tick.back_claimed = false;
	}
	// `Range::horizontalEncoderAction` (`range.cpp:35-84`): every turn is the item's, and with Shift it does nothing.
	if (const int8_t h = f.horizontal(); h != 0 && !f.shift()) {
		state.edit_edge = next_range_edge(ctx, index, state.edit_edge, h);
	}
	list_state.freeze_cursor = false;
	if (const int32_t t = f.turn(); t != 0) {
		if (ctx.facts != nullptr && ctx.facts->popup_showing()) {
			// `selectEncoderAction` does nothing at all while a popup is up (`multi_range.cpp:71-73`).
			list_state.freeze_cursor = true;
		}
		else if (state.edit_edge != deluge::ui::oled::RangeEdge::Off) {
			// The edge arm (`:76-124`): one note per turn, issued as one ranges write.
			std::vector<deluge::instrument::RangeState> ranges = ranges_of(ctx);
			if (index >= 0 && index < std::ssize(ranges)) {
				step_range_edge(ranges, index, state.edit_edge, t);
				if (ctx.sink != nullptr && ctx.addresses.slot) {
					(void)ctx.sink->write_ranges(*ctx.addresses.slot, static_cast<uint8_t>(ctx.source_index), ranges);
				}
			}
			f.gate().request_repaint();
		}
		else if (f.shift()) {
			// The Shift arm (`:130-201`): a range inserted beside the current one, which the cursor moves to.
			list_state.freeze_cursor = true;
			const std::vector<deluge::instrument::RangeState> ranges = ranges_of(ctx);
			if (const std::optional<RangeInsert> plan = plan_range_insert(ranges, index, t); !plan) {
				deluge::notify::notice_string(static_cast<uint16_t>(String::STRING_FOR_RANGE_CONTAINS_ONE_NOTE));
			}
			else if (ctx.sink != nullptr && ctx.addresses.slot
			         && ctx.sink->insert_range(*ctx.addresses.slot, static_cast<uint8_t>(ctx.source_index),
			                                   static_cast<uint16_t>(plan->at), plan->top_note, plan->previous_top)) {
				list_state.cursor = static_cast<int16_t>(plan->at);
				if (!plan->previous_top) {
					list_state.scroll++; // `:188`: inserted before, the window moves with the old range
				}
				deluge::notify::notice_string(static_cast<uint16_t>(String::STRING_FOR_RANGE_INSERTED),
				                              deluge::notify::Notice::Kind::Console);
			}
			f.gate().request_repaint();
		}
	}

	// `drawPixelsForOled` (`:364-408`) builds `kOLEDMenuNumOptionsVisible` labels around its own
	// `currentScroll` and calls `drawItemsForOled`. This builds every row and lets `ui::oled::list` pick the
	// slice; `ListState::keep_scroll` gives the widget legacy's window, which keeps its scroll rather than
	// re-centring on the cursor.
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
	// `:383-406`: while an edge is edited no row is selected, and the edited half of the range's row is inverted --
	// the left end's six characters from x = kTextSpacingX, or the right end from x = 10 * kTextSpacingX to the edge.
	const deluge::ui::oled::RangeEdge edge = state.edit_edge;
	list_state.hide_cursor = edge != deluge::ui::oled::RangeEdge::Off;
	if (list_state.hide_cursor && list_state.cursor >= 0 && list_state.cursor < count) {
		deluge::ui::oled::ListRow& span = rows[static_cast<size_t>(list_state.cursor)];
		if (edge == deluge::ui::oled::RangeEdge::Left) {
			span.invert_from = gfx::oled::kTextSpacingX;
			span.invert_to = gfx::oled::kTextSpacingX * 7;
		}
		else {
			span.invert_from = gfx::oled::kTextSpacingX * 10;
			span.invert_to = static_cast<int16_t>(f.bounds().w);
		}
	}

	state.tick.render_resp = deluge::ui::oled::list(
	    f, deluge::ui::oled::menu_rect(f), state.list[depth_of(state)],
	    std::span<const deluge::ui::oled::ListRow>{rows, static_cast<size_t>(count)}, state.scroll[depth_of(state)]);

	// `selectEncoderAction`'s "normal thing of just flicking through existing ranges" arm
	// (`:203-219`): the cursor IS the selection, and `setCurrentMultiRange` follows it. Republished
	// every frame rather than only on a move, for the same reason `SourceSelectionItem` does.
	const int32_t cursor = state.list[depth_of(state)].cursor;
	if (count > 0 && cursor >= 0 && cursor < count) {
		state.walk.range_choice = static_cast<int16_t>(cursor);
		// `:212`: a flick is remembered. A voice that starts without a pad or key (MIDI) moves the memory through
		// `Source::getRange` and not the cursor; a pad or key goes through `range_note_press`.
		if (state.tick.render_resp.changed && ctx.addresses.slot && ctx.sink != nullptr) {
			ctx.sink->set_default_range(*ctx.addresses.slot, static_cast<uint8_t>(ctx.source_index),
			                            static_cast<uint16_t>(cursor));
		}
	}
	if (count == 0 && f.select_pressed()) {
		state.tick.render_resp.entered = true; // nothing to pick: the press ascends
	}
}

deluge::ui::InputClaims MultiRangeItem::owned_input(const MenuContext& /*ctx*/, const MenuState& state) const {
	deluge::ui::InputClaims claims = kListInput;
	claims.horizontal = true;
	claims.back = state.edit_edge != deluge::ui::oled::RangeEdge::Off;
	return claims;
}

deluge::ui::shell::Result MultiRangeItem::handle(const deluge::ui::shell::UiEvent& /*event*/,
                                                 const deluge::ui::shell::UiContext& /*ui_ctx*/,
                                                 const MenuContext& /*ctx*/, MenuState& /*state*/) const {
	return {}; // every input the picker owns is read in `render`
}

std::optional<RangeInsert> plan_range_insert(std::span<const deluge::instrument::RangeState> ranges, int32_t index,
                                             int32_t offset) {
	const auto count = static_cast<int32_t>(ranges.size());
	if (index < 0 || index >= count) {
		return std::nullopt;
	}
	const auto top_of = [&](int32_t i) { return static_cast<int32_t>(ranges[static_cast<size_t>(i)].top_note); };
	// `:132-141`
	const int32_t bottom = index == 0 ? std::min<int32_t>(top_of(index) - 1, 0) : top_of(index - 1) + 1;
	// `:143-152`
	const int32_t top = index == count - 1 ? std::max<int32_t>(bottom + 1, 127) : top_of(index);
	if (top == bottom) { // `:154-157`
		return std::nullopt;
	}
	const auto mid = static_cast<int16_t>((top + bottom) >> 1);
	if (offset >= 0) { // inserted after, which takes the old range's top (`:177-182`)
		return RangeInsert{.at = index + 1, .top_note = static_cast<int16_t>(top), .previous_top = mid};
	}
	return RangeInsert{.at = index, .top_note = mid, .previous_top = std::nullopt}; // before (`:184-189`)
}

std::optional<RangeDelete> plan_range_delete(std::span<const deluge::instrument::RangeState> ranges, int32_t index) {
	const auto count = static_cast<int32_t>(ranges.size());
	if (count <= 1 || index < 0 || index >= count) { // `:245-249`
		return std::nullopt;
	}
	if (index == 0) { // `:257-260`
		return RangeDelete{.cursor = 0, .lower_top = std::nullopt};
	}
	const auto lower = static_cast<int32_t>(ranges[static_cast<size_t>(index - 1)].top_note);
	const auto deleted = static_cast<int32_t>(ranges[static_cast<size_t>(index)].top_note);
	// `:270-278`: under a deleted top range the one below becomes the top; else it takes the mean of the two tops.
	const int32_t top = index == count - 1 ? 32767 : (lower + deleted) >> 1;
	return RangeDelete{.cursor = index - 1, .lower_top = static_cast<int16_t>(top)};
}

void range_delete_press(const MenuContext& ctx, MenuState& state) {
	// `MultiRange::deletePress` (`multi_range.cpp:234-285`).
	if (state.edit_edge != deluge::ui::oled::RangeEdge::Off || (ctx.facts != nullptr && ctx.facts->popup_showing())) {
		return;
	}
	deluge::ui::ListState& list = state.list[depth_of(state)];
	const int32_t index = list.cursor;
	const std::optional<RangeDelete> plan = plan_range_delete(ranges_of(ctx), index);
	if (!plan) {
		deluge::notify::notice_string(static_cast<uint16_t>(String::STRING_FOR_LAST_RANGE_CANT_DELETE));
		return;
	}
	if (ctx.sink == nullptr || !ctx.addresses.slot
	    || !ctx.sink->delete_range(*ctx.addresses.slot, static_cast<uint8_t>(ctx.source_index),
	                               static_cast<uint16_t>(index), plan->lower_top)) {
		return;
	}
	list.cursor = static_cast<int16_t>(plan->cursor);
	state.walk.range_choice = static_cast<int16_t>(plan->cursor);
	deluge::notify::notice_string(static_cast<uint16_t>(String::STRING_FOR_RANGE_DELETED));
}

int32_t range_for_note(std::span<const deluge::instrument::RangeState> ranges, int32_t note) {
	const auto count = static_cast<int32_t>(ranges.size());
	for (int32_t i = 0; i < count; i++) {
		if (ranges[static_cast<size_t>(i)].top_note >= note) {
			return i;
		}
	}
	return count > 0 ? count - 1 : 0;
}

bool range_note_press(MenuContext& ctx, MenuState& state, int32_t note) {
	// `MultiRange::noteOnToChangeRange` (`multi_range.cpp:338-355`).
	if (state.edit_edge != deluge::ui::oled::RangeEdge::Off) {
		return false;
	}
	const std::vector<deluge::instrument::RangeState> ranges = ranges_of(ctx);
	if (ranges.size() <= 1) {
		return false;
	}
	const int32_t index = range_for_note(ranges, note);
	deluge::ui::ListState& list = state.list[depth_of(state)];
	if (index != list.cursor) {
		list.cursor = static_cast<int16_t>(index);
		state.walk.range_choice = static_cast<int16_t>(index);
		select_range(ctx, index);
	}
	return true;
}

bool may_edit_range_edge(const MenuContext& ctx, int32_t index, deluge::ui::oled::RangeEdge edge) {
	// `MultiRange::mayEditRangeEdge` (`multi_range.cpp:357-362`).
	switch (edge) {
	case deluge::ui::oled::RangeEdge::Left:
		return index != 0;
	case deluge::ui::oled::RangeEdge::Right:
		return index != range_count(ctx) - 1;
	case deluge::ui::oled::RangeEdge::Off:
		break;
	}
	return false;
}

deluge::ui::oled::RangeEdge next_range_edge(const MenuContext& ctx, int32_t index, deluge::ui::oled::RangeEdge current,
                                            int32_t offset) {
	using deluge::ui::oled::RangeEdge;
	// `Range::horizontalEncoderAction`'s two arms, mirrored.
	const RangeEdge towards = offset < 0 ? RangeEdge::Left : RangeEdge::Right;
	const RangeEdge away = offset < 0 ? RangeEdge::Right : RangeEdge::Left;
	if (current == towards) {
		return RangeEdge::Off;
	}
	if (may_edit_range_edge(ctx, index, towards)) {
		return towards;
	}
	return current == away ? RangeEdge::Off : current;
}

void step_range_edge(std::vector<deluge::instrument::RangeState>& ranges, int32_t index,
                     deluge::ui::oled::RangeEdge edge, int32_t offset) {
	using deluge::ui::oled::RangeEdge;
	const auto count = static_cast<int32_t>(ranges.size());
	if (index < 0 || index >= count) {
		return;
	}
	const auto at = [&](int32_t i) -> deluge::instrument::RangeState& { return ranges[static_cast<size_t>(i)]; };
	if (edge == RangeEdge::Left && index >= 1) {
		deluge::instrument::RangeState& lower = at(index - 1);
		if (offset >= 0) {
			const int32_t maximum = index < count - 1 ? at(index).top_note - 1 : 127;
			if (lower.top_note < maximum) {
				lower.top_note++;
			}
		}
		else {
			const int32_t minimum = index >= 2 ? at(index - 2).top_note + 1 : 0;
			if (lower.top_note > minimum) {
				lower.top_note--;
			}
		}
	}
	else if (edge == RangeEdge::Right) {
		deluge::instrument::RangeState& current = at(index);
		if (offset >= 0) {
			const int32_t maximum = index < count - 2 ? at(index + 1).top_note - 1 : 126;
			if (current.top_note < maximum) {
				current.top_note++;
			}
		}
		else {
			const int32_t minimum = index >= 1 ? at(index - 1).top_note + 1 : 1;
			if (current.top_note > minimum) {
				current.top_note--;
			}
		}
	}
}

const Node* multi_range_chain(const MenuContext& /*ctx*/, const MenuState& state) {
	return state.walk.pending_after_range; // `multi_range.cpp:334-336`
}

} // namespace deluge::scenes::menu::nodes
