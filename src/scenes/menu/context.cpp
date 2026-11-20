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
/// @brief The port of `SoundEditor::setup`'s context resolution and the common `checkPermissionToBeginSession` rule,
///        over the song's records and what the view says it opened over (`services::MenuView`). See `context.h`.
///
/// **What did NOT come across** (`setup()` is half context resolution, half UI
/// choreography, and only the first half belongs in `src/scenes`). UI side, not ported:
///
///   * `SoundEditor::setup`'s `getCurrentUI()`, `rootUIIsClipMinderScreen()`, `setupKitGlobalFXMenu`, `currentUIMode`
///     and `currentUI ==` comparisons, and `Buttons::isShiftButtonPressed()` -- reduced to `context_for`'s `bool`
///     arguments; the clip, its kit's selected row and affect-entire to its `MenuView`.
///   * The "select a row or affect entire" popup, the "parameter not applicable" popup and `display->cancelPopup()` --
///     every `display->` call.
///   * Retitling the shared `soundEditorRootMenuMIDIOrCV` singleton -- an artefact of one menu object serving MIDI
///     and CV; `Root::MidiOrCv` plus the instrument kind in `MenuContext::addresses` carries the same information as
///     data.
///   * `maybeGetParentMenu` / `focusChild` (horizontal-menu focusing), `multiRangeMenu.menuItemHeadingTo`, and
///     substituting the range picker for the chosen item -- navigation, which is `MenuScene`'s job.
///   * `allowsNoteTails` -- a `SoundEditor` render flag, not part of what is edited.
///   * `endScreen()`, `navigationDepth`, `shouldGoUpOneLevelOnBegin`, `menuItemNavigationRecord` -- `MenuState`, which
///     belongs to the interpreter.
///
/// **Not owned by this file** -- behaviour of `setup()` that is neither UI nor context, and that
/// will silently change if whoever opens a menu does not pick it up:
///
///   * `actionLogger.deleteAllLogs()` -- a mutation of undo history on every menu open.
///   * Remembering the previously selected `MultiRange` across sessions, and dropping it when the UI or the source
///     index changed. `MenuContext::range_index` is the field it feeds, from the scenes' view state; see
///     `permission_for`'s already-selected arm.
///   * `Source::getOrCreateFirstRange()` -- the legacy permission check *creates* a first
///     range (and can fail with `INSUFFICIENT_RAM`). `permission_for` is a pure predicate and does
///     not; whoever opens a range-dependent item must do the allocation.
///
/// **`currentSong` references in this file: 0.** Every place `setup()` reached for `currentSong` or a `getCurrent*()`
/// helper takes the `Song&` the caller passed to `context_for` (`MenuContext::song`) instead.

#include "scenes/menu/context.h"

#include "scenes/menu/bind_host.h"
#include "scenes/menu/bindings.h"

#include "scenes/menu/node.h"

#include "definitions_cxx.hpp" // kNumSources
#include "instruments/deluge/state_access.h"

#include "model/settings/runtime_feature_settings.h"
#include "model/song/song.h"

#include <array>
#include <iterator>
#include <span>

namespace deluge::scenes::menu {

namespace {

/// @brief The edited source's ranges, empty where the context edits no sound or no source.
std::span<const deluge::instrument::RangeState> edited_ranges(const MenuContext& ctx) {
	if (ctx.ports.sound == nullptr || !ctx.addresses.slot || ctx.source_index < 0 || ctx.source_index >= kNumSources) {
		return {};
	}
	const deluge::instrument::SoundState* state = ctx.ports.sound->sound_state(*ctx.addresses.slot);
	return state != nullptr
	           ? deluge::instrument::source_ranges(*state, static_cast<deluge::instrument::SourceId>(ctx.source_index))
	           : std::span<const deluge::instrument::RangeState>{};
}

} // namespace

int32_t range_count(const MenuContext& ctx) {
	// `std::ssize(source->ranges)`, the count main's `SoundEditor::setup` tests
	// (`origin/main:src/deluge/gui/ui/sound_editor.cpp`).
	return static_cast<int32_t>(edited_ranges(ctx).size());
}

std::vector<deluge::instrument::RangeState> ranges_of(const MenuContext& ctx) {
	const std::span<const deluge::instrument::RangeState> ranges = edited_ranges(ctx);
	return {ranges.begin(), ranges.end()};
}

int32_t range_top_note(const MenuContext& ctx, int32_t index) {
	if (index < 0 || index >= range_count(ctx)) {
		return deluge::instrument::kOpenTopNote; // `Source::getOrCreateFirstRange`'s "top of the keyboard"
	}
	return edited_ranges(ctx)[static_cast<size_t>(index)].top_note;
}

bool ensure_first_range(MenuContext& ctx) {
	if (!ctx.addresses.slot) {
		return true; // no sound: nothing is being edited that could want a range
	}
	if (range_count(ctx) == 0) {
		// `Source::getOrCreateFirstRange()`, as a request: the first range, which the app makes and the record holds. A
		// refusal is that call's INSUFFICIENT_RAM, which legacy answers with `MenuPermission::NO`.
		const std::array<deluge::instrument::RangeState, 1> first{deluge::instrument::RangeState{
		    .top_note = deluge::instrument::kOpenTopNote, .end_pos = deluge::instrument::kUnsetEndPos}};
		if (ctx.ports.sound_writes == nullptr
		    || !ctx.ports.sound_writes->write_ranges(*ctx.addresses.slot, static_cast<uint8_t>(ctx.source_index), first,
		                                             {})) {
			return false;
		}
		if (range_count(ctx) == 0) {
			return true; // a port that only records the request made nothing to point at
		}
	}
	// The same unambiguous cases `context_for` resolves, re-applied now that a range
	// is guaranteed to exist. A source that had none until a moment ago now has exactly one.
	if (ctx.is_kit || range_count(ctx) == 1) {
		ctx.range_index = 0;
	}
	return true;
}

void select_range(MenuContext& ctx, int32_t index) {
	if (index < 0 || index >= range_count(ctx)) {
		return;
	}
	// `SoundEditor::setCurrentMultiRange` (`origin/main:src/deluge/gui/ui/sound_editor.cpp`). The source's remembered
	// range is the picker's to write, on a flick (`MultiRangeItem::render`).
	ctx.range_index = static_cast<int16_t>(index);
}

void select_source(MenuContext& ctx, int8_t index) {
	if (!ctx.addresses.slot || index < 0 || index >= kNumSources) {
		return;
	}
	// The range reset in main's `SoundEditor::setup` (`origin/main:src/deluge/gui/ui/sound_editor.cpp`), folded in here
	// because this is the port of the one place legacy changes source index mid-menu: a range remembered for the
	// *other* source names a different sample entirely.
	//
	// NARROWED, deliberately: legacy's condition is a disjunction, and only the second half is ported. The first --
	// `currentUI != &soundEditor && currentUI != &sampleMarkerEditor` -- drops the remembered range whenever the menu
	// is being *entered* rather than navigated within, which it has to do because `currentMultiRange` is a
	// `SoundEditor` member that outlives every visit. `MenuContext` is not: `context_for` builds a fresh one per entry.
	// It is also a UI read this layer may not make (`scenes-no-root-ui`).
	if (ctx.source_index != index) {
		ctx.range_index = -1;
	}
	ctx.source_index = index;
	if (ctx.range_index < 0 && range_count(ctx) > 0) { // the first range, as `setup` selects it
		ctx.range_index = 0;
	}
}

MenuContext context_for(Song& song, const services::MenuView& view, int8_t source_index, bool performance_view,
                        bool session_or_arranger_root, bool session_view_root, bool kit_global_fx) {
	using bind::TrackKind;
	MenuContext ctx{};
	ctx.song = &song;
	ctx.source_index = source_index;
	// Legacy's `!rootUIIsClipMinderScreen()`: performance view is not a clip minder either, whatever its caller says of
	// the session and arranger.
	ctx.is_song_view = session_or_arranger_root || performance_view;
	// The narrower root-UI fact, `getRootUI() == &sessionView`: it steers no branch here, only
	// `relevance::is_session_view`.
	ctx.is_session_view = session_view_root;
	ctx.is_performance = performance_view;
	ctx.horizontal_menus = runtimeFeatureSettings.isOn(RuntimeFeatureSettingType::HorizontalMenus);
	ctx.shortcut_overlay = runtimeFeatureSettings.get(RuntimeFeatureSettingType::ShortcutOverlay)
	                       != static_cast<uint32_t>(RuntimeFeatureStateShortcutOverlay::Off);
	ctx.resolved = true;

	// --------------------------------------------------------------------------------------- Part 1: which sound or
	// chain (main's `SoundEditor::setup`, `origin/main:src/deluge/gui/ui/sound_editor.cpp`). Part 1 keys off the song
	// view FIRST and only then looks at the clip, while part 2 keys off the clip first: a song-view caller that also
	// passes a clip gets the song's chain with a clip root, as `setup()` does.
	// ---------------------------------------------------------------------------------------
	std::optional<project::SlotAddress> slot;
	std::optional<project::ChainAddress> chain;
	if (ctx.is_song_view) {
		// The song's own chain.
		chain = project::ChainAddress{.kind = project::ChainAddress::Kind::SONG};
	}
	else if (view.clip && view.track) {
		ctx.is_kit = view.kind == TrackKind::KIT;
		ctx.is_audio_clip = view.audio_clip;
		const uint32_t track = view.track->output_id;
		if (view.audio_clip) {
			// An audio track's chain.
			chain = project::ChainAddress{.kind = project::ChainAddress::Kind::OUTPUT, .id = track};
		}
		else if (ctx.is_kit) {
			ctx.non_audio_drum_row =
			    view.selected_row.has_value() && view.selected_row_type != project::DrumType::SOUND;
			ctx.gate_drum_row = view.selected_row.has_value() && view.selected_row_type == project::DrumType::GATE;
			// Affect-entire ("kit global FX"), which the caller says (`setupKitGlobalFXMenu`): the
			// kit's own chain. Not the clip's affect-entire: the kit-row grid shortcut enters with it on and edits the
			// row.
			if (kit_global_fx) {
				chain = project::ChainAddress{.kind = project::ChainAddress::Kind::OUTPUT, .id = track};
				ctx.is_kit_global_fx = true;
			}
			// A selected row: a sound row with its note row in the clip is a sound to edit, one without
			// is refused; a MIDI or gate row has an arpeggiator and nothing else.
			else if (view.selected_row) {
				ctx.is_drum = true;
				if (view.selected_row_type == project::DrumType::SOUND) {
					if (view.row_in_clip) {
						slot = project::SlotAddress{.kind = project::SlotAddress::Kind::DRUM, .id = *view.selected_row};
					}
					else {
						ctx.resolved = false;
					}
				}
			}
			// Neither affect-entire nor a selected row: nothing to edit.
			else {
				ctx.resolved = false;
			}
		}
		else if (view.kind == TrackKind::SYNTH) {
			slot = project::SlotAddress{.kind = project::SlotAddress::Kind::OUTPUT, .id = track};
		}
		// CV or MIDI: no sound, no chain.
	}

	// The kit clip's own affect-entire, whichever branch part 1 took (a synth clip's means nothing).
	ctx.affect_entire = ctx.is_kit && !ctx.is_audio_clip && view.affect_entire;

	// ---------------------------------------------------------------------------------------
	// Part 2: which root menu (the `item == nullptr` arm of `setup`). Legacy picks the kit roots from the clip's
	// affect-entire and edits what `setupKitGlobalFXMenu` says, and the two can disagree: the kit's own root is taken
	// when both say the kit, and otherwise the row's root by the selected drum's type, as legacy's arms run. The grid
	// shortcut (affect-entire on, the row edited) so lands on the row's root, which is where its item lives; with
	// affect-entire off and the fact set it is legacy's drum root over the kit's own effects. The note-editor roots
	// are the caller's (`NoteMode`).
	// ---------------------------------------------------------------------------------------
	if (view.clip) {
		if (!view.audio_clip) {
			if (view.kind == TrackKind::MIDI_OUT || view.kind == TrackKind::CV) {
				ctx.root = MenuContext::Root::MidiOrCv;
			}
			else if (ctx.is_kit && ctx.is_kit_global_fx && view.affect_entire) {
				ctx.root = MenuContext::Root::KitGlobalFx;
			}
			else if (ctx.is_kit && view.selected_row && view.selected_row_type == project::DrumType::MIDI) {
				ctx.root = MenuContext::Root::MidiDrum;
			}
			else if (ctx.is_kit && view.selected_row && view.selected_row_type == project::DrumType::GATE) {
				ctx.root = MenuContext::Root::GateDrum;
			}
			else {
				ctx.root = ctx.is_kit ? MenuContext::Root::Drum : MenuContext::Root::Sound;
			}
		}
		else {
			ctx.root = MenuContext::Root::AudioClip;
		}
	}
	else if (performance_view) {
		ctx.root = MenuContext::Root::Performance;
	}
	// The wide fact: the three song views share one root.
	else if (session_or_arranger_root) {
		ctx.root = MenuContext::Root::Song;
	}
	else {
		ctx.root = MenuContext::Root::Settings;
	}

	// What a target node binds through: the model, the addresses, and the app's ports, which act on the song
	// it plays: a context over any other song binds nothing through them.
	ctx.model = &song.project;
	if (host_binds(song)) {
		ctx.ports = host_ports();
	}
	// An unresolved context (a kit row with no note row) names nothing to edit.
	if (ctx.resolved && slot) {
		ctx.addresses.slot = slot;
	}
	if (ctx.resolved && ctx.is_kit && view.track) {
		ctx.addresses.kit_output = view.track->output_id;
	}

	// ---------------------------------------------------------------------------------------
	// Part 3: the range. `setup()` selects the first range once the item's permission is YES; with no
	// item to ask, it is selected where that rule is unambiguous (a kit row, or a single range), and left for the
	// range picker otherwise (`permission_for` then says `MustSelectRange`).
	// ---------------------------------------------------------------------------------------
	if (ctx.addresses.slot) {
		const int32_t ranges = range_count(ctx);
		if (ranges == 1 || (ctx.is_kit && ranges > 0)) {
			ctx.range_index = 0;
		}
	}

	// The records a model target edits: the clip and its track, a kit row's lane and (a sound row's) note row; and
	// what plays them, which the model does not hold but the view does.
	if (ctx.resolved && !ctx.is_song_view && view.clip && view.track) {
		ctx.addresses.clip = view.clip;
		ctx.addresses.track = view.track;
		using Instrument = deluge::plugin::SettingContext::Instrument;
		switch (view.kind) {
		case TrackKind::SYNTH:
			ctx.addresses.instrument = Instrument::SYNTH;
			break;
		case TrackKind::KIT:
			ctx.addresses.instrument = Instrument::KIT;
			break;
		case TrackKind::MIDI_OUT:
			ctx.addresses.instrument = Instrument::MIDI;
			break;
		case TrackKind::CV:
			ctx.addresses.instrument = Instrument::CV;
			break;
		case TrackKind::AUDIO:
			ctx.addresses.instrument = Instrument::AUDIO;
			break;
		}
		if (ctx.is_drum && view.selected_row) {
			ctx.addresses.lane = project::LaneAddress{view.track->output_id, *view.selected_row};
			if (view.selected_row_type == project::DrumType::SOUND) {
				ctx.addresses.note_row = project::NoteRowAddress{
				    .clip_id = view.clip->clip_id, .is_kit = true, .row_key = static_cast<int32_t>(*view.selected_row)};
			}
		}
	}
	// The chain an effect target edits when no sound is: the whole kit's, an audio track's or the master's.
	if (ctx.resolved && !ctx.addresses.slot && chain) {
		ctx.addresses.chain = chain;
		ctx.addresses.chain_owner = ctx.is_song_view    ? bind::ChainOwner::SONG
		                            : ctx.is_audio_clip ? bind::ChainOwner::AUDIO_TRACK
		                                                : bind::ChainOwner::KIT;
	}

	return ctx;
}

MenuContext range_rule_context(const Node& node, const MenuContext& ctx) {
	MenuContext own = ctx;
	const int8_t source = node.custom != nullptr ? node.custom->range_source() : int8_t{-1};
	if (source >= 0 && source != ctx.source_index && ctx.addresses.slot) {
		own.source_index = source;
		own.range_index = -1;
	}
	return own;
}

namespace {

/// @brief The context @p node's first range is made in and its picker test reads: @p ctx itself when the rule is
///        about the menu's own source, so the range it resolves stays the menu's; @p own, filled with
///        `range_rule_context`'s answer, when it is about another source.
/// @param node The range-specific item.
/// @param ctx  The menu's context.
/// @param own  Storage for the other source's context; must outlive the returned reference.
/// @return @p ctx or @p own.
MenuContext& range_rule_target(const Node& node, MenuContext& ctx, MenuContext& own) {
	own = range_rule_context(node, ctx);
	return own.source_index == ctx.source_index ? ctx : own;
}

} // namespace

Permission ensure_item_first_range(const Node& node, MenuContext& ctx) {
	if (!node.range_specific) {
		return Permission::Yes;
	}
	MenuContext own;
	MenuContext& rule_ctx = range_rule_target(node, ctx, own);
	if (!ensure_first_range(rule_ctx)) {
		return Permission::No;
	}
	// A context with no sound took the no-op success, and has no range to select.
	return (rule_ctx.addresses.slot && rule_ctx.range_index < 0) ? Permission::MustSelectRange : Permission::Yes;
}

Permission permission_for(const Node& node, const MenuContext& ctx) {
	// The base implementation, `MenuItem::checkPermissionToBeginSession`
	// (`origin/main:src/deluge/gui/menu_item/menu_item.cpp`): relevance, and nothing else. The `relevant`
	// hook is the port of the 112 `isRelevant` overrides the survey counts.
	if (!bindings::relevant(node, ctx)) {
		return Permission::No;
	}

	// The rule the range-specific overrides share, via
	// `SoundEditor::checkPermissionToBeginSessionForRangeSpecificParam` --
	// e.g. `menu_item::sample::Transpose::checkPermissionToBeginSession`
	// (`origin/main:src/deluge/gui/menu_item/sample/transpose.h`), which after its own relevance and osc-type test
	// defers to it verbatim.
	//
	// The discriminator is `Node::range_specific`, the items whose override ends in that call -- NOT "the node has a
	// `bind.param`". Everything else, an ordinary patched param on a multisampled sound included, takes the base rule
	// above and opens: legacy routes it through `MenuItem::checkPermissionToBeginSession`
	// (`origin/main:src/deluge/gui/menu_item/menu_item.cpp`), which returns YES without ever looking at a range.
	if (!node.range_specific || !ctx.addresses.slot) {
		return Permission::Yes;
	}
	// `main` asks about the item's own source (`whichThing`), which a per-oscillator item names itself.
	const MenuContext rule_ctx = range_rule_context(node, ctx);

	// A kit row is always range-unambiguous (one range per drum), and so is a source
	// with exactly one range. `context_for` has already pointed `ctx.range_index` at that range.
	if (rule_ctx.is_kit || range_count(rule_ctx) <= 1) {
		return Permission::Yes;
	}

	// A range is already selected for this source index (`ctx.source_index` is what
	// `context_for` resolved `ctx.range_index` against, so the legacy
	// `currentSourceIndex == whichThing` test holds by construction). Legacy's
	// `getCurrentUI() == &soundEditor` guard is dropped as a UI read; it is redundant here because
	// legacy already nulls the remembered range in `SoundEditor::setup` whenever the UI or source index
	// changed, which is the same condition.
	//
	// Unreachable for a context straight out of `context_for`: the only cases that set `ctx.range_index`
	// (kit row, single range) already returned `Yes` above. This is the entry point for a picker: once it
	// writes a chosen range back into the context, this is the arm that lets the item open.
	if (rule_ctx.range_index >= 0) {
		return Permission::Yes;
	}

	return Permission::MustSelectRange;
}

} // namespace deluge::scenes::menu
