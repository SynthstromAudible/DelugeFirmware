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
/// @brief The members of the two output items. See `output_items.h`.

#include "scenes/menu/nodes/output_items.h"

#include "scenes/bind/runtime_facts.h"
#include "scenes/bind/target.h"
#include "scenes/bind/write_sink.h"
#include "scenes/browser/browser_scene.h" // MidiDeviceLinkedItem's link arm opens this
#include "scenes/menu/context.h"
#include "scenes/menu/menu_scene.h"
#include "scenes/profile.h"

#include "ui/oled/text.h"

#include "gfx/oled/font.h"
#include "gfx/oled/icons.h" // MidiDeviceLinkedItem::row_icon -- the two checkbox icons
// `gfx/oled/font.h` above carries `string_width_px` -- `stringLengthPixels <= OLED_MAIN_WIDTH_PIXELS`

#include "definitions_cxx.hpp"
#include "deluge-project/setting_group.h"
#include "model/output.h"
#include "model/song/song.h"

#include <cstdint>
#include <cstdlib>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// `util/functions.h` is outside this layer's include allowance (`src/scenes/CMakeLists.txt`, and
// `scripts/check_layer_rules.py`'s `scenes-includes-outside`), so the one function needed is
// declared here at the global scope where it is defined (`util/functions.cpp:596
// getOutputTypeName`) -- the spelling `multi_range.cpp` uses for the three note-name tables.
char const* getOutputTypeName(OutputType outputType, int32_t channel);

namespace deluge::scenes::menu::nodes {

namespace {

/// @brief The pending browser id, as the `uint16_t` a `SceneId::arg` carries.
///
/// `output_items.h` builds `kMidiDeviceLinked` from a raw literal so that header does not drag
/// `scenes/browser/browser_scene.h` into every table that names the item; the literal is only
/// correct while `BrowserId` keeps its numbering, so it is pinned here, in the translation unit
/// that already sees both -- exactly what `sample.cpp` does for `MarkerType`.
static_assert(static_cast<uint16_t>(browser::BrowserId::MidiDeviceDefinition) == 1);

size_t depth_of(const MenuState& state) {
	return state.depth < kMenuDepth ? state.depth : 0;
}

/// @brief Draw the focused node's title, as `MenuItem::renderOLED` does before it calls
///        `drawPixelsForOled` (`gui/menu_item/menu_item.cpp:37-41`).
void draw_title_for(deluge::ui::oled::OledFrame& f, const MenuState& state) {
	const Node* node = state.depth < kMenuDepth ? state.path[state.depth] : nullptr;
	deluge::ui::shell::TextScratch scratch;
	deluge::ui::oled::title(f, node != nullptr ? deluge_profile().text_or_digits(node->title, scratch)
	                                           : std::string_view{});
}

/// @brief The track the menu's context names, as the song holds it; nullopt where it names none.
std::optional<bind::TrackFact> track_of(const MenuContext& ctx) {
	if (!ctx.addresses.track || ctx.facts == nullptr) {
		return std::nullopt;
	}
	for (const bind::TrackFact& track : ctx.facts->tracks()) {
		if (track.output_id == ctx.addresses.track->output_id) {
			return track;
		}
	}
	return std::nullopt;
}

/// @brief The TRACK row @p path (its name) of the track @p output_id, as the record holds it.
std::optional<serialization::Value> track_row(const MenuContext& ctx, uint32_t output_id, std::string_view path) {
	if (ctx.model == nullptr) {
		return std::nullopt;
	}
	return project::read_setting(
	    *ctx.model,
	    project::SettingAddress{.group = project::SettingGroupId::TRACK, .owner = project::TrackAddress{output_id}},
	    path);
}

/// @brief The MIDI track the menu edits, with the type test legacy leaves to `isRelevant`
///        (`midi/device_definition/linked.h:51-54`).
std::optional<bind::TrackFact> midi_track_of(const MenuContext& ctx) {
	const std::optional<bind::TrackFact> track = track_of(ctx);
	return track && track->kind == bind::TrackKind::MIDI_OUT ? track : std::nullopt;
}

// ---------------------------------------------------------------------------------------------
// `SpecificSourceOutputSelector`'s four private helpers, as free functions over the context.
//
// Legacy keeps `audioOutputBeingEdited`, `outputIndex` and `numOutputs` as members of a singleton
// item (`specific_output_source_selector.h:109-112`). A `CustomItem` is `const` and shared, so the
// first is re-resolved from the context on every call, the second lives in
// `MenuState::list[depth].cursor` (`SourceSelectionItem`'s reason, `nodes/patch_cables.h`) and the
// third is asked of the song each time -- which is strictly fresher than legacy's snapshot, taken
// once in `beginSession` and then used by every subsequent `selectEncoderAction` (`:32`, `:155`).
// An index is a position in the song's track list ("only outputs still in the main song list are
// selectable; hibernated instruments should not display here", `:122`).

/// @brief The audio track being edited, with the type test legacy leaves to `isRelevant`
///        (`specific_output_source_selector.h:102-105`) -- here `relevance::audio_source_is_specific`.
std::optional<uint32_t> editing_of(const MenuContext& ctx) {
	const std::optional<bind::TrackFact> track = track_of(ctx);
	return track && track->kind == bind::TrackKind::AUDIO ? std::optional{track->output_id} : std::nullopt;
}

/// @brief The track the audio track records from, by id; nullopt for none.
std::optional<uint32_t> recording_from_of(const MenuContext& ctx, uint32_t editing) {
	if (ctx.model == nullptr) {
		return std::nullopt;
	}
	const std::optional<serialization::Value> id = project::read_setting(
	    *ctx.model,
	    project::SettingAddress{.group = project::SettingGroupId::AUDIO_INPUT, .owner = project::TrackAddress{editing}},
	    "outputRecordingFrom");
	const int64_t value = id ? std::strtoll(id->str().c_str(), nullptr, 10) : -1;
	return value >= 0 ? std::optional{static_cast<uint32_t>(value)} : std::nullopt;
}

/// @brief `getRecordableOutputIndex` (`:117-132`): @p out's position in the song's track list, or -1 if it is not
///        there or cannot be recorded from.
int32_t recordable_index(std::span<const bind::TrackFact> tracks, std::optional<uint32_t> editing,
                         std::optional<uint32_t> out) {
	if (!editing || !out) {
		return -1;
	}
	for (size_t i = 0; i < tracks.size(); i++) {
		if (tracks[i].output_id == *out) {
			return bind::can_record_from(*editing, tracks[i]) ? static_cast<int32_t>(i) : -1;
		}
	}
	return -1;
}

/// @brief `getOutputFromSelectedIndex` (`:134-137`): the track at @p index, or null when the audio track cannot
///        record from it.
const bind::TrackFact* track_at(std::span<const bind::TrackFact> tracks, std::optional<uint32_t> editing,
                                int32_t index) {
	if (!editing || index < 0 || static_cast<size_t>(index) >= tracks.size()) {
		return nullptr;
	}
	return bind::can_record_from(*editing, tracks[index]) ? &tracks[index] : nullptr;
}

/// @brief `getNextRecordableOutputIndex` (`:139-168`): walk @p offset detents from @p start,
///        stopping only on rows the audio track can record from and stopping dead at either end.
int32_t next_recordable_index(const MenuContext& ctx, std::span<const bind::TrackFact> tracks,
                              std::optional<uint32_t> editing, int32_t start, int32_t offset) {
	if (offset == 0) {
		// `:141-143`. Legacy reads its `outputIndex` member here rather than `startIndex`; the only
		// caller passes the member as `startIndex`, so the two are the same value.
		return track_at(tracks, editing, start) != nullptr
		           ? start
		           : recordable_index(tracks, editing, editing ? recording_from_of(ctx, *editing) : std::nullopt);
	}

	const auto num_outputs = static_cast<int32_t>(tracks.size());
	const int32_t direction = offset > 0 ? 1 : -1;
	const int32_t steps = offset > 0 ? offset : -offset;
	int32_t selected = start;

	for (int32_t step = 0; step < steps; step++) {
		int32_t candidate_index = selected;
		while (true) {
			candidate_index += direction;
			if (candidate_index < 0 || candidate_index >= num_outputs) {
				return selected;
			}
			if (track_at(tracks, editing, candidate_index) != nullptr) {
				selected = candidate_index;
				break;
			}
		}
	}
	return selected;
}

/// @brief `getOutputTypeName(outputType, channel)` for @p track, with legacy's own channel arm
///        (`specific_output_source_selector.h:70-79`).
///
/// @note That arm is **unreachable today**, and is reproduced rather than dropped so it stays
///       correct if it ever becomes reachable: the only tracks that get here can be recorded from,
///       which already excludes MIDI and CV. The same filter is why an audio source is labelled from
///       channel 0 -- i.e. always "Audio Player" (`util/functions.cpp:614-626`) whatever its mode.
///       Legacy has the identical behaviour; it is a fidelity result, not a port decision.
std::string_view output_type_text(const bind::TrackFact& track) {
	OutputType type = OutputType::SYNTH;
	switch (track.kind) {
	case bind::TrackKind::KIT:
		type = OutputType::KIT;
		break;
	case bind::TrackKind::MIDI_OUT:
		type = OutputType::MIDI_OUT;
		break;
	case bind::TrackKind::CV:
		type = OutputType::CV;
		break;
	case bind::TrackKind::AUDIO:
		type = OutputType::AUDIO;
		break;
	case bind::TrackKind::SYNTH:
		break;
	}
	const char* text = getOutputTypeName(type, track.kind == bind::TrackKind::MIDI_OUT ? track.channel : 0);
	return text != nullptr ? std::string_view{text} : std::string_view{};
}

/// @brief Write the track the audio track records from (or none) through the menu's sink.
void write_recording_from(const MenuContext& ctx, const bind::TrackFact* from) {
	if (ctx.sink == nullptr) {
		return;
	}
	(void)ctx.sink->write({.target = {.kind = bind::Target::Kind::MODEL,
	                                  .path = "outputRecordingFrom",
	                                  .group = project::SettingGroupId::AUDIO_INPUT},
	                       .where = ctx.addresses,
	                       .menu_value = from != nullptr ? static_cast<int32_t>(from->output_id) : -1});
}

} // namespace

// ---------------------------------------------------------------------------------------------
// `MidiDeviceLinkedItem` -- `midi::device_definition::Linked`

bool MidiDeviceLinkedItem::linked(const MenuContext& ctx) {
	// `readCurrentValue` (`linked.h:33-36`).
	const std::optional<bind::TrackFact> track = midi_track_of(ctx);
	const std::optional<serialization::Value> file =
	    track && ctx.model != nullptr
	        ? project::read_setting(*ctx.model,
	                                project::SettingAddress{.group = project::SettingGroupId::INSTRUMENT_DEVICE,
	                                                        .owner = project::TrackAddress{track->output_id}},
	                                "deviceDefinitionFileName")
	        : std::nullopt;
	return file && !file->str().empty();
}

const deluge::gfx::oled::Icon* MidiDeviceLinkedItem::row_icon(const MenuContext& ctx) const {
	// `renderSubmenuItemTypeForOled` (`linked.h:56-78`), icon half: `getToggleValue()` is
	// `readCurrentValue`'s answer, which `linked()` above already is.
	return linked(ctx) ? &deluge::gfx::oled::icons::checkedBox : &deluge::gfx::oled::icons::uncheckedBox;
}

void MidiDeviceLinkedItem::on_focus(const MenuContext& ctx, MenuState& state) const {
	// `Toggle::selectButtonPress` -> `toggleValue()` (`gui/menu_item/toggle.h:25-29`, `:33-44`):
	// read the current value, invert it, write it. The write is `Linked::writeCurrentValue`
	// (`linked.h:37-49`), whose two arms are the two below. `CustomItem::on_focus` IS
	// `beginSession` (`node.h`), and a descent into this item is what the press that legacy answered
	// without navigating turns into here (`output_items.h`, and `nodes/actions.h` for the shape).
	if (linked(ctx)) {
		// Unlinking (`:45-48`): the new value is false, so just clear the file name. No UI is
		// opened, so nothing is filed and `render` is what takes the user back out.
		if (midi_track_of(ctx) && ctx.sink != nullptr) {
			(void)ctx.sink->write_text({.kind = bind::Target::Kind::MODEL,
			                            .path = "deviceDefinitionFileName",
			                            .group = project::SettingGroupId::INSTRUMENT_DEVICE},
			                           ctx.addresses, "");
		}
		return;
	}
	// Linking (`:41-43`): `openUI(&loadMidiDeviceDefinitionUI)`. The descriptor for that browser
	// already ships complete -- title, XML extensions, `MIDI_DEVICES/DEFINITION`, both icon pieces,
	// its `can`, its `start_at` and an `accept` that performs the real load
	// (`browser::BrowserId::MidiDeviceDefinition`, `scenes/browser/browser_scene.cpp`) -- so this is
	// the whole of the wiring it was waiting for.
	//
	// A scene may not touch the `Shell` stack itself, so the push is recorded in
	// `MenuState::pending_scene` and `MenuScene::after_render` turns it into an
	// `Action::Kind::Push`, setting `go_up_on_resume` beside it. Legacy does not set
	// `shouldGoUpOneLevelOnBegin` here -- it never descended into the item in the first place -- so
	// the two agree on where the user lands when the browser closes: the parent list.
	//
	// **Not ported:** `LoadMidiDeviceDefinitionUI` refuses to open unless the root UI is a clip
	// minder as well (`load_midi_device_definition_ui.cpp:45-47`); that conjunct is a root-UI read
	// this layer may not make, and `BrowserScene`'s `can` keeps the half that discriminates. The
	// browser's own `accept` performs the load, so unlike the sample side there is no
	// `take_choice()` for a dispatcher to drain.
	browser::browser_scene().open(ctx);
	state.pending_scene = deluge::ui::shell::SceneId{browser::kBrowserSceneKind, browser_arg_};
}

void MidiDeviceLinkedItem::render(deluge::ui::oled::OledFrame& f, const MenuContext& /*ctx*/, MenuState& state) const {
	// Reached on the **unlink** arm only. On the link arm `after_render` files the `Push` at the end
	// of the very tick that focused this item, and `BrowserScene` is not `oled_transparent`, so the
	// menu never renders again until the browser closes -- by which time `go_up_on_resume` has
	// already stepped back out.
	//
	// Legacy draws no page for this item at all (`Toggle` is toggled from the parent list, never
	// entered), so the title is the whole of it -- `ActionItem::render`'s reasoning
	// (`nodes/actions.cpp`), and what `PendingCustom::render` drew before this item existed.
	draw_title_for(f, state);

	// `Toggle::selectButtonPress` returns `NO_NAVIGATION` (`toggle.h:42`): the user is left on the
	// parent list. `after_render`'s leaf arm turns `entered` into `ascend()`, which is how a
	// `CustomItem` that legacy never entered leaves again. Legacy never draws this item at all, so
	// the port's one frame of its own title before it leaves is a difference -- the same one
	// `ResetItem` already ships, and stated for the same reason.
	//
	// Set unconditionally rather than once, which matters only at **depth 0** -- this node opened as
	// a root through `SceneId::arg` or `open()`. There `on_focus` was skipped entirely
	// (`FocusCause::NoRowPress` plus `on_focus_is_press`, see the class note), the title is simply the
	// screen, and this drives a no-op `ascend()` every frame: `ascend` returns immediately at depth
	// 0 (`menu_scene.cpp`). Harmless and cheap, but not accidental.
	state.render_resp.entered = true;
}

deluge::ui::shell::Result MidiDeviceLinkedItem::handle(const deluge::ui::shell::UiEvent& /*event*/,
                                                       const deluge::ui::shell::UiContext& /*ui_ctx*/,
                                                       const MenuContext& /*ctx*/, MenuState& /*state*/) const {
	// Focused for at most one tick, and nothing on that screen is interactive.
	return {};
}

// ---------------------------------------------------------------------------------------------
// `SpecificOutputItem` -- `audio_clip::SpecificSourceOutputSelector`

void SpecificOutputItem::on_focus(const MenuContext& ctx, MenuState& state) const {
	// `beginSession` (`specific_output_source_selector.h:30-47`).
	const size_t d = depth_of(state);
	state.list[d] = deluge::ui::ListState{};
	state.scroll[d] = deluge::ui::ScrollText{};

	const std::optional<uint32_t> editing = editing_of(ctx);
	const std::vector<bind::TrackFact> tracks =
	    ctx.facts != nullptr ? ctx.facts->tracks() : std::vector<bind::TrackFact>{};
	int32_t index = recordable_index(tracks, editing, editing ? recording_from_of(ctx, *editing) : std::nullopt);
	if (index < 0) {
		// `:36-41` -- "if the stored source was removed or became invalid, land on the first valid
		// target instead", and write that choice straight back to the model. A model write from
		// `on_focus` is legacy's own (`beginSession` does exactly this).
		index = next_recordable_index(ctx, tracks, editing, -1, 1);
		if (editing) {
			write_recording_from(ctx, track_at(tracks, editing, index));
		}
	}
	if (index < 0) {
		index = 0; // `:43-45`
	}
	state.list[d].cursor = static_cast<int16_t>(index);
}

void SpecificOutputItem::render(deluge::ui::oled::OledFrame& f, const MenuContext& ctx, MenuState& state) const {
	draw_title_for(f, state);

	const size_t d = depth_of(state);
	const std::optional<uint32_t> editing = editing_of(ctx);
	const std::vector<bind::TrackFact> tracks =
	    ctx.facts != nullptr ? ctx.facts->tracks() : std::vector<bind::TrackFact>{};

	// `selectEncoderAction` (`:49-58`). Done here rather than in `handle` for
	// `SourceSelectionItem`'s reason: the select-encoder turn is a widget-owned shape delivered
	// before `after_render`, so reading it from the render is what keeps the page in step with the
	// encoder instead of one tick behind it (`browser_scene.h` §5.7 records the same finding).
	if (const int8_t turn = f.turn(); turn != 0) {
		const int32_t next = next_recordable_index(ctx, tracks, editing, state.list[d].cursor, turn);
		if (next >= 0) { // `:51-53`
			state.list[d].cursor = static_cast<int16_t>(next);
			// `:56` -- `setOutputRecordingFrom(...)`. A render may not write the model
			// (`menu_scene.h`), so the write is recorded and `commit()` performs it, which is where
			// every other kind's write happens too. Filed on every turn legacy would have written
			// on, including one that hit an end and moved nothing: legacy re-writes the same output
			// there, and `setOutputRecordingFrom` releasing and re-taking its own monitoring claim
			// is not a difference this port should invent.
			state.value_changed = true;
			state.value_node = state.depth < kMenuDepth ? state.path[state.depth] : nullptr;
		}
	}

	// `drawPixelsForOled` (`:59-100`), laid out at legacy's own offsets from the top of the panel.
	//
	// **`ui::oled::output_name` is deliberately not used**, even though it is the port of the same
	// two-line output display (`ui/oled/misc.h`). Its rows are `View::drawOutputNameFromDetails`'s
	// (12 and 30 on a 64-row panel), and that display has no screen title above it; this item does
	// (`MenuItem::renderOLED`, `gui/menu_item/menu_item.cpp:37-41`), and at row 12 the type label
	// would collide with the title rule the profile puts at `menu_base_y`. Legacy's own numbers --
	// 14 and 28, with 21 for the empty case -- clear it, so they are what this draws.
	//
	// They are also unconditional, on both panel geometries, because legacy's are: this item is
	// newer code than `drawOutputNameFromDetails` and carries no
	// `#if OLED_MAIN_HEIGHT_PIXELS == 64` pair to select between. Row 28 plus the 9-row title face
	// still fits a 48-row panel, so nothing is clipped.
	const deluge::gfx::oled::Rect area = f.bounds();
	const deluge::gfx::oled::FontId type_font = f.style.row_font;   // kTextSpacingX / kTextSpacingY
	const deluge::gfx::oled::FontId name_font = f.style.title_font; // kTextTitleSpacingX / kTextTitleSizeY
	const int32_t type_h = deluge::ui::oled::text_height_for(type_font);
	const int32_t name_h = deluge::ui::oled::text_height_for(name_font);

	const bind::TrackFact* out = track_at(tracks, editing, state.list[d].cursor);
	if (out == nullptr) {
		// `:64-67`. Not an l10n id: legacy's literal is not in `gui/l10n/strings.h`, and inventing
		// one would be a change to the l10n data rather than a port (`browser_scene.h`'s
		// `Descriptor::title` makes the same call).
		deluge::ui::oled::text(
		    f, deluge::gfx::oled::Rect{area.x, area.y + 21, area.w, type_h}, "No track",
		    deluge::ui::oled::TextStyle{.font = type_font, .align = deluge::ui::oled::Align::Centre});
	}
	else {
		deluge::ui::oled::text(
		    f, deluge::gfx::oled::Rect{area.x, area.y + 14, area.w, type_h}, output_type_text(*out),
		    deluge::ui::oled::TextStyle{.font = type_font, .align = deluge::ui::oled::Align::Centre});
		// `:87-99` -- centred when it fits, left-aligned when it does not (the side-scroller legacy
		// arms in that case has no port; see `output_items.h`).
		const std::optional<serialization::Value> named = track_row(ctx, out->output_id, "name");
		const std::string name_text = named ? named->str() : std::string{};
		const std::string_view name{name_text};
		const bool fits = deluge::gfx::oled::string_width_px(name, name_h) <= area.w;
		deluge::ui::oled::text(f, deluge::gfx::oled::Rect{area.x, area.y + 28, area.w, name_h}, name,
		                       deluge::ui::oled::TextStyle{.font = name_font,
		                                                   .align = fits ? deluge::ui::oled::Align::Centre
		                                                                 : deluge::ui::oled::Align::Left});
	}

	// `MenuItem::selectButtonPress`'s default is `nullptr` -- "go up a level" -- and this class does
	// not override it. Nothing else here claims the press, so it has to be said explicitly; without
	// it the page could only be left with BACK.
	if (f.select_pressed()) {
		state.render_resp.entered = true;
	}
}

void SpecificOutputItem::commit(const MenuContext& ctx, MenuState& state) const {
	// `selectEncoderAction`'s `:55-56`, in the one place a write may happen. The cursor cannot have
	// moved between the render that set `value_changed` and this call -- `after_render` runs
	// immediately after, with no input in between (`ActiveScalesItem::commit`'s note).
	const std::optional<uint32_t> editing = editing_of(ctx);
	if (!editing || ctx.facts == nullptr) {
		return;
	}
	const std::vector<bind::TrackFact> tracks = ctx.facts->tracks();
	write_recording_from(ctx, track_at(tracks, editing, state.list[depth_of(state)].cursor));
}

deluge::ui::shell::Result SpecificOutputItem::handle(const deluge::ui::shell::UiEvent& /*event*/,
                                                     const deluge::ui::shell::UiContext& /*ui_ctx*/,
                                                     const MenuContext& /*ctx*/, MenuState& /*state*/) const {
	// The turn and the select press are both read from the render (see above); this page has no
	// pad or button input of its own, so nothing is claimed here.
	return {};
}

} // namespace deluge::scenes::menu::nodes
