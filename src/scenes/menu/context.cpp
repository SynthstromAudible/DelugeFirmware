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
/// @brief The port of `SoundEditor::setup`'s context resolution, `getCurrentModelStack`, and the
///        common `checkPermissionToBeginSession` rule. See `context.h` for the interface.
///
/// **What did NOT come across** (task-3 ruling 2 -- `setup()` is half context resolution, half UI
/// choreography, and only the first half belongs in `src/scenes`). UI side, not ported:
///
///   * `:1684` `getCurrentUI()`, `:1687` `rootUIIsClipMinderScreen()`, `:1709`
///     `setupKitGlobalFXMenu`, `:1774`/`:1777` `currentUIMode`, `:1816`/`:1819` the
///     `currentUI ==` comparisons and `Buttons::isShiftButtonPressed()` -- reduced to
///     `context_for`'s three `bool` arguments.
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
/// **`currentSong` references in this file: 0.** Ruling 3 confines the global to this translation
/// unit and Task 12 ratchets the count; every place `setup()` and `getCurrentModelStack()` reached
/// for `currentSong` (`:1690`, `:2042`, `:2045`, `:2059`) or for a `getCurrent*()` helper that
/// reads it (`:2038`, `:2039`, `:2059`) takes it from `MenuContext::song` here instead, which is
/// the `Song&` the caller passed to `context_for`. The baseline Task 12 should record is 0.

#include "scenes/menu/context.h"

#include "scenes/bind/write_sink.h"
#include "scenes/menu/bind_host.h"
#include "scenes/menu/bindings.h"

#include "scenes/menu/node.h"
#include "scenes/services/view_state.h"

#include "model/clip/audio_clip.h"
#include "model/clip/clip.h"
#include "model/clip/instrument_clip.h"
#include "model/drum/drum.h"
#include "model/drum/non_audio_drum.h"
#include "model/edits/param_address_for.h"
#include "model/instrument/instrument.h"
#include "model/instrument/kit.h"
#include "model/mod_controllable/mod_controllable_audio.h"
#include "model/model_stack.h" // ruling 6: this file and bindings.cpp only
#include "model/note/note_row.h"
#include "model/output.h"
#include "model/project_bridge/chain_bridge.h"
#include "model/project_bridge/controls_bridge.h"
#include "model/project_bridge/slot_bridge.h"
#include "model/song/song.h"
#include "modulation/params/param_manager.h"
#include "playback/playback_handler.h"
#include "processing/sound/sound.h"
#include "processing/sound/sound_drum.h"
#include "processing/sound/sound_instrument.h"
#include "processing/source.h"
#include "storage/multi_range/multi_range.h"

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

MenuContext context_for(Song& song, Clip* clip, int8_t source_index, bool performance_view,
                        bool session_or_arranger_root, bool session_view_root, bool kit_global_fx) {
	MenuContext ctx{};
	ctx.song = &song;
	// The engine objects the context is resolved from; it keeps their addresses, not them.
	Output* output = nullptr;
	Sound* sound = nullptr;
	ctx.source_index = source_index;
	// Legacy's `!rootUIIsClipMinderScreen()`: performance view is not a clip minder either, whatever its caller says of
	// the session and arranger.
	ctx.is_song_view = session_or_arranger_root || performance_view;
	// The second, narrower root-UI fact -- `getRootUI() == &sessionView` rather than
	// `!rootUIIsClipMinderScreen()`. It steers no branch in this function: `:1819-1822` funnels the
	// session, arranger and song-automation views into one root, and only `relevance::is_session_view`
	// (the two `song::` items' `isRelevant`) ever asks which of the three it was.
	ctx.is_session_view = session_view_root;
	ctx.is_performance = performance_view;
	ctx.resolved = true;

	InstrumentClip* instrument_clip = nullptr;
	OutputType output_type = OutputType::NONE;

	// ---------------------------------------------------------------------------------------
	// Part 1: which sound / param manager / arp / mod controllable (`:1686-1762`).
	//
	// Note the asymmetry with part 2, transcribed as-is: part 1 keys off
	// `!rootUIIsClipMinderScreen()` FIRST and only then looks at `clip`, while part 2 keys off
	// `clip` first. A caller in a session view that also passes a clip therefore gets the song's
	// param manager with a clip root -- exactly what `setup()` does today.
	//
	// Part 3 inherits the same asymmetry in one more place: its audio-clip arm is guarded by
	// `ctx.is_audio_clip`, which only part 1's `else if (clip != nullptr)` arm ever sets, whereas
	// legacy `:1893` tests `clip->type` unguarded. So in the session-root-with-an-audio-clip case
	// legacy still fills `currentSampleControls`/`currentPriority` and this port leaves them null.
	// Deliberate, and the trade runs the other way too: legacy `:1893` dereferences `clip` when
	// `clip == nullptr` (the settings root with no sound), a latent null-deref this port does not
	// inherit -- so do NOT "restore fidelity" by dropping the guard.
	// ---------------------------------------------------------------------------------------

	// `:1686-1695` -- performance / session / arranger: the song's own param manager and global
	// effectable, read off a song-as-timeline-counter stack.
	if (ctx.is_song_view) {
		alignas(ModelStackWithAutoParam) char stack_memory[MODEL_STACK_MAX_SIZE];
		ModelStackWithThreeMainThings* stack = song.setupModelStackWithSongAsTimelineCounter(stack_memory);
		if (stack != nullptr) {
			ctx.mod = static_cast<ModControllableAudio*>(stack->modControllable);
		}
	}

	// `:1696-1762`
	else if (clip != nullptr) {
		output = clip->output;      // `:1697`
		output_type = output->type; // `:1698`
		ctx.is_kit = output_type == OutputType::KIT;
		ctx.is_audio_clip = clip->type == ClipType::AUDIO;

		// `:1700-1755` -- InstrumentClips.
		if (clip->type == ClipType::INSTRUMENT) {
			instrument_clip = static_cast<InstrumentClip*>(clip); // `:1702`

			// `:1704-1740` -- Kit.
			if (ctx.is_kit) {
				Drum* selected_drum = static_cast<Kit*>(output)->selectedDrum; // `:1706`

				// `:1708-1713` -- affect-entire ("kit global FX"): the kit itself is the mod
				// controllable and the clip's own param manager is what is edited.
				//
				// `setup()` gates this on its `setupKitGlobalFXMenu` member, which is the fourth
				// UI fact `context_for` takes as an argument. It is NOT the same as the clip's
				// `affectEntire`: the member is only ever set true (from
				// `outputType == KIT && instrumentClip->affectEntire` --
				// `model/clip/instrument_clip_minder.cpp:393-417`,
				// `gui/views/automation_view.cpp:1623-1625`) and is cleared on menu exit
				// (`sound_editor.cpp:189`, `:768`), so the kit-row grid-shortcut path
				// (`sound_editor.cpp:1205-1213`) enters with it false while `affectEntire` is true,
				// and legacy edits the row's params rather than the kit's. Deriving the branch from
				// `affectEntire` would silently take the kit path there -- and with it a different
				// root (`KitGlobalFx` vs `Drum`) and different addresses.
				//
				// `ctx.affect_entire` below still reports the clip's own setting, so a caller can
				// see both facts.
				if (kit_global_fx) {
					ctx.mod = static_cast<ModControllableAudio*>(output->toModControllable());
					ctx.is_kit_global_fx = true;
				}

				// `:1715-1730` -- a selected drum.
				else if (selected_drum != nullptr) {
					ctx.is_drum = true;

					// `:1717-1726` -- a SoundDrum: its note row holds the param manager.
					if (selected_drum->type == DrumType::SOUND) {
						NoteRow* note_row = instrument_clip->getNoteRowForDrum(selected_drum); // `:1718`
						if (note_row == nullptr) {
							ctx.resolved = false; // `:1719-1721` -- `setup()` returns false
						}
						else {
							auto* sound_drum = static_cast<SoundDrum*>(selected_drum);
							sound = sound_drum;
							ctx.mod = sound_drum;
						}
					}

					// `:1727-1729` -- a MIDI or gate drum: an arpeggiator and nothing else.
					else if (selected_drum->type == DrumType::MIDI || selected_drum->type == DrumType::GATE) {}
				}

				// `:1732-1739` -- a kit with neither affect-entire nor a selected drum: nothing to
				// edit. (`setup()` also pops up "select a row or affect entire" for the sequence
				// direction item; UI side, not ported.)
				else {
					ctx.resolved = false;
				}
			}

			// `:1742-1754` -- synth, MIDI or CV.
			else {
				// `:1744-1748` -- Synth.
				if (output_type == OutputType::SYNTH) {
					auto* synth = static_cast<SoundInstrument*>(output);
					sound = synth;
					ctx.mod = synth;
				}

				// `:1750` -- CV or MIDI: not much happens (no sound, no mod controllable).
			}
		}

		// `:1757-1761` -- AudioClips.
		else {
			ctx.mod = static_cast<ModControllableAudio*>(output->toModControllable());
		}
	}

	// A resolved output, reported whichever branch part 1 took: the kit clip's own `affectEntire`.
	// Kit-only, so that a synth clip (whose `affectEntire` member exists but means nothing) reports
	// false, as the task-3 brief's first case requires.
	ctx.affect_entire = ctx.is_kit && instrument_clip != nullptr && instrument_clip->affectEntire;

	// ---------------------------------------------------------------------------------------
	// Part 2: which root menu (`:1769-1827`, the `item == nullptr` arm -- an explicitly requested
	// `item` is the caller's own choice and has no analogue here).
	//
	// The three kit arms below are spelled with `is_kit_global_fx` where legacy `:1790`/`:1794`/
	// `:1800` spell `instrumentClip->affectEntire`. Inside this arm's own domain the two are the
	// same fact: `:1790` is only ever evaluated when `item == nullptr`, and every caller that
	// passes no item sets `setupKitGlobalFXMenu` from `affectEntire`, while the one caller where
	// they differ -- the kit-row grid shortcut (`:1205-1213`) -- always passes an item and so never
	// reaches here. Outside it, this spelling is what keeps the root consistent with the
	// `params`/`mod`/`arp` part 1 just resolved: a shortcut caller editing one row gets `Drum`
	// (or `MidiDrum`/`GateDrum`), not `KitGlobalFx` over row params.
	// ---------------------------------------------------------------------------------------

	if (clip != nullptr) { // `:1770`
		if (clip->type == ClipType::INSTRUMENT) {
			// `:1774-1779` -- the `UI_MODE_NOTES_PRESSED` / `UI_MODE_AUDITIONING` note-editor roots
			// are not chosen here: legacy reads `currentUIMode` for them (`:1774`, `:1777`), which is UI
			// side, so this function never assigns `Root::NoteEditor`/`Root::NoteRowEditor`. The caller
			// names them instead, through `NoteMode` (`util/shell_menu_entry.h`), and `openShellMenu`
			// overrides `ctx.root` with it after this function returns
			// (`gui/ui/shell_menu_entry.cpp:136-139`).

			if (output_type == OutputType::MIDI_OUT) { // `:1780-1784`
				ctx.root = MenuContext::Root::MidiOrCv;
			}
			else if (output_type == OutputType::CV) { // `:1785-1788`, the `goto doMIDIOrCV` arm
				ctx.root = MenuContext::Root::MidiOrCv;
			}
			else if (ctx.is_kit && ctx.is_kit_global_fx) { // `:1790-1792`
				ctx.root = MenuContext::Root::KitGlobalFx;
			}
			else if (ctx.is_kit && !ctx.is_kit_global_fx && ctx.is_drum
			         && static_cast<Kit*>(output)->selectedDrum->type == DrumType::MIDI) { // `:1794-1798`
				ctx.root = MenuContext::Root::MidiDrum;
			}
			else if (ctx.is_kit && !ctx.is_kit_global_fx && ctx.is_drum
			         && static_cast<Kit*>(output)->selectedDrum->type == DrumType::GATE) { // `:1800-1804`
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
	else {
		if (performance_view) { // `:1816-1818`
			ctx.root = MenuContext::Root::Performance;
		}
		// `:1819-1822` -- the WIDE bool on purpose: legacy's three views share one root menu, and
		// `session_view_root` only narrows what is *relevant* inside it, never which root is picked.
		else if (session_or_arranger_root) {
			ctx.root = MenuContext::Root::Song;
		}
		else { // `:1823-1825`
			ctx.root = MenuContext::Root::Settings;
		}
	}

	// ---------------------------------------------------------------------------------------
	// Part 3: the range (`:1873-1897`).
	// ---------------------------------------------------------------------------------------

	if (sound != nullptr) { // `:1881-1892`
		// `:1887-1891` -- auto-select the first range. `setup()` does this only once
		// `checkPermissionToBeginSession` has returned YES for the *specific item* being opened,
		// which `context_for` has no item to ask. So the selection is made exactly where the
		// permission rule itself would be unambiguous
		// (`checkPermissionToBeginSessionForRangeSpecificParam:1950` -- a kit, or a single range),
		// and left unchosen when a choice is genuinely pending; `permission_for()` below then reports
		// `MustSelectRange` for that case, as it must.
		//
		// `range_index` is therefore the *selected/remembered* range, i.e. the input `permission_for`
		// reads, matching legacy's `newRange` at `:1839-1843` rather than its post-permission
		// assignment at `:1889`.
		const std::ptrdiff_t ranges = std::ssize(sound->sources[ctx.source_index].ranges);
		if (ranges == 1 || (ctx.is_kit && ranges > 0)) {
			ctx.range_index = 0;
		}
	}

	// What a target node binds through: the model, the edited sound's slot, the kit an affect-entire write fans out
	// over, and the app's facts and sink.
	ctx.model = &song.project;
	// The app's facts and sink act on the song it plays: a context over any other song binds nothing through them.
	if (host_binds(song)) {
		ctx.facts = &host_runtime_facts();
		ctx.sink = &host_write_sink();
	}
	// An unresolved context (a kit row with no note row) is one legacy refuses to open: it names nothing to edit.
	if (ctx.resolved && sound != nullptr) {
		ctx.addresses.slot = deluge::bridge::slotAddressOf(*sound);
	}
	if (ctx.resolved && ctx.is_kit && output != nullptr) {
		ctx.addresses.kit_output = output->id();
	}
	// The records a model target edits: the clip and its track, a kit row's lane and (a sound row's) note row; and what
	// plays them, which the model does not hold but the root does.
	if (ctx.resolved && !ctx.is_song_view && clip != nullptr && output != nullptr && output->id()) {
		ctx.addresses.clip = deluge::project::ClipAddress{clip->id()};
		ctx.addresses.track = deluge::project::TrackAddress{*output->id()};
		using Instrument = deluge::engine::SettingContext::Instrument;
		switch (output_type) {
		case OutputType::SYNTH:
			ctx.addresses.instrument = Instrument::SYNTH;
			break;
		case OutputType::KIT:
			ctx.addresses.instrument = Instrument::KIT;
			break;
		case OutputType::MIDI_OUT:
			ctx.addresses.instrument = Instrument::MIDI;
			break;
		case OutputType::CV:
			ctx.addresses.instrument = Instrument::CV;
			break;
		case OutputType::AUDIO:
			ctx.addresses.instrument = Instrument::AUDIO;
			break;
		default:
			break;
		}
		if (ctx.is_drum) {
			const Drum* drum = static_cast<Kit*>(output)->selectedDrum;
			if (drum != nullptr && drum->id()) {
				ctx.addresses.lane = deluge::project::LaneAddress{*output->id(), *drum->id()};
				if (drum->type == DrumType::SOUND) {
					ctx.addresses.note_row = deluge::project::NoteRowAddress{
					    .clip_id = clip->id(), .is_kit = true, .row_key = static_cast<int32_t>(*drum->id())};
				}
			}
		}
	}
	// The chain an effect target edits when no sound is: the whole kit's, an audio track's or the master's. The model's
	// address cannot say which kind of output holds it; the root can.
	if (ctx.resolved && sound == nullptr && ctx.mod != nullptr) {
		ctx.addresses.chain = deluge::bridge::chainAddressOf(*ctx.mod, song);
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

// -- The gold-knob model boundary (`services::ModKnobs`, E4 task 2) ---------------------------
//
// See `context.h`'s `@name` block for why these four live here and not beside the service that
// calls them: they need `model/model_stack.h`, and this file is one of the two the
// `scenes-model-stack-outside-context` ratchet lets include it. Everything that is *policy* rather
// than a model transaction -- `calculateKnobPosForModEncoderTurn`, the popup, the MIDI-follow
// feedback, the indicator blink, the repaint, every UI read legacy makes -- is deliberately absent
// here and lives in `services/mod_knobs.cpp`.

namespace {

/// @brief Rebuild `View::activeModControllableModelStack` (`view.h:120-121`) over @p memory from
///        the pointers `ModTarget` keeps.
/// @param t      The target.
/// @param memory `MODEL_STACK_MAX_SIZE` bytes.
/// @return The rebuilt stack.
ModelStackWithThreeMainThings* stack_for(const services::ModTarget& t, void* memory) {
	// Spelled out rather than through `setupModelStackWithThreeMainThingsIncludingNoteRow`
	// (`model_stack.h:335-343`), whose param-manager argument is a `ParamManagerForTimeline*`:
	// `ModelStackWithThreeMainThings::paramManager` is a plain `ParamManager*`, and so is the one
	// `View::setActiveModControllableWithoutTimelineCounter` puts there (`view.cpp:1917-1918`), via
	// `addOtherTwoThingsButNoNoteRow`'s `ParamManager*` overload (`model_stack.h:369-373`).
	return setupModelStackWithSong(memory, t.song)
	    ->addTimelineCounter(t.timeline_counter)
	    ->addNoteRow(t.note_row_id, t.note_row)
	    ->addOtherTwoThings(t.mod_controllable, t.param_manager);
}

/// @brief Copy a stack's five resolved fields back into @p t.
///
/// Legacy needs no equivalent: its stack *is* the member, so anything that rewrites it (the
/// arrangement-recording clone at `view.cpp:1009-1017`, `getActiveModControllable` at `:1893`) has
/// already updated the state. This port's stacks are scratch, so the write-back is explicit.
/// @param t The target to update.
/// @param s The stack to read.
void store(services::ModTarget& t, const ModelStackWithThreeMainThings* s) {
	t.timeline_counter = s->getTimelineCounterAllowNull();
	t.note_row = s->getNoteRowAllowNull();
	t.note_row_id = s->noteRowId;
	t.mod_controllable = s->modControllable;
	t.param_manager = s->paramManager;
}

/// @brief `View::getModelStackWithParam` (`view.cpp:988-1027`), over an already-built @p active.
/// @param target                  Updated in place if the clone replaced what is being edited.
/// @param active                  The rebuilt active stack; **overwritten in place** by the return
///                                value, exactly as legacy's member is.
/// @param which_encoder           0 (lower gold) or 1 (upper).
/// @param note_tails_allowed_before Out: `:988-989`'s reading, taken before the clone.
/// @return The param stack, or null.
ModelStackWithAutoParam* param_stack(services::ModTarget& target, ModelStackWithThreeMainThings* active,
                                     int32_t which_encoder, bool& note_tails_allowed_before) {
	if (active->timelineCounterIsSet()) { // `:979`
		alignas(ModelStackWithAutoParam) char temp_memory[MODEL_STACK_MAX_SIZE];
		copyModelStack(temp_memory, active, sizeof(ModelStackWithThreeMainThings));
		auto* temp = reinterpret_cast<ModelStackWithThreeMainThings*>(temp_memory);

		note_tails_allowed_before = active->modControllable->allowNoteTails(temp->addSoundFlags()); // `:989-990`

		auto* with_counter = static_cast<ModelStackWithTimelineCounter*>(static_cast<void*>(active));
		if (active->getTimelineCounter()->possiblyCloneForArrangementRecording(with_counter)) { // `:992-994`
			// `:996-1000` -- get back the NoteRow, ParamManager and whatever else.
			active->getTimelineCounter()->getActiveModControllable(with_counter);
		}
	}
	store(target, active);
	return active->modControllable->getParamFromModEncoder(which_encoder, active); // `:1004-1005`
}

} // namespace

void mod_target_from_timeline(services::ModTarget& target, Song& song, TimelineCounter* counter) {
	if (counter != nullptr) {
		counter = counter->getTimelineCounterToRecordTo(); // `:1868-1870`
	}

	alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* stack = setupModelStackWithSong(memory, &song)->addTimelineCounter(counter);

	if (counter != nullptr) {
		counter->getActiveModControllable(stack); // `:1876-1878`
	}
	else {
		stack->addOtherTwoThingsButNoNoteRow(nullptr, nullptr); // `:1880-1882`
	}

	// Both arms leave a `ModelStackWithThreeMainThings` in the same bytes -- every `add*` in this
	// family reinterpret-casts `this` rather than building anywhere new (`model_stack.h:347-372`),
	// which is exactly why legacy can keep reading its member as one afterwards.
	target.song = &song;
	store(target, reinterpret_cast<const ModelStackWithThreeMainThings*>(memory));
}

services::ModTurn mod_encoder_turn(services::ModTarget& target, int32_t which_encoder, int32_t offset,
                                   services::NextKnobPosFn next_knob_pos) {
	services::ModTurn out{};
	if (target.mod_controllable == nullptr) {
		return out; // `:824` -- the whole of `modEncoderAction`'s body is inside this test.
	}
	out.has_controllable = true;

	alignas(ModelStackWithAutoParam) char active_memory[MODEL_STACK_MAX_SIZE];
	ModelStackWithThreeMainThings* active = stack_for(target, active_memory);

	bool note_tails_allowed_before = false;
	ModelStackWithAutoParam* param = param_stack(target, active, which_encoder, note_tails_allowed_before);

	if (param == nullptr || param->autoParam == nullptr) {
		// `modEncoderAction_nonExistentParam` (`:843-852`). Note legacy passes the (possibly null)
		// param stack straight through, so the `ModControllable` sees the same argument it would.
		const ActionResult result =
		    target.mod_controllable->modEncoderActionForNonExistentParam(offset, which_encoder, param);
		out.caused_change = (result == ActionResult::ACTIONED_AND_CAUSED_CHANGE); // `:848`
		return out;
	}
	out.param_exists = true;

	// `modEncoderAction_existentParam` (`:853-948`).
	alignas(ModelStackWithAutoParam) char temp_memory[MODEL_STACK_MAX_SIZE];
	copyModelStack(temp_memory, param, sizeof(ModelStackWithThreeMainThings)); // `:855-858`
	auto* temp = reinterpret_cast<ModelStackWithThreeMainThings*>(temp_memory);

	const deluge::modulation::params::Kind kind = param->paramCollection->getParamKind(); // `:859`
	const int32_t value = param->autoParam->getValuePossiblyAtPos(target.mod_pos, param); // `:861`
	const int32_t knob_pos = param->paramCollection->paramValueToKnobPos(value, param);   // `:862`
	const int32_t new_knob_pos = next_knob_pos(kind, knob_pos, offset);                   // `:863`

	out.kind = kind;
	out.param_id = param->paramId;
	out.old_knob_pos = knob_pos;
	out.new_knob_pos = new_knob_pos;

	if (new_knob_pos == knob_pos) {
		return out; // `:902-904`, which in legacy is reached *after* the popup -- see `context.h`.
	}
	out.value_changed = true;

	const int32_t new_value = param->paramCollection->knobPosToParamValue(new_knob_pos, param); // `:920`
	// `:923` -- the write, as the param's Edit by its address, over the region the view holds. A knob's owner made
	// this session takes its durable id first, so it has an address.
	(void)param->doc();
	const std::optional<deluge::project::ParamAddress> address = deluge::edits::addressFor(*param);
	if (!address
	    || !host_write_sink().write_param(*address, new_value, static_cast<int32_t>(target.mod_pos),
	                                      static_cast<int32_t>(target.mod_length))) {
		out.value_changed = false; // refused: nothing changed, so nothing is fed back
		return out;
	}

	if (target.timeline_counter_set()) { // `:925`
		const bool note_tails_allowed_after = param->modControllable->allowNoteTails(temp->addSoundFlags());
		out.note_tails_changed = (note_tails_allowed_before != note_tails_allowed_after); // `:929`
	}

	// `potentiallyMakeItHarderToTurnKnob`'s one model read (`:1068-1069`); the blink decision it
	// feeds is `ModKnobs`'.
	out.should_indicate_middle_value = param->paramCollection->shouldParamIndicateMiddleValue(param);
	return out;
}

services::ModParamInfo mod_param_info(services::ModTarget& target, int32_t which_encoder) {
	services::ModParamInfo out{};
	if (target.mod_controllable == nullptr) {
		return out; // `:974` -- `getModelStackWithParam`'s own guard.
	}

	alignas(ModelStackWithAutoParam) char active_memory[MODEL_STACK_MAX_SIZE];
	ModelStackWithThreeMainThings* active = stack_for(target, active_memory);

	bool note_tails_allowed_before = false;
	ModelStackWithAutoParam* param = param_stack(target, active, which_encoder, note_tails_allowed_before);
	if (param == nullptr || param->autoParam == nullptr) {
		return out; // `:1015`
	}
	out.param_exists = true;
	out.kind = param->paramCollection->getParamKind(); // `:1016`
	out.param_id = param->paramId;
	return out;
}

void mod_region_grab_values(const services::ModTarget& target) {
	if (target.mod_controllable == nullptr || target.param_manager == nullptr) {
		return; // `:1930-1931`
	}
	alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
	ModelStackWithThreeMainThings* active = stack_for(target, memory);
	// `:1937-1938`. The comment there: "activeModControllable might not be a Sound, but in that
	// case, the pointer's not going to get used".
	target.param_manager->toForTimeline()->grabValuesFromPos(target.mod_pos, active);
}

services::ModIndicatorInfo mod_indicator_info(const services::ModTarget& target, int32_t which_encoder) {
	services::ModIndicatorInfo out{};
	if (target.mod_controllable == nullptr) {
		// `View::setKnobIndicatorLevels` (`:1390`) only calls this with a mod controllable resolved;
		// guarded here anyway, because `setKnobIndicatorLevel` itself dereferences it unchecked.
		return out;
	}

	alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
	ModelStackWithThreeMainThings* active = stack_for(target, memory);
	// `:1407-1408`. The third argument is legacy's own `false`: no arrangement-recording clone --
	// levelling an indicator is a read, and must not fork a clip.
	ModelStackWithAutoParam* param = target.mod_controllable->getParamFromModEncoder(which_encoder, active, false);
	if (param == nullptr) {
		// Legacy dereferences the result unchecked (`:1413`); returning "nothing to declare" is the
		// port's answer to a null it cannot prove impossible.
		return out;
	}
	out.param_id = param->paramId;

	if (param->autoParam != nullptr) { // `:1413`
		out.has_auto_param = true;
		const int32_t value = param->autoParam->getValuePossiblyAtPos(target.mod_pos, param); // `:1414`
		ParamCollection* collection = param->paramCollection;                                 // `:1415`
		out.kind = collection->getParamKind();                                                // `:1416`
		out.bipolar = deluge::modulation::params::isParamBipolar(out.kind, param->paramId);   // `:1417`
		out.knob_pos = collection->paramValueToKnobPos(value, param);                         // `:1418`
		out.quantized_stutter = deluge::modulation::params::isParamQuantizedStutter(
		    out.kind, param->paramId, static_cast<ModControllableAudio*>(param->modControllable)); // `:1428-1429`
		return out;
	}

	if (param->paramId == 255) { // `:1454`
		out.non_existent_param = true;
		out.non_existent_knob_pos =
		    param->modControllable->getKnobPosForNonExistentParam(which_encoder, param); // `:1455-1456`
		return out;
	}
	// `:1460` -- "is it not just a param? then its a patch cable".
	out.patch_cable_default = ((param->paramId & 0x0000FF00) != 0x0000FF00);
	return out;
}

bool mod_encoder_button(const services::ModTarget& target, int32_t which_encoder, bool on) {
	if (target.mod_controllable == nullptr) {
		return false; // `:1340` -- `modEncoderButtonAction`'s own guard.
	}
	alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
	ModelStackWithThreeMainThings* active = stack_for(target, memory);
	// `:1364-1366` copies the stack before handing it over, because `ModControllable`
	// implementations write to it; here `active` is already a scratch copy, so it is handed over
	// directly.
	const bool edited = target.mod_controllable->modEncoderButtonAction(which_encoder, on, active); // `:1368-1369`
	// `:1420`: a press may cycle a knob to its next param (LPF, HPF, EQ), so its owner's mappings go to the record.
	const std::optional<project::ControlsAddress> address =
	    target.song != nullptr ? bridge::controlsAddressOf(*target.mod_controllable, *target.song) : std::nullopt;
	if (address && address->kind != project::ControlsAddress::Kind::SONG && host_binds(*target.song)) {
		bool is_sound = false;
		::ModControllableAudio* owner = bridge::controlsOwnerAt(*target.song, *address, &is_sound);
		if (owner != nullptr) {
			(void)host_write_sink().write_controls(*address, is_sound
			                                                     ? bridge::controlsOf(static_cast<::Sound&>(*owner))
			                                                     : std::vector<project::ControllerMapping>{});
		}
	}
	return edited;
}

bool mod_knob_mode(const services::ModTarget& target, uint8_t mode) {
	uint8_t* slot = target.mod_controllable != nullptr ? target.mod_controllable->getModKnobMode() : nullptr;
	if (slot == nullptr || target.song == nullptr) {
		return false;
	}
	if (!host_binds(*target.song)) {
		*slot = mode; // a song the app does not play: its engine field alone
		return false;
	}
	if (slot == &target.song->live.global_effectable.modKnobMode) {
		return host_write_sink().knob_mode(project::SongAddress{}, mode);
	}
	for (Output* list : {target.song->live.media.first_output,
	                     static_cast<Output*>(target.song->live.media.first_hibernating_instrument)}) {
		for (Output* output = list; output != nullptr; output = output->next) {
			if (slot == &output->modKnobMode) {
				return host_write_sink().knob_mode(project::TrackAddress{target.song->outputIdFor(*output)}, mode);
			}
		}
	}
	*slot = mode; // an owner outside the song: its engine field alone
	return false;
}

bool mod_delete_automation(const services::ModTarget& target, int32_t which_encoder) {
	if (target.mod_controllable == nullptr) {
		return false;
	}
	alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
	ModelStackWithThreeMainThings* active = stack_for(target, memory);
	ModelStackWithAutoParam* param =
	    target.mod_controllable->getParamFromModEncoder(which_encoder, active); // `:1352-1354`
	if (param == nullptr || param->autoParam == nullptr) {
		return false; // `:1356`
	}
	// `:1357-1358`, as the param's Edit by its address.
	(void)param->doc();
	const std::optional<deluge::project::ParamAddress> address = deluge::edits::addressFor(*param);
	// `:1359`'s popup is the caller's, as an `Action::Kind::Popup`, raised only where the delete was taken.
	return address && host_write_sink().delete_automation(*address);
}

} // namespace deluge::scenes::menu
