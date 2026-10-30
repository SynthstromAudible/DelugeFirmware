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
/// @brief The one place the menu system touches the model. See `bindings.h` for the interface.
///
/// A node edits through its target: `scenes/bind` reads its value, range, names and applicability from its descriptor
/// and the model, and its writes go to the menu's write sink as requests the app applies as Edits. A param node's
/// target is a PARAM target (`scenes/bind/param_display.cpp` for its display; the app's sink writes it through
/// SetParamValue, or SetKitParamValue under affect-entire). What remains here is the hook nodes' own read and write,
/// the per-kind ranges, names and learn.
///
/// **Port table** -- one entry per line (a markdown table here gets reflowed into an unreadable
/// run-on by clang-format). Every line below is transcribed, not called: `gui/menu_item/` is behind the
/// `scenes-includes-outside` wall.
///
///   * 0..50 default -- `Param::getMaxValue`/`getMinValue` (`gui/menu_item/param.h:34-35`)
///   * enum default -- `Enumeration::selectEncoderAction`'s `[0, numOptions)` clamp
///     (`gui/menu_item/enumeration.cpp:28`)
///   * toggle default -- `Toggle::selectEncoderAction` (`gui/menu_item/toggle.cpp:14-17`), a `Value<bool>`
///   * automation refresh -- `automationView.possiblyRefreshAutomationEditorGrid` (`patched_param/integer.cpp:73-77`,
///     `unpatched_param.cpp:77-81`)
///   * patched learn -- `PatchedParam::getLearningThing` (`gui/menu_item/patched_param.cpp:51-55`)
///   * unpatched learn -- `UnpatchedParam::getLearningThing` (`gui/menu_item/unpatched_param.cpp:88-92`)
///
/// **What did NOT come across.**
///
///   * The UI-mode half of the affect-entire condition. Legacy asks
///     `currentUIMode == UI_MODE_HOLDING_AFFECT_ENTIRE_IN_SOUND_EDITOR && soundEditor.editingKitRow()`
///     (`unpatched_param.cpp:52`). The first half is a `currentUIMode` read, which this layer may not do, so it
///     arrives as `MenuContext::affect_entire_held` -- a field the caller sets and `context_for` never does. It is
///     deliberately NOT `MenuContext::affect_entire`: that is the *clip's*
///     `InstrumentClip::affectEntire` setting, which is true on the kit-row grid-shortcut path
///     (`sound_editor.cpp:1205-1213`) where legacy edits one row only.
///   * `getRootUI() == &automationView` and the `lastSelectedParamID`/`Kind` match inside
///     `possiblyRefreshAutomationEditorGrid` (`patched_param/integer.cpp:73`, `automation_view.cpp:612-629`). The
///     notify design replaces "am I in the automation view, and is this the param it is showing?"
///     with unconditional facts. See `write()`'s own comment for which are raised and why.
///
/// **`currentSong` references in this file: 0** (a ratchet holds it there).

#include "scenes/menu/bindings.h"
#include "gui/l10n/short_label.h"

#include "scenes/bind/param_display.h"

#include "scenes/bind/dispatch.h"
#include "scenes/bind/slot_reader.h"

#include "scenes/menu/context.h"
#include "scenes/menu/note_bridge.h"
#include "scenes/profile.h"

#include "definitions_cxx.hpp"
#include "model/clip/clip.h"
#include "model/clip/instrument_clip.h"
#include "model/drum/drum.h"
#include "model/instrument/kit.h"
#include "model/note/note_row.h"
#include "model/output.h"
#include "model/song/song.h"
#include "modulation/params/param.h"
#include "modulation/params/param_collection.h"
#include "modulation/params/param_manager.h"
#include "notify/dirty_set.h"
#include "processing/sound/sound_drum.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>

namespace deluge::scenes::menu::bindings {

namespace {

namespace params = deluge::modulation::params;

/// What a target node shows, or nullopt where its target does not resolve or the menu has no model to read.
std::optional<deluge::scenes::bind::View> target_view(const Node& node, const MenuContext& ctx) {
	if (ctx.model == nullptr || ctx.facts == nullptr) {
		return std::nullopt;
	}
	return deluge::scenes::bind::view(node.bind.target, *ctx.model, ctx.addresses, *ctx.facts);
}

/// The text of a choice listed although it no longer applies: "<name> (<n/a>)". One buffer per listed entry, valid
/// until the next names() call, as the shell's TextScratch is.
std::array<std::array<char, 40>, kMaxDynamicNames> unavailable_text{};

/// @brief How many `Kind::Enum` options @p node has -- the input to its default range.
///
/// This is the *option* count, legacy's `Enumeration::size()`, not the number of names a particular
/// caller has room to render: `Enumeration::selectEncoderAction` clamps against `size()` on every
/// detent (`enumeration.cpp:19,28`), so running a `dynamic_names` hook to obtain it is exactly what
/// legacy does, at the same rate. That is why the window here is the full `kMaxDynamicNames`
/// (`node.h:98` -- the documented cap on what one call may produce) rather than whatever `out` span
/// `names()` was handed: a caller with room for four names still edits a ten-option enum over its
/// real 0..9 range. Cost is a `std::string_view[64]` -- 512 bytes on the
/// 32-bit target -- in this frame for the duration of the hook call.
size_t name_count(const Node& node, const MenuContext& ctx) {
	if (node.bind.dynamic_names != nullptr) {
		std::string_view window[kMaxDynamicNames]{};
		return std::min(node.bind.dynamic_names(ctx, std::span<std::string_view>{window}), kMaxDynamicNames);
	}
	return std::min(node.bind.names.size(), kMaxDynamicNames);
}

} // namespace

/// Legacy `unpatched_param.cpp:52` / `patched_param/integer.cpp:45` -- write the whole kit rather than the
/// selected row? `editingKitRow()` (`sound_editor.cpp:207-209`) is `is_kit && !is_kit_global_fx`;
/// the held-button half arrives as `affect_entire_held` (see the file header). Declared in
/// `bindings.h` because a couple of the tables' own `set` hooks branch on it.
bool writes_whole_kit(const MenuContext& ctx) {
	return ctx.affect_entire_held && ctx.is_kit && !ctx.is_kit_global_fx && ctx.addresses.kit_output.has_value()
	       && ctx.addresses.clip.has_value();
}

void write_song(const MenuContext& ctx, std::string_view path, int32_t value) {
	if (ctx.sink == nullptr) {
		return;
	}
	(void)ctx.sink->write({.target = {.kind = deluge::scenes::bind::Target::Kind::MODEL,
	                                  .path = path,
	                                  .group = deluge::project::SettingGroupId::SONG},
	                       .menu_value = value});
}

bool relevant(const Node& node, const MenuContext& ctx) {
	if (node.bind.target.valid()) {
		const std::optional<deluge::scenes::bind::View> shown = target_view(node, ctx);
		return shown && shown->applies;
	}
	return node.relevant == nullptr || node.relevant(ctx);
}

size_t listed_children(const Node& node, const MenuContext& ctx, std::span<const Node*> out) {
	size_t kept = 0;
	if (node.dynamic_children != nullptr) {
		const size_t produced = std::min(node.dynamic_children(ctx, out), out.size());
		for (size_t i = 0; i < produced; i++) {
			const Node* child = out[i];
			if (child != nullptr && relevant(*child, ctx)) {
				out[kept++] = child; // kept <= i always, so this compaction never overwrites ahead
			}
		}
		return kept;
	}
	for (const Node* child : node.children) {
		if (kept >= out.size()) {
			break;
		}
		if (child != nullptr && relevant(*child, ctx)) {
			out[kept++] = child;
		}
	}
	return kept;
}

std::optional<int32_t> stored(const deluge::scenes::bind::Target& target, const MenuContext& ctx) {
	if (ctx.model == nullptr || ctx.facts == nullptr || !ctx.addresses.slot) {
		return std::nullopt;
	}
	return deluge::scenes::bind::SlotReader{*ctx.model, *ctx.addresses.slot, *ctx.facts}.read(target.path);
}

int32_t read(const Node& node, const MenuContext& ctx) {
	const Binding& bind = node.bind;

	if (bind.target.valid()) {
		const std::optional<deluge::scenes::bind::View> shown = target_view(node, ctx);
		return shown ? shown->value : 0;
	}

	// `get` first, then `param` -- `node.h`'s documented precedence. A node with both is one of
	// legacy's `menu_item::Transpose` derivatives, whose value lives outside the param set and
	// whose `ParamRef` exists only so `learn_target` below has something to bind.
	if (bind.get != nullptr) {
		return bind.get(ctx);
	}

	return 0;
}

void write(const Node& node, const MenuContext& ctx, int32_t value) {
	const Binding& bind = node.bind;

	// A target node: the request goes to the menu's sink, which applies the target's Edit. The stale entry, a value
	// outside the range and an unresolved target ask for nothing.
	if (bind.target.valid()) {
		if (ctx.model == nullptr || ctx.facts == nullptr || ctx.sink == nullptr) {
			return;
		}
		const std::optional<deluge::scenes::bind::WriteRequest> request =
		    deluge::scenes::bind::request_for(bind.target, *ctx.model, ctx.addresses, *ctx.facts, value);
		if (request && ctx.sink->write(*request)) {
			notify::raise(notify::Dirty::Oled);
			if (bind.repaints_grid) {
				notify::raise(notify::Dirty::GridMain);
			}
			if (bind.target.kind == deluge::scenes::bind::Target::Kind::PARAM) {
				// Three facts, replacing three legacy effects. Each is unconditional here where legacy was
				// gated; over-invalidation, never loss.
				//
				//  * `Dirty::ParamAutomation` and `Dirty::ModTarget` -- the OLED half. Legacy reaches it two
				//    ways: `possiblyRefreshAutomationEditorGrid` (`patched_param/integer.cpp:73-77` /
				//    `unpatched_param.cpp:77-81`), and the knob-indicator refresh
				//    `setCurrentValueInResponseToUserInput` triggers through
				//    `ParamManager::notifyParamModifiedInSomeWay` -> `view.notifyParamAutomationOccurred`
				//    (`auto_param.cpp:1161`, `view.cpp:1708-1728`), whose deferred `displayAutomation()` calls
				//    `setKnobIndicatorLevels()` (`view.cpp:1758-1761`). `legacy_redraw_for` lists both
				//    enumerators under `r.oled` (`gui/ui/notify_drain_mapping.cpp:20`).
				//
				//  * `Dirty::GridMain` -- the PAD half, and the reason the pair above is not enough.
				//    `possiblyRefreshAutomationEditorGrid` ends in
				//    `uiNeedsRendering(&automationView)` (`automation_view.cpp:625`), i.e. a main-pad re-render
				//    (`AutomationView::renderMainPads`, `:633`) -- and `ParamAutomation`/`ModTarget` appear
				//    *only* in `r.oled`, never in `r.main_rows` (`notify_drain_mapping.cpp:13-14`). Without this
				//    third raise, editing the param the automation view is showing would stop repainting its
				//    grid the moment `MenuScene` replaces the legacy items. `Dirty::GridMain` is the enumerator
				//    `r.main_rows` carries for exactly this; the mapping file itself is left alone.
				//
				//    Coarser than legacy on purpose: `possiblyRefreshAutomationEditorGrid` repaints only when the
				//    edited param *is* the view's `lastSelectedParamID`/`Kind` (`automation_view.cpp:612-623`),
				//    a comparison this layer cannot make -- it may not read `automationView` or the root UI. So
				//    every param write asks for one grid repaint the drain already coalesces, rather than
				//    silently dropping the one that mattered.
				notify::raise(notify::Dirty::ParamAutomation);
				notify::raise(notify::Dirty::ModTarget);
				notify::raise(notify::Dirty::GridMain);
			}
		}
		return;
	}

	// A `get`/`set` (or `action`) node: the hook owns the model write, so all this layer knows is
	// that something the OLED shows has changed. Tested before `param` -- `node.h`'s documented
	// precedence, and the reason a `menu_item::Transpose` node can carry a learn-only `ParamRef`
	// without this function writing to it.
	if (bind.set != nullptr) {
		bind.set(ctx, value);
		notify::raise(notify::Dirty::Oled);
		return;
	}
}

void adjust(const Node& node, const MenuContext& ctx, int32_t offset) {
	// Mirrors `write`'s `get`/`set` hook arm (`:563-567` above): the hook owns the model write --
	// legacy's `adjustVelocity`/`adjustProbability`/`adjustNoteIterance`/`adjustNoteFillWithOffset`
	// -- so all this layer knows is that something the OLED shows has changed.
	if (node.bind.adjust != nullptr) {
		node.bind.adjust(ctx, offset);
		notify::raise(notify::Dirty::Oled);
	}
}

IntRange range(const Node& node, const MenuContext& ctx) {
	if (node.bind.target.valid()) {
		const std::optional<deluge::scenes::bind::View> shown = target_view(node, ctx);
		return shown ? IntRange{shown->least, shown->greatest, 1} : IntRange{0, 0, 1};
	}
	// node.h:111-117's documented order: a dynamic hook, then the literal bounds a builder was
	// given, then the legacy per-kind default.
	if (node.bind.range != nullptr) {
		return node.bind.range(ctx);
	}
	if (node.bind.fixed_range.has_value()) {
		return *node.bind.fixed_range;
	}

	switch (node.kind) {
	case Kind::Toggle:
		// `Toggle` is a `Value<bool>` (`gui/menu_item/toggle.h:8`) whose encoder action stores
		// `offset > 0` (`toggle.cpp:14-17`).
		return IntRange{0, 1, 1};

	case Kind::Enum: {
		// `Enumeration::selectEncoderAction` clamps to `[0, numOptions)` (`enumeration.cpp:28`).
		const size_t count = name_count(node, ctx);
		return IntRange{0, count > 0 ? static_cast<int32_t>(count) - 1 : 0, 1};
	}

	default:
		// `param.h:34-35` (`kMaxMenuValue` / `kMinMenuValue`), forwarded by
		// `patched_param/integer.h:35-36` and `unpatched_param.h:42-43`.
		//
		// This arm is also where `Kind::Bipolar`, `Range`, `MultiRange`, `Colour` and `SyncLevel`
		// land. Their interpreters are E2's, and each will want its own
		// default -- legacy `menu_item::Bipolar`, `menu_item::Range`, `multi_range.h`,
		// `menu_item::Colour`, `sync_level.h`. Until then 0..50 is a placeholder, not a claim:
		// nothing in E1's tables declares one of those kinds, and every one of them will arrive
		// with either a `fixed_range` from its builder or a `range` hook, both of which are
		// consulted above before this switch is ever reached. Deliberately not a hard failure --
		// a `FREEZE` in a library this layer means to keep pure would be worse than a wrong number.
		return IntRange{kMinMenuValue, kMaxMenuValue, 1};
	}
}

size_t names(const Node& node, const MenuContext& ctx, std::span<std::string_view> out,
             std::span<deluge::ui::shell::TextScratch> scratch, NameForm form) {
	// The two spans are documented as the same length; the shorter one wins if a caller gets it
	// wrong, so a mismatch truncates rather than writing past either end.
	const size_t limit = std::min({out.size(), scratch.size(), kMaxDynamicNames});
	if (limit == 0) {
		return 0;
	}

	const deluge::ui::shell::BoardProfile& text = scenes::deluge_profile();
	const bool short_form = form == NameForm::Short && node.short_options;
	// An id's text in the form asked for: the short table's entry where the node shortens and there is one.
	const auto name_of = [&](uint16_t id, deluge::ui::shell::TextScratch& slot) {
		if (short_form) {
			if (const auto shortened = deluge::l10n::shortForm(static_cast<deluge::l10n::String>(id))) {
				return *shortened;
			}
		}
		return text.text_or_digits(id, slot);
	};
	if (node.bind.target.valid()) {
		if (ctx.model == nullptr || ctx.facts == nullptr) {
			return 0;
		}
		std::array<deluge::scenes::bind::ChoiceName, kMaxDynamicNames> listed{};
		const size_t count = std::min(
		    deluge::scenes::bind::choice_names(node.bind.target, *ctx.model, ctx.addresses, *ctx.facts, listed), limit);
		for (size_t i = 0; i < count; ++i) {
			const std::string_view name = name_of(static_cast<uint16_t>(listed[i].name), scratch[i]);
			if (!listed[i].unavailable) {
				out[i] = name;
				continue;
			}
			deluge::ui::shell::TextScratch mark_scratch{};
			const std::string_view mark = text.text_or_digits(
			    static_cast<uint16_t>(deluge::l10n::String::STRING_FOR_CHOICE_UNAVAILABLE), mark_scratch);
			const int written =
			    snprintf(unavailable_text[i].data(), unavailable_text[i].size(), "%.*s (%.*s)",
			             static_cast<int>(name.size()), name.data(), static_cast<int>(mark.size()), mark.data());
			out[i] = std::string_view(unavailable_text[i].data(), std::min(static_cast<size_t>(std::max(written, 0)),
			                                                               unavailable_text[i].size() - 1));
		}
		return count;
	}

	if (node.bind.dynamic_names != nullptr) {
		// The hook writes views it owns (static strings, or storage of its own); `scratch` is for
		// the id path only, and `DynamicNamesFn`'s signature (node.h:90) has nowhere to pass it.
		return std::min(node.bind.dynamic_names(ctx, out.first(limit)), limit);
	}

	// Ids resolve through the profile's `text_or_digits`, never `text_lookup` directly --
	// `shell/profile.h:147` already implements the digits fallback for an id with no entry.
	const size_t count = std::min(node.bind.names.size(), limit);
	for (size_t i = 0; i < count; ++i) {
		out[i] = name_of(node.bind.names[i], scratch[i]);
	}
	return count;
}

std::optional<deluge::project::ControlsAddress> controls_owner(const MenuContext& ctx) {
	using Kind = deluge::project::ControlsAddress::Kind;
	if (const auto& slot = ctx.addresses.slot) {
		return deluge::project::ControlsAddress{
		    .kind = slot->kind == deluge::project::SlotAddress::Kind::DRUM ? Kind::DRUM : Kind::OUTPUT, .id = slot->id};
	}
	if (const auto& chain = ctx.addresses.chain) {
		return chain->kind == deluge::project::ChainAddress::Kind::SONG
		           ? deluge::project::ControlsAddress{.kind = Kind::SONG}
		           : deluge::project::ControlsAddress{.kind = Kind::OUTPUT, .id = chain->id};
	}
	return std::nullopt;
}

std::optional<deluge::project::ParamTarget> learn_target(const Node& node, const MenuContext& ctx) {
	// `PatchedParam`/`UnpatchedParam::getLearningThing`: the node's param, by its engine id.
	const std::optional<deluge::project::ControlsAddress> owner = controls_owner(ctx);
	if (!node.bind.param.valid() || !owner) {
		return std::nullopt;
	}
	return deluge::project::ParamTarget{.owner = *owner, .param = param_id_of(node.bind.param)};
}

// ---------------------------------------------------------------------------------------------
// The value scaling `get`/`set` items reuse (`bindings.h`'s note)

int32_t standard_to_menu(int32_t stored) {
	return deluge::scenes::bind::menu_of(deluge::engine::ParamDisplay::STANDARD, stored);
}
int32_t menu_to_standard(int32_t menu_value) {
	return deluge::scenes::bind::stored_of(deluge::engine::ParamDisplay::STANDARD, menu_value);
}
int32_t pan_to_menu(int32_t stored) {
	return deluge::scenes::bind::menu_of(deluge::engine::ParamDisplay::PAN, stored);
}
int32_t menu_to_pan(int32_t menu_value) {
	return deluge::scenes::bind::stored_of(deluge::engine::ParamDisplay::PAN, menu_value);
}
int32_t unsigned_to_menu(uint32_t stored) {
	// `computeCurrentValueForUnsignedMenuItem` (`value_scaling.cpp:18-20`).
	return ((int64_t)stored * kMaxMenuValue + 2147483648) >> 32;
}
uint32_t menu_to_unsigned(int32_t menu_value) {
	// `computeFinalValueForUnsignedMenuItem` (`value_scaling.cpp:60-62`).
	return (uint32_t)menu_value * 85899345;
}

// ---------------------------------------------------------------------------------------------
// The play direction (`gui/menu_item/sequence/direction.h`)

namespace {

/// @brief Where the play direction is: `sequence::Direction::getIndividualNoteRow` (`sequence/direction.h:29-52`),
///        minus the audition arm `bindings.h` documents as belonging to `NoteClipScene`. A kit row the clip is not on
///        affect-entire for has its own (`:31-37`), when it has a note row; anything else edits the clip's.
deluge::scenes::bind::Target direction_target(const MenuContext& ctx) {
	const bool row = !ctx.affect_entire && ctx.is_kit && ctx.addresses.lane.has_value();
	return {.kind = deluge::scenes::bind::Target::Kind::MODEL,
	        .path = "sequenceDirection",
	        .group = row ? deluge::project::SettingGroupId::NOTE_ROW : deluge::project::SettingGroupId::CLIP};
}

/// @brief The addresses the play direction resolves against: the kit row's note row is its lane's, whatever kind of
///        drum it plays.
deluge::scenes::bind::Addresses direction_where(const MenuContext& ctx) {
	deluge::scenes::bind::Addresses where = ctx.addresses;
	if (ctx.addresses.lane && ctx.addresses.clip) {
		where.note_row = deluge::project::NoteRowAddress{.clip_id = ctx.addresses.clip->clip_id,
		                                                 .is_kit = true,
		                                                 .row_key = static_cast<int32_t>(ctx.addresses.lane->drum_id)};
	}
	return where;
}

/// @brief Is @p ctx one this pair of functions can work on at all: an instrument clip of the song the app plays?
bool has_instrument_clip(const MenuContext& ctx) {
	return ctx.addresses.clip.has_value() && !ctx.is_audio_clip && ctx.model != nullptr && ctx.facts != nullptr;
}

// `getIndividualNoteRow`'s non-kit auditioned-row arm (`sequence/direction.h:50-60`),
// gated by `soundEditor.selectedNoteRow` in legacy rather than transcribed as a model read.
//
// **The equivalence argument, narrowed to what actually holds.** `selectedNoteRow` is
// latched at `Direction::checkPermissionToBeginSession`'s else-if (`sequence/direction.h:117-119`):
// `soundEditor.selectedNoteRow = isUIModeActive(UI_MODE_AUDITIONING);`, reached only when the
// output is not a kit, and read every session this item begins. What is true is narrower than "every
// other caller opens with `UI_MODE_NONE`": under `Root::NoteRowEditor` specifically, the latch is
// ALWAYS true, and nothing else can make it true from this port's own entry points:
//   * `InstrumentClipView::enterNoteRowEditor`'s own guard (`instrument_clip_view.cpp:3767`),
//     `getNumNoteRowsAuditioning() == 1`, requires one row already auditioning before it calls
//     `soundEditor.setup` -- the only caller that can open with `UI_MODE_AUDITIONING` already
//     active. A row only ever becomes "auditioning" through `InstrumentClipView::startAuditioningRow`
//     entering that UI mode itself (`instrument_clip_view.cpp:5518`) the moment a row's audition pad
//     goes down, so the one row counted by `getNumNoteRowsAuditioning() == 1` cannot exist without
//     that mode already being active.
//   * Once inside the note-row editor, `handleNoteRowEditorPadAction`'s sidebar arm
//     (`instrument_clip_view.cpp:3855-3865`) only ever calls `handleNoteRowEditorAuditionPadAction`
//     on a press (`if (on)`, `:3857`); a pad release is silently dropped rather than exiting the UI
//     mode, so `UI_MODE_AUDITIONING` never lapses for as long as the note-row editor stays open.
//   * The eight gated `openShellMenu` sites (`gui/ui/shell_menu_entry.cpp`'s table) all open with
//     `currentUIMode == UI_MODE_NONE` or `UI_MODE_NOTES_PRESSED` (site 7's arranger arm), and neither
//     of those is `UI_MODE_AUDITIONING`, so none of the eight can latch the flag true on entry.
// `ctx.root == Root::NoteRowEditor` on a non-kit clip is therefore `selectedNoteRow`'s exact
// port-side equivalent for every entry point this layer has.
//
// **What this does NOT cover, and is not attempting to: in-menu audition editing under `Root::Sound`
// / `Root::MidiOrCv`.** `sequenceDirectionMenu` also sits in the *regular* sound-editor roots
// (`soundEditorRootMenu`, `gui/ui/menus.cpp:1774`/`:1802`; `soundEditorRootMenuMIDIOrCV`, `:1952`/
// `:1964`), and `SoundEditor::padAction` forwards audition-pad presses to `instrumentClipView` while
// any *other* item of that root is focused (`gui/ui/sound_editor.cpp:1499-1503`, "allow user to
// interact with audition pads while in regular sound editor"). So a user can hold an audition pad,
// select-press down into Play Direction while still holding it, and land with `selectedNoteRow`
// latched true under `Root::Sound`/`Root::MidiOrCv` -- and `InstrumentClipView::
// potentiallyRefreshNoteRowMenu` (`instrument_clip_view.cpp:5551-5576`) then re-reads the item on
// every later audition press, letting the user switch which row Play Direction edits without leaving
// the menu. This is a real legacy behaviour this port does not build: nothing in `src/scenes` names
// `Root::Sound`/`Root::MidiOrCv` in this arm, and this comment records the gap rather than closing
// it.
bool note_row_bridge_direction(const MenuContext& ctx, int32_t* value) {
	return ctx.root == MenuContext::Root::NoteRowEditor && !ctx.is_kit && note_bridge() != nullptr
	       && note_bridge()->row_sequence_direction(value);
}

} // namespace

int32_t read_sequence_direction(const MenuContext& ctx, bool* from_note_row) {
	if (from_note_row != nullptr) {
		*from_note_row = false;
	}
	int32_t bridged = 0;
	if (note_row_bridge_direction(ctx, &bridged)) { // `:50-60`, through `NoteBridge`
		if (from_note_row != nullptr) {
			*from_note_row = true;
		}
		return bridged;
	}
	if (!has_instrument_clip(ctx)) {
		return 0;
	}
	const deluge::scenes::bind::Target target = direction_target(ctx); // `direction.h:70-75`
	if (from_note_row != nullptr) {
		*from_note_row = target.group == deluge::project::SettingGroupId::NOTE_ROW;
	}
	const std::optional<deluge::scenes::bind::View> shown =
	    deluge::scenes::bind::view(target, *ctx.model, direction_where(ctx), *ctx.facts);
	return shown ? shown->value : 0;
}

void write_sequence_direction(const MenuContext& ctx, int32_t value) {
	// This call is the GATE, not a redundant resolve-then-resolve-again --
	// `note_row_bridge_direction` answering `false` (no row, or not this root/clip shape) is exactly
	// what sends the write down to the kit-row arm and clip fallback below instead, so the value it
	// reads back is never used, only whether it answered at all. Calling it here and then having
	// `set_row_sequence_direction` resolve the row a second time (rather than threading the first
	// resolution through) is safe because `NoteBridge`'s row resolution is idempotent: on a non-kit
	// clip with no row yet, both calls reach the identical `createNoteRowForYDisplay` at the same
	// `lastAuditionedYDisplay` (`shell_note_bridge.cpp`'s `noteRowForSequenceDirection`), and the
	// second call finds the row the first one already created rather than making another.
	int32_t existing = 0;
	if (note_row_bridge_direction(ctx, &existing)) { // `:78-90`, through `NoteBridge`
		note_bridge()->set_row_sequence_direction(value);
		deluge::notify::raise(deluge::notify::Dirty::Oled);
		return;
	}
	if (!has_instrument_clip(ctx) || ctx.sink == nullptr) {
		return;
	}
	// `direction.h:83-89`: the kit row's note row, else the clip.
	const std::optional<deluge::scenes::bind::WriteRequest> request =
	    deluge::scenes::bind::request_for(direction_target(ctx), *ctx.model, direction_where(ctx), *ctx.facts, value);
	if (request) {
		(void)ctx.sink->write(*request);
	}
	deluge::notify::raise(deluge::notify::Dirty::Oled);
}

} // namespace deluge::scenes::menu::bindings
