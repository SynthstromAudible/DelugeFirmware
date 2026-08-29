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
///   * `:1629` `getCurrentUI()`, `:1632` `rootUIIsClipMinderScreen()`, `:1719`/`:1722`
///     `currentUIMode`, `:1761`/`:1764` the `currentUI ==` comparisons and
///     `Buttons::isShiftButtonPressed()` -- reduced to `context_for`'s two `bool` arguments.
///   * `:1654` the `setupKitGlobalFXMenu` member -- see the branch comment below.
///   * `:1679-1682` the "select a row or affect entire" popup, `:1794` the "parameter not
///     applicable" popup, `:1809` `display->cancelPopup()` -- every `display->` call.
///   * `:1716` `actionLogger.deleteAllLogs()` -- an undo-history side effect of *opening* a menu.
///   * `:1726`/`:1731` retitling the shared `soundEditorRootMenuMIDIOrCV` singleton -- an artefact
///     of one menu object serving MIDI and CV; `Root::MidiOrCv` plus `MenuContext::output` carries
///     the same information as data.
///   * `:1781-1782`/`:1805-1807` `maybeGetParentMenu` / `focusChild` (horizontal-menu focusing),
///     `:1802` `multiRangeMenu.menuItemHeadingTo`, `:1803` substituting the range picker for the
///     chosen item -- navigation, which is `MenuScene`'s job (spec E §4.1).
///   * `:1811-1816` `allowsNoteTails` -- a `SoundEditor` render flag, not part of what is edited.
///   * `:1844-1849` `endScreen()`, `navigationDepth`, `shouldGoUpOneLevelOnBegin`,
///     `menuItemNavigationRecord` -- `MenuState`, which belongs to the interpreter.
///
/// **`currentSong` references in this file: 0.** Ruling 3 confines the global to this translation
/// unit and Task 12 ratchets the count; every place `setup()` and `getCurrentModelStack()` reached
/// for `currentSong` (`:1635`, `:1987`, `:1990`, `:2004`) or for a `getCurrent*()` helper that
/// reads it (`:1983`, `:1984`, `:2004`) takes it from `MenuContext::song` here instead, which is
/// the `Song&` the caller passed to `context_for`. The baseline Task 12 should record is 0.

#include "scenes/menu/context.h"

#include "scenes/menu/node.h"

#include "model/clip/audio_clip.h"
#include "model/clip/clip.h"
#include "model/clip/instrument_clip.h"
#include "model/drum/drum.h"
#include "model/drum/non_audio_drum.h"
#include "model/instrument/kit.h"
#include "model/mod_controllable/mod_controllable_audio.h"
#include "model/model_stack.h" // ruling 6: this file and bindings.cpp only
#include "model/note/note_row.h"
#include "model/output.h"
#include "model/song/song.h"
#include "modulation/params/param_manager.h"
#include "processing/sound/sound.h"
#include "processing/sound/sound_drum.h"
#include "processing/sound/sound_instrument.h"
#include "processing/source.h"
#include "storage/multi_range/multi_range.h"

#include <iterator>

namespace deluge::scenes::menu {

namespace {

/// @brief `Source::ranges` holds `MultiRange*` by base pointer; element 0 is what `setup()`'s
///        auto-selection (`:1834`) and `checkPermissionToBeginSessionForRangeSpecificParam`'s
///        single-range shortcut (`:2049`) both reach for.
MultiRange* first_range(Source& source) {
	return source.ranges.empty() ? nullptr : static_cast<MultiRange*>(source.ranges.getElement(0));
}

/// @brief How many ranges the addressed source has -- `std::ssize(source->ranges)` at `:2049`.
std::ptrdiff_t range_count(const MenuContext& ctx) {
	return ctx.source != nullptr ? std::ssize(ctx.source->ranges) : 0;
}

} // namespace

MenuContext context_for(Song& song, Clip* clip, int8_t source_index, bool performance_view,
                        bool session_or_arranger_root) {
	MenuContext ctx{};
	ctx.song = &song;
	ctx.clip = clip;
	ctx.source_index = source_index;
	ctx.is_song_view = session_or_arranger_root;
	ctx.is_performance = performance_view;
	ctx.resolved = true;

	InstrumentClip* instrument_clip = nullptr;
	OutputType output_type = OutputType::NONE;

	// ---------------------------------------------------------------------------------------
	// Part 1: which sound / param manager / arp / mod controllable (`:1631-1707`).
	//
	// Note the asymmetry with part 2, transcribed as-is: part 1 keys off
	// `!rootUIIsClipMinderScreen()` FIRST and only then looks at `clip`, while part 2 keys off
	// `clip` first. A caller in a session view that also passes a clip therefore gets the song's
	// param manager with a clip root -- exactly what `setup()` does today.
	// ---------------------------------------------------------------------------------------

	// `:1631-1640` -- performance / session / arranger: the song's own param manager and global
	// effectable, read off a song-as-timeline-counter stack.
	if (session_or_arranger_root) {
		char stack_memory[MODEL_STACK_MAX_SIZE];
		ModelStackWithThreeMainThings* stack = song.setupModelStackWithSongAsTimelineCounter(stack_memory);
		if (stack != nullptr) {
			ctx.params = static_cast<ParamManagerForTimeline*>(stack->paramManager);
			ctx.mod = static_cast<ModControllableAudio*>(stack->modControllable);
		}
	}

	// `:1641-1707`
	else if (clip != nullptr) {
		ctx.output = clip->output;      // `:1642`
		output_type = ctx.output->type; // `:1643`
		ctx.is_kit = output_type == OutputType::KIT;
		ctx.is_audio_clip = clip->type == ClipType::AUDIO;

		// `:1645-1700` -- InstrumentClips.
		if (clip->type == ClipType::INSTRUMENT) {
			instrument_clip = static_cast<InstrumentClip*>(clip); // `:1647`

			// `:1649-1685` -- Kit.
			if (ctx.is_kit) {
				Drum* selected_drum = static_cast<Kit*>(ctx.output)->selectedDrum; // `:1651`

				// `:1653-1658` -- affect-entire ("kit global FX"): the kit itself is the mod
				// controllable and the clip's own param manager is what is edited.
				//
				// `setup()` gates this on its `setupKitGlobalFXMenu` member rather than on
				// `affectEntire` directly. Both of that member's writers set it to exactly
				// `outputType == KIT && instrumentClip->affectEntire`
				// (`model/clip/instrument_clip_minder.cpp:388-390`,
				// `gui/views/automation_view.cpp:1601-1603`), so the flag is ported as that
				// condition. The one case the member also encodes -- entering via a kit-row grid
				// shortcut, where it is left false so the row's own params are edited -- is a
				// shortcut-table concern (spec E §4.2) and is not resolvable from these arguments;
				// `affect_entire` below carries what such a caller needs to override it.
				if (instrument_clip->affectEntire) {
					ctx.mod = static_cast<ModControllableAudio*>(ctx.output->toModControllable());
					ctx.params = &instrument_clip->paramManager;
					ctx.arp = &instrument_clip->arpSettings;
					ctx.is_kit_global_fx = true;
				}

				// `:1660-1675` -- a selected drum.
				else if (selected_drum != nullptr) {
					ctx.is_drum = true;

					// `:1662-1671` -- a SoundDrum: its note row holds the param manager.
					if (selected_drum->type == DrumType::SOUND) {
						NoteRow* note_row = instrument_clip->getNoteRowForDrum(selected_drum); // `:1663`
						if (note_row == nullptr) {
							ctx.resolved = false; // `:1664-1666` -- `setup()` returns false
						}
						else {
							auto* sound_drum = static_cast<SoundDrum*>(selected_drum);
							ctx.sound = sound_drum;
							ctx.mod = sound_drum;
							ctx.params = &note_row->paramManager;
							ctx.arp = &sound_drum->arpSettings;
						}
					}

					// `:1672-1674` -- a MIDI or gate drum: an arpeggiator and nothing else.
					else if (selected_drum->type == DrumType::MIDI || selected_drum->type == DrumType::GATE) {
						ctx.arp = &static_cast<NonAudioDrum*>(selected_drum)->arpSettings;
					}
				}

				// `:1677-1684` -- a kit with neither affect-entire nor a selected drum: nothing to
				// edit. (`setup()` also pops up "select a row or affect entire" for the sequence
				// direction item; UI side, not ported.)
				else {
					ctx.resolved = false;
				}
			}

			// `:1687-1699` -- synth, MIDI or CV.
			else {
				// `:1689-1693` -- Synth.
				if (output_type == OutputType::SYNTH) {
					auto* synth = static_cast<SoundInstrument*>(ctx.output);
					ctx.sound = synth;
					ctx.mod = synth;
				}

				// `:1695` -- CV or MIDI: not much happens (no sound, no mod controllable).

				ctx.params = &clip->paramManager;        // `:1697`
				ctx.arp = &instrument_clip->arpSettings; // `:1698`
			}
		}

		// `:1702-1706` -- AudioClips.
		else {
			ctx.params = &clip->paramManager;
			ctx.mod = static_cast<ModControllableAudio*>(ctx.output->toModControllable());
		}
	}

	// Kit-only, so that a synth clip (whose `affectEntire` member exists but means nothing) reports
	// false, as the task-3 brief's first case requires.
	ctx.affect_entire = ctx.is_kit && instrument_clip != nullptr && instrument_clip->affectEntire;

	// ---------------------------------------------------------------------------------------
	// Part 2: which root menu (`:1711-1772`, the `item == nullptr` arm -- an explicitly requested
	// `item` is the caller's own choice and has no analogue here).
	// ---------------------------------------------------------------------------------------

	if (clip != nullptr) { // `:1715`
		if (clip->type == ClipType::INSTRUMENT) {
			// `:1719-1724` -- `UI_MODE_NOTES_PRESSED` / `UI_MODE_AUDITIONING` note-editor roots:
			// UI side, not ported (see the file header).

			if (output_type == OutputType::MIDI_OUT) { // `:1725-1729`
				ctx.root = MenuContext::Root::MidiOrCv;
			}
			else if (output_type == OutputType::CV) { // `:1730-1733`, the `goto doMIDIOrCV` arm
				ctx.root = MenuContext::Root::MidiOrCv;
			}
			else if (ctx.is_kit && ctx.affect_entire) { // `:1735-1737`
				ctx.root = MenuContext::Root::KitGlobalFx;
			}
			else if (ctx.is_kit && !ctx.affect_entire && ctx.is_drum
			         && static_cast<Kit*>(ctx.output)->selectedDrum->type == DrumType::MIDI) { // `:1739-1743`
				ctx.root = MenuContext::Root::MidiDrum;
			}
			else if (ctx.is_kit && !ctx.affect_entire && ctx.is_drum
			         && static_cast<Kit*>(ctx.output)->selectedDrum->type == DrumType::GATE) { // `:1745-1749`
				ctx.root = MenuContext::Root::GateDrum;
			}
			else { // `:1751-1753`
				ctx.root = ctx.is_kit ? MenuContext::Root::Drum : MenuContext::Root::Sound;
			}
		}
		else { // `:1756-1758`
			ctx.root = MenuContext::Root::AudioClip;
		}
	}
	else {
		if (performance_view) { // `:1761-1763`
			ctx.root = MenuContext::Root::Performance;
		}
		else if (session_or_arranger_root) { // `:1764-1767`
			ctx.root = MenuContext::Root::Song;
		}
		else { // `:1768-1770`
			ctx.root = MenuContext::Root::Settings;
		}
	}

	// ---------------------------------------------------------------------------------------
	// Part 3: the derived `current*` fields (`:1818-1842`).
	// ---------------------------------------------------------------------------------------

	if (ctx.mod != nullptr) { // `:1822-1824`
		ctx.sidechain = &ctx.mod->sidechain;
	}

	if (ctx.sound != nullptr) {                             // `:1826-1837`
		ctx.source = &ctx.sound->sources[ctx.source_index]; // `:1828`
		ctx.sample_controls = &ctx.source->sampleControls;  // `:1829`
		ctx.priority = &ctx.sound->voicePriority;           // `:1830`

		// `:1832-1836` -- auto-select the first range. `setup()` does this only once
		// `checkPermissionToBeginSession` has returned YES for the *specific item* being opened,
		// which `context_for` has no item to ask. So the selection is made exactly where the
		// permission rule itself would be unambiguous
		// (`checkPermissionToBeginSessionForRangeSpecificParam:2049` -- a kit, or a single range),
		// and left null when a choice is genuinely pending; `permission_for()` below then reports
		// `MustSelectRange` for that case, as it must.
		const std::ptrdiff_t ranges = std::ssize(ctx.source->ranges);
		if (ranges == 1 || (ctx.is_kit && ranges > 0)) {
			ctx.range = first_range(*ctx.source);
			ctx.range_index = 0;
		}
	}
	else if (ctx.is_audio_clip) { // `:1838-1842`
		auto* audio_clip = static_cast<AudioClip*>(clip);
		ctx.sample_controls = &audio_clip->sampleControls;
		ctx.priority = &audio_clip->voicePriority;
	}

	return ctx;
}

ModelStackWithThreeMainThings* model_stack_for(const MenuContext& ctx, void* memory) {
	// `:1986-1988` -- not a clip-minder screen: the song is the timeline counter and brings its own
	// param manager and global effectable.
	if (ctx.is_song_view) {
		return ctx.song->setupModelStackWithSongAsTimelineCounter(memory);
	}

	// `:1989-1993` -- kit with affect-entire: the clip is the timeline counter, and the context's
	// own mod controllable + param manager are attached with no note row.
	if (ctx.is_kit && ctx.affect_entire) {
		ModelStackWithTimelineCounter* stack = setupModelStackWithTimelineCounter(memory, ctx.song, ctx.clip);
		return stack->addOtherTwoThingsButNoNoteRow(ctx.mod, ctx.params);
	}

	// `:1994-2004` -- everything else: a note row too, when a kit drum is selected.
	//
	// `noteRowIndex` is left uninitialised by `getCurrentModelStack` when there is no selected drum
	// (`:1996`); it is zero-initialised here, since `addNoteRow(id, nullptr)` stores the id either
	// way and an indeterminate value is a real (if currently harmless) UB read.
	NoteRow* note_row = nullptr;
	int32_t note_row_index = 0;
	if (ctx.is_kit) {
		Drum* selected_drum = static_cast<Kit*>(ctx.output)->selectedDrum;
		if (selected_drum != nullptr) {
			note_row = static_cast<InstrumentClip*>(ctx.clip)->getNoteRowForDrum(selected_drum, &note_row_index);
		}
	}

	return setupModelStackWithThreeMainThingsIncludingNoteRow(memory, ctx.song, ctx.clip, note_row_index, note_row,
	                                                          ctx.mod, ctx.params);
}

Permission permission_for(const Node& node, const MenuContext& ctx) {
	// The base implementation, `MenuItem::checkPermissionToBeginSession`
	// (`gui/menu_item/menu_item.cpp:27-31`): relevance, and nothing else. Spec E §4's `relevant`
	// hook is the port of the 112 `isRelevant` overrides the survey counts.
	if (node.relevant != nullptr && !node.relevant(ctx)) {
		return Permission::No;
	}

	// The rule the 18 overrides share, via
	// `SoundEditor::checkPermissionToBeginSessionForRangeSpecificParam` (`sound_editor.cpp:2035`) --
	// e.g. `menu_item::sample::Transpose::checkPermissionToBeginSession`
	// (`gui/menu_item/sample/transpose.h:120-135`), which after its own relevance and osc-type test
	// defers to it verbatim. A node that edits a model parameter on a sound whose addressed source
	// has more than one `MultiRange`, with none selected, needs the range picked first.
	if (!node.bind.param.valid() || ctx.sound == nullptr) {
		return Permission::Yes;
	}

	// `:2049` -- a kit row is always range-unambiguous (one range per drum), and so is a source
	// with exactly one range. `context_for` has already pointed `ctx.range` at that range.
	if (ctx.is_kit || range_count(ctx) <= 1) {
		return Permission::Yes;
	}

	// `:2054-2056` -- a range is already selected for this source index (`ctx.source_index` is what
	// `context_for` resolved `ctx.source`/`ctx.range` against, so the legacy
	// `currentSourceIndex == whichThing` test holds by construction).
	if (ctx.range != nullptr) {
		return Permission::Yes;
	}

	return Permission::MustSelectRange; // `:2058`
}

} // namespace deluge::scenes::menu
