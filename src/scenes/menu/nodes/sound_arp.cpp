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
/// @brief The arpeggiator subtree's `get`/`set` hooks and its exported root span. The `Node`
///        literals are in `sound_arp.h`.
///
/// @note **Why these hooks do not use `bindings::apply_to_targets`.** That helper's affect-entire
///       sweep visits `ModControllableAudio`s -- `SoundDrum`s (`bindings.cpp`'s loop). Every
///       arpeggiator item's own loop visits **every** `Drum` in the kit, MIDI and gate drums
///       included, because `ArpeggiatorSettings` lives on `Drum`, not on `Sound`
///       (`arpeggiator/mode.h:35-38`, `octaves.h:24-29`, ...). `apply_to_arp_settings` below is
///       that loop. It needs no model stack, so it lives here rather than in `bindings.cpp`.

#include "scenes/menu/nodes/sound_arp.h"

#include "scenes/menu/bindings.h"
#include "scenes/menu/context.h"

#include "definitions_cxx.hpp"
#include "model/clip/clip.h"
#include "model/drum/drum.h"
#include "model/instrument/kit.h"
#include "model/output.h"
#include "model/sync.h"
#include "modulation/arpeggiator.h"
#include "processing/sound/sound.h"

namespace deluge::scenes::menu::nodes {

namespace {

/// @brief Legacy's arpeggiator affect-entire loop, once: every `Drum` in the kit when the
///        AFFECT-ENTIRE button is held on a kit row, else the one selected thing.
///
/// @param filter_gate_drums Several items skip gate drums inside the loop and say why they do
///                          ("we need to apply the same filtering as stated in the isRelevant()
///                          function", `octaves.h:25-26`). Passing the filter here keeps it exactly
///                          where legacy put it -- inside the sweep, not around it.
template <typename Fn>
void apply_to_arp_settings(const MenuContext& ctx, bool filter_gate_drums, Fn&& fn) {
	if (bindings::writes_whole_kit(ctx) && ctx.output != nullptr) {
		for (Drum* drum = static_cast<Kit*>(ctx.output)->firstDrum; drum != nullptr; drum = drum->next) {
			if (filter_gate_drums && drum->type == DrumType::GATE) {
				continue;
			}
			fn(drum->arpSettings);
		}
		return;
	}
	if (ctx.arp != nullptr) {
		fn(*ctx.arp);
	}
}

/// @brief `soundEditor.editingCVOrMIDIClip()` (`gui/ui/sound_editor.cpp:173-175`).
bool cv_or_midi_clip(const MenuContext& ctx) {
	return ctx.output != nullptr && (ctx.output->type == OutputType::MIDI_OUT || ctx.output->type == OutputType::CV);
}

/// @brief The "if was off, or is now becoming off, silence what is playing" arm shared by
///        `arpeggiator::Mode::writeCurrentValue` (`arpeggiator/mode.h:23-67`) and
///        `PresetMode::writeCurrentValue` (`preset_mode.h:27-72`), which are the same code twice.
///
/// @note **The MIDI/CV arm is not transcribed.** Legacy calls
///       `getCurrentInstrumentClip()->stopAllNotesForMIDIOrCV(modelStack->toWithTimelineCounter())`
///       (`mode.h:56-57`), which needs a timeline-counter model stack -- reachable only from
///       `bindings.cpp` (the `scenes-model-stack-outside-context` ratchet) -- and only ever fires on
///       a MIDI or CV clip, whose root menus are Task 8's, not this task's. Recorded, not dropped.
void silence_for_arp_toggle(const MenuContext& ctx, bool becoming_off) {
	if (ctx.arp == nullptr) {
		return;
	}
	// `mode.h:45`, `:59` -- only when the arpeggiator was already off or is being switched off.
	if (ctx.arp->mode != ArpMode::OFF && !becoming_off) {
		return;
	}
	// `mode.h:30`, `:44` -- and only when the clip is actually sounding, and not on a kit's own
	// global-FX page.
	if (ctx.clip == nullptr || !ctx.clip->isActiveOnOutput() || (ctx.is_kit && ctx.is_kit_global_fx)) {
		return;
	}

	if (ctx.is_kit && ctx.output != nullptr) {
		auto* kit = static_cast<Kit*>(ctx.output);
		if (bindings::writes_whole_kit(ctx)) {
			kit->cutAllSound(); // `mode.h:31`
		}
		else if (kit->selectedDrum != nullptr) {
			kit->selectedDrum->killAllVoices(); // `mode.h:50-53`
		}
		return;
	}
	if (cv_or_midi_clip(ctx)) {
		return; // see the note above
	}
	// `mode.h:59-65` -- the synth arm. `apply_to_targets` is used purely to obtain the sound's own
	// `ModelStackWithSoundFlags`; off a kit row it runs exactly once, on `ctx.sound`.
	bindings::apply_to_targets(ctx, 0, 0, [](const bindings::SoundTarget& t, int32_t, int32_t, const MenuContext&) {
		if (t.sound == nullptr || t.stack == nullptr) {
			return;
		}
		t.sound->allNotesOff(t.stack, t.sound->getArp());
		t.sound->reassessRenderSkippingStatus(t.stack);
	});
}

} // namespace

// ---------------------------------------------------------------------------------------------
// Mode and preset

int32_t get_arp_mode(const MenuContext& ctx) {
	return ctx.arp != nullptr ? static_cast<int32_t>(ctx.arp->mode) : 0; // `mode.h:33`
}
void set_arp_mode(const MenuContext& ctx, int32_t value) {
	const auto mode = static_cast<ArpMode>(value);
	silence_for_arp_toggle(ctx, mode == ArpMode::OFF);
	apply_to_arp_settings(ctx, /*filter_gate_drums=*/false, [mode](ArpeggiatorSettings& arp) {
		arp.mode = mode;                       // `mode.h:52`, `:85`
		arp.updatePresetFromCurrentSettings(); // `mode.h:53 writeCurrentValue`, `:86`
	});
}

int32_t get_arp_preset(const MenuContext& ctx) {
	return ctx.arp != nullptr ? static_cast<int32_t>(ctx.arp->preset) : 0; // `preset_mode.h:37 readCurrentValue`
}
void set_arp_preset(const MenuContext& ctx, int32_t value) {
	const auto preset = static_cast<ArpPreset>(value);
	silence_for_arp_toggle(ctx, preset == ArpPreset::OFF);
	apply_to_arp_settings(ctx, /*filter_gate_drums=*/false, [preset](ArpeggiatorSettings& arp) {
		arp.preset = preset;                   // `preset_mode.h:56 writeCurrentValue`, `:90`
		arp.updateSettingsFromCurrentPreset(); // `:41`, `:75`
		arp.flagForceArpRestart = true;        // `:42`, `:76`
	});
}

// ---------------------------------------------------------------------------------------------
// The basic page

int32_t get_arp_sync(const MenuContext& ctx) {
	if (ctx.arp == nullptr) {
		return 0;
	}
	// `arpeggiator::Sync::readCurrentValue` (`arpeggiator/sync.h:29-32`) via
	// `SyncLevel::syncTypeAndLevelToMenuOption` (`gui/menu_item/sync_level.cpp:79-81`), transcribed
	// the same way `sound_lfo.cpp` transcribes it.
	return static_cast<int32_t>(ctx.arp->syncType)
	       + (static_cast<int32_t>(ctx.arp->syncLevel) - (ctx.arp->syncType != SYNC_TYPE_EVEN ? 1 : 0));
}
void set_arp_sync(const MenuContext& ctx, int32_t value) {
	apply_to_arp_settings(ctx, /*filter_gate_drums=*/false, [value](ArpeggiatorSettings& arp) {
		arp.syncType = syncValueToSyncType(value); // `sync.h:29-30`, `:36-37`
		arp.syncLevel = syncValueToSyncLevel(value);
	});
}

// ---------------------------------------------------------------------------------------------
// The pattern page

int32_t get_arp_octaves(const MenuContext& ctx) {
	return ctx.arp != nullptr ? ctx.arp->numOctaves : 1; // `octaves.h:29 readCurrentValue`
}
void set_arp_octaves(const MenuContext& ctx, int32_t value) {
	apply_to_arp_settings(ctx, /*filter_gate_drums=*/true,
	                      [value](ArpeggiatorSettings& arp) { arp.numOctaves = static_cast<uint8_t>(value); });
}

int32_t get_arp_step_repeat(const MenuContext& ctx) {
	return ctx.arp != nullptr ? ctx.arp->numStepRepeats : 1; // `step_repeat.h:29 readCurrentValue`
}
void set_arp_step_repeat(const MenuContext& ctx, int32_t value) {
	apply_to_arp_settings(ctx, /*filter_gate_drums=*/true,
	                      [value](ArpeggiatorSettings& arp) { arp.numStepRepeats = static_cast<uint8_t>(value); });
}

int32_t get_arp_octave_mode(const MenuContext& ctx) {
	return ctx.arp != nullptr ? static_cast<int32_t>(ctx.arp->octaveMode) : 0; // `octave_mode.h:32 readCurrentValue`
}
void set_arp_octave_mode(const MenuContext& ctx, int32_t value) {
	const auto mode = static_cast<ArpOctaveMode>(value);
	apply_to_arp_settings(ctx, /*filter_gate_drums=*/true, [mode](ArpeggiatorSettings& arp) {
		arp.octaveMode = mode;                 // `octave_mode.h:30`, `:38`
		arp.updatePresetFromCurrentSettings(); // `:31`, `:39`
		arp.flagForceArpRestart = true;        // `:32`, `:40`
	});
}

int32_t get_arp_note_mode(const MenuContext& ctx) {
	return ctx.arp != nullptr ? static_cast<int32_t>(ctx.arp->noteMode) : 0; // `note_mode.h:28 readCurrentValue`
}
void set_arp_note_mode(const MenuContext& ctx, int32_t value) {
	// `NoteMode::writeCurrentValue` (`note_mode.h:29-36`) has NO affect-entire branch -- the class
	// does not override `usesAffectEntire()` -- so this writes the selected settings only, and it is
	// the one arp item that regenerates the note pattern.
	if (ctx.arp == nullptr) {
		return;
	}
	ctx.arp->noteMode = static_cast<ArpNoteMode>(value);
	ctx.arp->updatePresetFromCurrentSettings();
	if (ctx.arp->noteMode == ArpNoteMode::PATTERN) {
		ctx.arp->generateNewNotePattern();
	}
	ctx.arp->flagForceArpRestart = true;
}

int32_t get_arp_note_mode_drums(const MenuContext& ctx) {
	// `note_mode_for_drums.h:31 readCurrentValue`
	return ctx.arp != nullptr ? static_cast<int32_t>(ctx.arp->noteMode) : 0;
}
void set_arp_note_mode_drums(const MenuContext& ctx, int32_t value) {
	const auto mode = static_cast<ArpNoteMode>(value);
	apply_to_arp_settings(ctx, /*filter_gate_drums=*/true, [mode](ArpeggiatorSettings& arp) {
		arp.noteMode = mode;                   // `note_mode_for_drums.h:29`, `:37`
		arp.updatePresetFromCurrentSettings(); // `:30`, `:38` -- no `generateNewNotePattern` here
		arp.flagForceArpRestart = true;        // `:31`, `:39`
	});
}

int32_t get_arp_chord_simulator(const MenuContext& ctx) {
	return ctx.arp != nullptr ? ctx.arp->chordTypeIndex : 0; // `chord_type.h:33 readCurrentValue`
}
void set_arp_chord_simulator(const MenuContext& ctx, int32_t value) {
	if (value < 0 || value >= MAX_CHORD_TYPES) {
		return; // `chord_type.h:38-40`
	}
	apply_to_arp_settings(ctx, /*filter_gate_drums=*/true, [value](ArpeggiatorSettings& arp) {
		arp.chordTypeIndex = static_cast<uint8_t>(value); // `chord_type.h:50 writeCurrentValue`, `:57`
		arp.flagForceArpRestart = true;                   // `:35`, `:42`
	});
}

int32_t get_arp_rhythm_midi(const MenuContext& ctx) {
	// `midi_cv::Rhythm::readCurrentValue` (`arpeggiator/midi_cv/rhythm.h:33-35`).
	return ctx.arp != nullptr ? bindings::unsigned_to_menu(ctx.arp->rhythm) : 0;
}
void set_arp_rhythm_midi(const MenuContext& ctx, int32_t value) {
	if (ctx.arp != nullptr) {
		ctx.arp->rhythm = bindings::menu_to_unsigned(value); // `midi_cv/rhythm.h:36-39`
	}
}

int32_t get_arp_sequence_length_midi(const MenuContext& ctx) {
	// `midi_cv::SequenceLength::readCurrentValue` (`midi_cv/sequence_length.h:28-30`).
	return ctx.arp != nullptr ? bindings::unsigned_to_menu(ctx.arp->sequenceLength) : 0;
}
void set_arp_sequence_length_midi(const MenuContext& ctx, int32_t value) {
	if (ctx.arp != nullptr) {
		ctx.arp->sequenceLength = bindings::menu_to_unsigned(value); // `midi_cv/sequence_length.h:31-34`
	}
}

// ---------------------------------------------------------------------------------------------
// The MPE page, and the kit-arp toggle

int32_t get_arp_mpe_velocity(const MenuContext& ctx) {
	return ctx.arp != nullptr ? static_cast<int32_t>(ctx.arp->mpeVelocity) : 0; // `mpe_velocity.h:29 readCurrentValue`
}
void set_arp_mpe_velocity(const MenuContext& ctx, int32_t value) {
	const auto source = static_cast<ArpMpeModSource>(value);
	apply_to_arp_settings(ctx, /*filter_gate_drums=*/true,
	                      [source](ArpeggiatorSettings& arp) { arp.mpeVelocity = source; });
}

int32_t get_arp_include_in_kit(const MenuContext& ctx) {
	// `IncludeInKitArp::readCurrentValue` (`include_in_kit_arp.h:31-38`): a sound that cannot hold a
	// note tail always reads OFF.
	if (ctx.arp == nullptr || !bindings::allows_note_tails(ctx)) {
		return 0;
	}
	return ctx.arp->includeInKitArp ? 1 : 0;
}
void set_arp_include_in_kit(const MenuContext& ctx, int32_t value) {
	// `IncludeInKitArp::writeCurrentValue` (`include_in_kit_arp.h:24-54`), both arms exactly.
	const bool on = value != 0;

	// `:28-47` -- the affect-entire sweep asks **each** drum its own `allowNoteTails` and skips the
	// ones that refuse (a MIDI or gate drum has no `Sound` and is always allowed, `:33`). It does
	// *not* consult the context's answer (review round 1, I3); `bindings::drum_allows_note_tails`
	// is the per-drum test, on that drum's own model stack.
	if (bindings::writes_whole_kit(ctx) && ctx.output != nullptr) {
		for (Drum* drum = static_cast<Kit*>(ctx.output)->firstDrum; drum != nullptr; drum = drum->next) {
			if (bindings::drum_allows_note_tails(ctx, drum)) {
				drum->arpSettings.includeInKitArp = on; // `:43-45`
			}
		}
		return;
	}

	// `:48-53` -- one sound, gated on the context's own cached answer, which is what
	// `soundEditor.allowsNoteTails` is (`sound_editor.cpp:1811-1816`).
	if (ctx.arp != nullptr && bindings::allows_note_tails(ctx)) {
		ctx.arp->includeInKitArp = on;
	}
}

namespace {
constexpr const Node* kRoots[] = {&arp};
} // namespace

const std::span<const Node* const> kSoundArpChildren{kRoots};

} // namespace deluge::scenes::menu::nodes
