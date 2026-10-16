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
///       (`arpeggiator/mode.h:35-38`, `octaves.h:24-29`, ...). `apply_to_arp` below is that loop,
///       each write an Edit on the drum's lane arp or the clip's arp, which silences what the arp
///       played as it turns on or off and restarts it as its pattern changes.

#include "scenes/menu/nodes/sound_arp.h"

#include "scenes/menu/bindings.h"
#include "scenes/menu/context.h"

#include "definitions_cxx.hpp"
#include "model/clip/clip.h"
#include "model/drum/drum.h"
#include "model/edits/arp_setting_for.h"
#include "model/instrument/kit.h"
#include "model/output.h"
#include "modulation/arpeggiator.h"
#include "modulation/sync.h"
#include "processing/sound/sound.h"

namespace deluge::scenes::menu::nodes {

namespace {

using deluge::serialization::Value;

/// @brief Legacy's arpeggiator affect-entire loop, once: every `Drum` in the kit when the
///        AFFECT-ENTIRE button is held on a kit row, else the one selected arp; each write is the
///        arp setting's Edit at @p path.
///
/// @param filter_gate_drums Several items skip gate drums inside the loop and say why they do
///                          ("we need to apply the same filtering as stated in the isRelevant()
///                          function", `octaves.h:25-26`). Passing the filter here keeps it exactly
///                          where legacy put it -- inside the sweep, not around it.
void apply_to_arp(const MenuContext& ctx, bool filter_gate_drums, std::string_view path, const Value& value) {
	if (bindings::writes_whole_kit(ctx) && ctx.output != nullptr) {
		for (Drum* drum = static_cast<Kit*>(ctx.output)->firstDrum; drum != nullptr; drum = drum->next) {
			if (filter_gate_drums && drum->type == DrumType::GATE) {
				continue;
			}
			deluge::edits::laneArpSettingFor(*drum, path, value);
		}
		return;
	}
	if (ctx.arp != nullptr) {
		deluge::edits::arpSettingFor(*ctx.arp, path, value);
	}
}

Value number(int64_t n) {
	return Value::makeNumber(n);
}

} // namespace

// ---------------------------------------------------------------------------------------------
// Mode and preset

int32_t get_arp_mode(const MenuContext& ctx) {
	return ctx.arp != nullptr ? static_cast<int32_t>(ctx.arp->mode) : 0; // `mode.h:33`
}
void set_arp_mode(const MenuContext& ctx, int32_t value) {
	apply_to_arp(ctx, /*filter_gate_drums=*/false, "arpeggiator/arpMode",
	             deluge::edits::arpChoice("arpeggiator/arpMode", value));
}

int32_t get_arp_preset(const MenuContext& ctx) {
	return ctx.arp != nullptr ? static_cast<int32_t>(ctx.arp->preset) : 0; // `preset_mode.h:37 readCurrentValue`
}
void set_arp_preset(const MenuContext& ctx, int32_t value) {
	apply_to_arp(ctx, /*filter_gate_drums=*/false, "arpeggiator/preset",
	             deluge::edits::arpChoice("arpeggiator/preset", value));
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
	// `sync.h:29-30`, `:36-37`
	apply_to_arp(ctx, /*filter_gate_drums=*/false, "arpeggiator/syncType",
	             number(std::to_underlying(syncValueToSyncType(value))));
	apply_to_arp(ctx, /*filter_gate_drums=*/false, "arpeggiator/syncLevel",
	             number(deluge::engine::index_of(syncValueToSyncLevel(value))));
}

// ---------------------------------------------------------------------------------------------
// The pattern page

int32_t get_arp_octaves(const MenuContext& ctx) {
	return ctx.arp != nullptr ? ctx.arp->numOctaves : 1; // `octaves.h:29 readCurrentValue`
}
void set_arp_octaves(const MenuContext& ctx, int32_t value) {
	apply_to_arp(ctx, /*filter_gate_drums=*/true, "arpeggiator/numOctaves", number(value));
}

int32_t get_arp_step_repeat(const MenuContext& ctx) {
	return ctx.arp != nullptr ? ctx.arp->numStepRepeats : 1; // `step_repeat.h:29 readCurrentValue`
}
void set_arp_step_repeat(const MenuContext& ctx, int32_t value) {
	apply_to_arp(ctx, /*filter_gate_drums=*/true, "arpeggiator/stepRepeat", number(value));
}

int32_t get_arp_octave_mode(const MenuContext& ctx) {
	return ctx.arp != nullptr ? static_cast<int32_t>(ctx.arp->octaveMode) : 0; // `octave_mode.h:32 readCurrentValue`
}
void set_arp_octave_mode(const MenuContext& ctx, int32_t value) {
	apply_to_arp(ctx, /*filter_gate_drums=*/true, "arpeggiator/octaveMode",
	             deluge::edits::arpChoice("arpeggiator/octaveMode", value));
}

int32_t get_arp_note_mode(const MenuContext& ctx) {
	return ctx.arp != nullptr ? static_cast<int32_t>(ctx.arp->noteMode) : 0; // `note_mode.h:28 readCurrentValue`
}
void set_arp_note_mode(const MenuContext& ctx, int32_t value) {
	// `NoteMode::writeCurrentValue` (`note_mode.h:29-36`) has NO affect-entire branch -- the class
	// does not override `usesAffectEntire()` -- so this writes the selected settings only, and it is
	// the one arp item that regenerates the note pattern.
	if (ctx.arp != nullptr) {
		deluge::edits::arpSettingFor(*ctx.arp, "arpeggiator/noteMode",
		                             deluge::edits::arpChoice("arpeggiator/noteMode", value));
	}
}

int32_t get_arp_note_mode_drums(const MenuContext& ctx) {
	// `note_mode_for_drums.h:31 readCurrentValue`
	return ctx.arp != nullptr ? static_cast<int32_t>(ctx.arp->noteMode) : 0;
}
void set_arp_note_mode_drums(const MenuContext& ctx, int32_t value) {
	apply_to_arp(ctx, /*filter_gate_drums=*/true, "arpeggiator/noteMode",
	             deluge::edits::arpChoice("arpeggiator/noteMode", value));
}

int32_t get_arp_chord_simulator(const MenuContext& ctx) {
	return ctx.arp != nullptr ? ctx.arp->chordTypeIndex : 0; // `chord_type.h:33 readCurrentValue`
}
void set_arp_chord_simulator(const MenuContext& ctx, int32_t value) {
	if (value < 0 || value >= MAX_CHORD_TYPES) {
		return; // `chord_type.h:38-40`
	}
	apply_to_arp(ctx, /*filter_gate_drums=*/true, "arpeggiator/chordType", number(value));
}

int32_t get_arp_rhythm_midi(const MenuContext& ctx) {
	// `midi_cv::Rhythm::readCurrentValue` (`arpeggiator/midi_cv/rhythm.h:33-35`).
	return ctx.arp != nullptr ? bindings::unsigned_to_menu(ctx.arp->rhythm) : 0;
}
void set_arp_rhythm_midi(const MenuContext& ctx, int32_t value) {
	if (ctx.arp != nullptr) { // `midi_cv/rhythm.h:36-39`
		deluge::edits::arpSettingFor(*ctx.arp, "arpeggiator/rhythm",
		                             number(static_cast<int32_t>(bindings::menu_to_unsigned(value))));
	}
}

int32_t get_arp_sequence_length_midi(const MenuContext& ctx) {
	// `midi_cv::SequenceLength::readCurrentValue` (`midi_cv/sequence_length.h:28-30`).
	return ctx.arp != nullptr ? bindings::unsigned_to_menu(ctx.arp->sequenceLength) : 0;
}
void set_arp_sequence_length_midi(const MenuContext& ctx, int32_t value) {
	if (ctx.arp != nullptr) { // `midi_cv/sequence_length.h:31-34`
		deluge::edits::arpSettingFor(*ctx.arp, "arpeggiator/sequenceLength",
		                             number(static_cast<int32_t>(bindings::menu_to_unsigned(value))));
	}
}

// ---------------------------------------------------------------------------------------------
// The MPE page, and the kit-arp toggle

int32_t get_arp_mpe_velocity(const MenuContext& ctx) {
	return ctx.arp != nullptr ? static_cast<int32_t>(ctx.arp->mpeVelocity) : 0; // `mpe_velocity.h:29 readCurrentValue`
}
void set_arp_mpe_velocity(const MenuContext& ctx, int32_t value) {
	apply_to_arp(ctx, /*filter_gate_drums=*/true, "arpeggiator/mpeVelocity",
	             deluge::edits::arpChoice("arpeggiator/mpeVelocity", value));
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
	const Value on = number(value != 0 ? 1 : 0);

	// `:28-47` -- the affect-entire sweep asks **each** drum its own `allowNoteTails` and skips the
	// ones that refuse (a MIDI or gate drum has no `Sound` and is always allowed, `:33`). It does
	// *not* consult the context's answer (review round 1, I3); `bindings::drum_allows_note_tails`
	// is the per-drum test, on that drum's own model stack.
	if (bindings::writes_whole_kit(ctx) && ctx.output != nullptr) {
		for (Drum* drum = static_cast<Kit*>(ctx.output)->firstDrum; drum != nullptr; drum = drum->next) {
			if (bindings::drum_allows_note_tails(ctx, drum)) {
				deluge::edits::laneArpSettingFor(*drum, "arpeggiator/kitArp", on); // `:43-45`
			}
		}
		return;
	}

	// `:48-53` -- one sound, gated on the context's own cached answer, which is what
	// `soundEditor.allowsNoteTails` is (`sound_editor.cpp:1866-1871`).
	if (ctx.arp != nullptr && bindings::allows_note_tails(ctx)) {
		deluge::edits::arpSettingFor(*ctx.arp, "arpeggiator/kitArp", on);
	}
}

namespace {
constexpr const Node* kRoots[] = {&arp};
} // namespace

const std::span<const Node* const> kSoundArpChildren{kRoots};

} // namespace deluge::scenes::menu::nodes
