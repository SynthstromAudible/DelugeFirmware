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
/// **What did NOT come across** (task-3 ruling 2 -- `setup()` is half context resolution, half UI
/// choreography, and only the first half belongs in `src/scenes`). UI side, not ported:
///
///   * `:1684` `getCurrentUI()`, `:1687` `rootUIIsClipMinderScreen()`, `:1709`
///     `setupKitGlobalFXMenu`, `:1774`/`:1777` `currentUIMode`, `:1816`/`:1819` the
///     `currentUI ==` comparisons and `Buttons::isShiftButtonPressed()` -- reduced to
///     `context_for`'s `bool` arguments; the clip, its kit's selected row and affect-entire to its `MenuView`.
///   * `:1734-1737` the "select a row or affect entire" popup, `:1849` the "parameter not
///     applicable" popup, `:1864` `display->cancelPopup()` -- every `display->` call.
///   * `:1781`/`:1786` retitling the shared `soundEditorRootMenuMIDIOrCV` singleton -- an artefact
///     of one menu object serving MIDI and CV; `Root::MidiOrCv` plus the instrument kind in
///     `MenuContext::addresses` carries the same information as data.
///   * `:1836-1837`/`:1860-1862` `maybeGetParentMenu` / `focusChild` (horizontal-menu focusing),
///     `:1857` `multiRangeMenu.menuItemHeadingTo`, `:1858` substituting the range picker for the
///     chosen item -- navigation, which is `MenuScene`'s job (spec E §4.1).
///   * `:1866-1871` `allowsNoteTails` -- a `SoundEditor` render flag, not part of what is edited.
///   * `:1899-1904` `endScreen()`, `navigationDepth`, `shouldGoUpOneLevelOnBegin`,
///     `menuItemNavigationRecord` -- `MenuState`, which belongs to the interpreter.
///
/// **Owed to E2** -- behaviour of `setup()` that is neither UI nor context, has no owner yet, and
/// will silently change if nothing picks it up when `MenuScene` lands (fix round 1, Minor 5):
///
///   * `:1771` `actionLogger.deleteAllLogs()` -- a mutation of undo history on every menu open.
///   * `:1839-1843` remembering the previously selected `MultiRange` across sessions, and dropping
///     it when the UI or the source index changed. `MenuContext::range_index` is the field it feeds,
///     from the scenes' view state; see `permission_for`'s already-selected arm.
///   * `:1993` `Source::getOrCreateFirstRange()` -- the legacy permission check *creates* a first
///     range (and can fail with `INSUFFICIENT_RAM`). `permission_for` is a pure predicate and does
///     not; whoever opens a range-dependent item must do the allocation.
///
/// **`currentSong` references in this file: 0.** Every place `setup()` reached for `currentSong` or a `getCurrent*()`
/// helper takes the `Song&` the caller passed to `context_for` (`MenuContext::song`) instead.

#include "scenes/menu/context.h"

#include "scenes/bind/write_sink.h"
#include "scenes/menu/bind_host.h"
#include "scenes/menu/bindings.h"

#include "scenes/menu/node.h"
#include "scenes/services/view_state.h"

#include "model/settings/runtime_feature_settings.h"
#include "model/song/song.h"

#include <array>
#include <iterator>

namespace deluge::scenes::menu {

namespace {

/// @brief The edited source's record, or null where the context edits no sound.
const deluge::instrument::SourceState* source_state(const MenuContext& ctx) {
	if (ctx.facts == nullptr || !ctx.addresses.slot || ctx.source_index < 0 || ctx.source_index >= kNumSources) {
		return nullptr;
	}
	const deluge::instrument::SoundState* state = ctx.facts->sound_state(*ctx.addresses.slot);
	return state != nullptr ? &state->sources[ctx.source_index] : nullptr;
}

} // namespace

int32_t range_count(const MenuContext& ctx) {
	// `std::ssize(source->ranges)` at `:2005`.
	const deluge::instrument::SourceState* source = source_state(ctx);
	return source != nullptr ? static_cast<int32_t>(source->ranges.size()) : 0;
}

int32_t range_top_note(const MenuContext& ctx, int32_t index) {
	if (index < 0 || index >= range_count(ctx)) {
		return deluge::instrument::kOpenTopNote; // `Source::getOrCreateFirstRange`'s "top of the keyboard"
	}
	return source_state(ctx)->ranges[static_cast<size_t>(index)].top_note;
}

bool ensure_first_range(MenuContext& ctx) {
	if (!ctx.addresses.slot) {
		return true; // no sound: nothing is being edited that could want a range
	}
	if (range_count(ctx) == 0) {
		// `:1993`, as a request: the first range, which the app makes and the record holds. A refusal is
		// `:1994-1997`'s INSUFFICIENT_RAM, which legacy answers with `MenuPermission::NO`.
		const std::array<deluge::instrument::RangeState, 1> first{deluge::instrument::RangeState{
		    .top_note = deluge::instrument::kOpenTopNote, .end_pos = deluge::instrument::kUnsetEndPos}};
		if (ctx.sink == nullptr
		    || !ctx.sink->write_ranges(*ctx.addresses.slot, static_cast<uint8_t>(ctx.source_index), first)) {
			return false;
		}
		if (range_count(ctx) == 0) {
			return true; // a sink that only records the request made nothing to point at
		}
	}
	// `:2005-2007` -- the same unambiguous cases `context_for` resolves, re-applied now that a range
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
	// `SoundEditor::setCurrentMultiRange` (`:1983-1986`).
	ctx.range_index = static_cast<int16_t>(index);
	// `MultiRange::selectEncoderAction`'s own `defaultRangeI` write (`multi_range.cpp:239`), which is what makes the
	// choice stick across a later visit to the same source: the scenes' view state, by slot and source.
	if (ctx.model != nullptr) {
		services::view_state().forget_unless(ctx.model->identity());
	}
	services::view_state().set_last_range(*ctx.addresses.slot, static_cast<uint8_t>(ctx.source_index),
	                                      static_cast<uint16_t>(index));
}

void select_source(MenuContext& ctx, int8_t index) {
	if (!ctx.addresses.slot || index < 0 || index >= kNumSources) {
		return;
	}
	// `SoundEditor::setup`'s `:1841-1843`, folded in here because this is the port of the one place legacy changes
	// source index mid-menu: a range remembered for the *other* source names a different sample entirely.
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
	if (ctx.range_index < 0 && range_count(ctx) > 0) { // `:247-249`
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
	ctx.resolved = true;

	// ---------------------------------------------------------------------------------------
	// Part 1: which sound or chain (`:1686-1762`). Part 1 keys off the song view FIRST and only then looks at the
	// clip, while part 2 keys off the clip first: a song-view caller that also passes a clip gets the song's chain
	// with a clip root, as `setup()` does.
	// ---------------------------------------------------------------------------------------
	std::optional<project::SlotAddress> slot;
	std::optional<project::ChainAddress> chain;
	if (ctx.is_song_view) {
		// `:1686-1695` -- the song's own chain.
		chain = project::ChainAddress{.kind = project::ChainAddress::Kind::SONG};
	}
	else if (view.clip && view.track) {
		ctx.is_kit = view.kind == TrackKind::KIT;
		ctx.is_audio_clip = view.audio_clip;
		const uint32_t track = view.track->output_id;
		if (view.audio_clip) {
			// `:1757-1761` -- an audio track's chain.
			chain = project::ChainAddress{.kind = project::ChainAddress::Kind::OUTPUT, .id = track};
		}
		else if (ctx.is_kit) {
			// `:1708-1713` -- affect-entire ("kit global FX"), which the caller says (`setupKitGlobalFXMenu`): the
			// kit's own chain. Not the clip's affect-entire: the kit-row grid shortcut enters with it on and edits the
			// row.
			if (kit_global_fx) {
				chain = project::ChainAddress{.kind = project::ChainAddress::Kind::OUTPUT, .id = track};
				ctx.is_kit_global_fx = true;
			}
			// `:1715-1730` -- a selected row: a sound row with its note row in the clip is a sound to edit, one without
			// is refused (`:1719-1721`); a MIDI or gate row has an arpeggiator and nothing else.
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
			// `:1732-1739` -- neither affect-entire nor a selected row: nothing to edit.
			else {
				ctx.resolved = false;
			}
		}
		else if (view.kind == TrackKind::SYNTH) {
			slot = project::SlotAddress{.kind = project::SlotAddress::Kind::OUTPUT, .id = track};
		}
		// `:1750` -- CV or MIDI: no sound, no chain.
	}

	// The kit clip's own affect-entire, whichever branch part 1 took (a synth clip's means nothing).
	ctx.affect_entire = ctx.is_kit && !ctx.is_audio_clip && view.affect_entire;

	// ---------------------------------------------------------------------------------------
	// Part 2: which root menu (`:1769-1827`, the `item == nullptr` arm). The kit arms read `is_kit_global_fx` where
	// legacy reads the clip's affect-entire: the same fact wherever no item is passed, and on the grid shortcut it
	// keeps the root consistent with what part 1 edits. The note-editor roots are the caller's (`NoteMode`).
	// ---------------------------------------------------------------------------------------
	if (view.clip) { // `:1770`
		if (!view.audio_clip) {
			if (view.kind == TrackKind::MIDI_OUT || view.kind == TrackKind::CV) { // `:1780-1788`
				ctx.root = MenuContext::Root::MidiOrCv;
			}
			else if (ctx.is_kit && ctx.is_kit_global_fx) { // `:1790-1792`
				ctx.root = MenuContext::Root::KitGlobalFx;
			}
			else if (ctx.is_kit && ctx.is_drum && view.selected_row_type == project::DrumType::MIDI) { // `:1794-1798`
				ctx.root = MenuContext::Root::MidiDrum;
			}
			else if (ctx.is_kit && ctx.is_drum && view.selected_row_type == project::DrumType::GATE) { // `:1800-1804`
				ctx.root = MenuContext::Root::GateDrum;
			}
			else { // `:1806-1808`
				ctx.root = ctx.is_kit ? MenuContext::Root::Drum : MenuContext::Root::Sound;
			}
		}
		else { // `:1811-1813`
			ctx.root = MenuContext::Root::AudioClip;
		}
	}
	else if (performance_view) { // `:1816-1818`
		ctx.root = MenuContext::Root::Performance;
	}
	// `:1819-1822` -- the wide fact: the three song views share one root.
	else if (session_or_arranger_root) {
		ctx.root = MenuContext::Root::Song;
	}
	else { // `:1823-1825`
		ctx.root = MenuContext::Root::Settings;
	}

	// What a target node binds through: the model, the addresses, and the app's facts and sink, which act on the song
	// it plays: a context over any other song binds nothing through them.
	ctx.model = &song.project;
	if (host_binds(song)) {
		ctx.facts = &host_runtime_facts();
		ctx.sink = &host_write_sink();
	}
	// An unresolved context (a kit row with no note row) names nothing to edit.
	if (ctx.resolved && slot) {
		ctx.addresses.slot = slot;
	}
	if (ctx.resolved && ctx.is_kit && view.track) {
		ctx.addresses.kit_output = view.track->output_id;
	}

	// ---------------------------------------------------------------------------------------
	// Part 3: the range (`:1873-1897`). `setup()` selects the first range once the item's permission is YES; with no
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

Permission permission_for(const Node& node, const MenuContext& ctx) {
	// The base implementation, `MenuItem::checkPermissionToBeginSession`
	// (`gui/menu_item/menu_item.cpp:27-31`): relevance, and nothing else. Spec E §4's `relevant`
	// hook is the port of the 112 `isRelevant` overrides the survey counts.
	if (!bindings::relevant(node, ctx)) {
		return Permission::No;
	}

	// The rule the range-specific overrides share, via
	// `SoundEditor::checkPermissionToBeginSessionForRangeSpecificParam` (`sound_editor.cpp:1988`) --
	// e.g. `menu_item::sample::Transpose::checkPermissionToBeginSession`
	// (`gui/menu_item/sample/transpose.h:120-135`), which after its own relevance and osc-type test
	// defers to it verbatim.
	//
	// The discriminator is `Node::range_dependent`, the port of `MenuItem::isRangeDependent()`
	// (`gui/menu_item/menu_item.h:202`; true at `sample/transpose.h:137`, `sample/loop_point.h:31`,
	// `multi_range.h:32`) -- NOT "the node has a `bind.param`". Everything else, an ordinary
	// patched param on a multisampled sound included, takes the base rule above and opens: legacy
	// routes it through `MenuItem::checkPermissionToBeginSession` (`menu_item.cpp:27-31`), which
	// returns YES without ever looking at a range.
	if (!node.range_dependent || !ctx.addresses.slot) {
		return Permission::Yes;
	}

	// `:1950` -- a kit row is always range-unambiguous (one range per drum), and so is a source
	// with exactly one range. `context_for` has already pointed `ctx.range_index` at that range.
	if (ctx.is_kit || range_count(ctx) <= 1) {
		return Permission::Yes;
	}

	// `:1955-1957` -- a range is already selected for this source index (`ctx.source_index` is what
	// `context_for` resolved `ctx.range_index` against, so the legacy
	// `currentSourceIndex == whichThing` test holds by construction). Legacy's
	// `getCurrentUI() == &soundEditor` guard is dropped as a UI read; it is redundant here because
	// legacy already nulls the remembered range at `:1784-1788` whenever the UI or source index
	// changed, which is the same condition.
	//
	// Unreachable for a context straight out of `context_for`: the only cases that set `ctx.range_index`
	// (kit row, single range) already returned `Yes` above. This is E2's entry point -- once the
	// picker writes a chosen range back into the context, this is the arm that lets the item open.
	if (ctx.range_index >= 0) {
		return Permission::Yes;
	}

	return Permission::MustSelectRange; // `:1959`
}

} // namespace deluge::scenes::menu
