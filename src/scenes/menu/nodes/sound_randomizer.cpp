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

namespace deluge::scenes::menu::nodes {

namespace {
constexpr const Node* kRoots[] = {&randomizer};
} // namespace

const std::span<const Node* const> kSoundRandomizerChildren{kRoots};

// The kit's own effects' MIDI/CV columns: main lists them by the kit's selected drum (`editingNonAudioDrumRow()`,
// `editingMidiDrumRow()`) and, for most, the kit arp's mode; their edits go to the kit's params, which the kit's arp
// reads on every render.
namespace {

bool kit_arp_on(const MenuContext& ctx) {
	return bindings::read(arp_mode, ctx) != 0;
}
/// `randomizer::midi_cv::RandomizerNonSoundInteger` and `arpeggiator::midi_cv::ArpNonSoundInteger` with the arp on.
bool non_audio_arp_on(const MenuContext& ctx) {
	return ctx.non_audio_drum_row && kit_arp_on(ctx);
}
/// `midi_cv::NoteProbability`, `Rhythm` and `SequenceLength`: a non-audio drum, arp or not.
bool non_audio(const MenuContext& ctx) {
	return ctx.non_audio_drum_row;
}
/// `midi_cv::SpreadVelocity`: a MIDI drum, arp or not.
bool midi_drum(const MenuContext& ctx) {
	return ctx.non_audio_drum_row && !ctx.gate_drum_row;
}
/// The `midi_cv` items that ask `editingMidiDrumRow()` with the arp on.
bool midi_drum_arp_on(const MenuContext& ctx) {
	return midi_drum(ctx) && kit_arp_on(ctx);
}

struct KitFxEntry {
	const Node* column;
	bindings::KitFxTwin twin;
};

constexpr KitFxEntry kKitFxTwins[] = {
    {&spread_gate_midi, {&spread_gate, non_audio_arp_on}},
    {&spread_octave_midi, {&spread_octave, midi_drum_arp_on}},
    {&spread_velocity_midi, {&spread_velocity, midi_drum}},
    {&ratchet_amount_midi, {&ratchet_amount, non_audio_arp_on}},
    {&ratchet_probability_midi, {&ratchet_probability, non_audio_arp_on}},
    {&note_probability_midi, {&note_probability, non_audio}},
    {&swap_probability_midi, {&swap_probability, midi_drum_arp_on}},
    {&bass_probability_midi, {&bass_probability, midi_drum_arp_on}},
    {&glide_probability_midi, {&glide_probability, midi_drum_arp_on}},
    {&arp_rhythm_midi, {&arp_rhythm, non_audio}},
    {&arp_sequence_length_midi, {&arp_sequence_length, non_audio}},
};

} // namespace

} // namespace deluge::scenes::menu::nodes

namespace deluge::scenes::menu::bindings {

const KitFxTwin* kit_fx_twin(const Node& node) {
	for (const nodes::KitFxEntry& entry : nodes::kKitFxTwins) {
		if (entry.column == &node) {
			return &entry.twin;
		}
	}
	return nullptr;
}

} // namespace deluge::scenes::menu::bindings
