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
/// @brief The randomizer page's `get`/`set` hooks and its exported root span. The `Node` literals
///        are in `sound_randomizer.h`.
///
/// Only the lock and the eleven `midi_cv` columns need hooks: the sound-side columns are unpatched
/// `ParamRef`s that `bindings::read`/`write` already serve.

#include "scenes/menu/nodes/sound_randomizer.h"

#include "scenes/menu/bindings.h"
#include "scenes/menu/context.h"

#include "definitions_cxx.hpp"
#include "model/drum/drum.h"
#include "model/instrument/kit.h"
#include "model/output.h"
#include "modulation/arpeggiator.h"

namespace deluge::scenes::menu::nodes {

int32_t get_randomizer_lock(const MenuContext& ctx) {
	return (ctx.arp != nullptr && ctx.arp->randomizerLock) ? 1 : 0; // `randomizer_lock.h:29 readCurrentValue`
}

void set_randomizer_lock(const MenuContext& ctx, int32_t value) {
	// `RandomizerLock::writeCurrentValue` (`randomizer_lock.h:16-32`): the affect-entire sweep
	// covers every `Drum`, not just the sound ones -- the same loop `sound_arp.cpp` explains.
	const bool on = value != 0;
	if (bindings::writes_whole_kit(ctx) && ctx.output != nullptr) {
		for (Drum* drum = static_cast<Kit*>(ctx.output)->firstDrum; drum != nullptr; drum = drum->next) {
			drum->arpSettings.randomizerLock = on;
		}
		return;
	}
	if (ctx.arp != nullptr) {
		ctx.arp->randomizerLock = on;
	}
}

/// @brief The eleven `midi_cv` hook pairs `sound_randomizer.h`'s `DELUGE_RANDOMIZER_MIDI` declared.
///
/// Every one is the same three lines against a different `uint32_t` field of `ArpeggiatorSettings`
/// (`modulation/arpeggiator.h:139-147`), through the unsigned value scaling
/// (`randomizer/midi_cv/spread_gate.h:27-32` and its ten siblings).
#define DELUGE_RANDOMIZER_MIDI_HOOKS(node_name, field)                                                                 \
	int32_t get_##node_name(const MenuContext& ctx) {                                                                  \
		return ctx.arp != nullptr ? bindings::unsigned_to_menu(ctx.arp->field) : 0;                                    \
	}                                                                                                                  \
	void set_##node_name(const MenuContext& ctx, int32_t value) {                                                      \
		if (ctx.arp != nullptr) {                                                                                      \
			ctx.arp->field = bindings::menu_to_unsigned(value);                                                        \
		}                                                                                                              \
	}

DELUGE_RANDOMIZER_MIDI_HOOKS(spread_gate_midi, spreadGate)
DELUGE_RANDOMIZER_MIDI_HOOKS(spread_octave_midi, spreadOctave)
DELUGE_RANDOMIZER_MIDI_HOOKS(spread_velocity_midi, spreadVelocity)
DELUGE_RANDOMIZER_MIDI_HOOKS(ratchet_amount_midi, ratchetAmount)
DELUGE_RANDOMIZER_MIDI_HOOKS(ratchet_probability_midi, ratchetProbability)
DELUGE_RANDOMIZER_MIDI_HOOKS(chord_polyphony_midi, chordPolyphony)
DELUGE_RANDOMIZER_MIDI_HOOKS(chord_probability_midi, chordProbability)
DELUGE_RANDOMIZER_MIDI_HOOKS(note_probability_midi, noteProbability)
DELUGE_RANDOMIZER_MIDI_HOOKS(swap_probability_midi, swapProbability)
DELUGE_RANDOMIZER_MIDI_HOOKS(bass_probability_midi, bassProbability)
DELUGE_RANDOMIZER_MIDI_HOOKS(glide_probability_midi, glideProbability)

#undef DELUGE_RANDOMIZER_MIDI_HOOKS

namespace {
constexpr const Node* kRoots[] = {&randomizer};
} // namespace

const std::span<const Node* const> kSoundRandomizerChildren{kRoots};

} // namespace deluge::scenes::menu::nodes
