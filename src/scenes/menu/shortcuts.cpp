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
/// @brief The seven legacy shortcut tables, transcribed cell by cell as `Node` pointers.
///
/// Each table below is written **column-major, exactly as legacy writes it** -- one braced line per
/// pad column `x`, eight cells `y = 0..7` -- so a reviewer can diff it line for line against
/// `gui/ui/menus.cpp`. `from_columns` transposes them once, at compile time, into the row-major
/// `Cells` the lookups index. A `comingSoonMenu` cell becomes `nullptr` (legacy pops up
/// "unimplemented" and goes nowhere, `sound_editor.cpp:1330`) and says so in the line comment.
///
/// The transcription is not trusted on its own: `sim/scenes_spec/shortcuts_spec.cpp` walks every
/// `(x, y)` of every legacy array and of every port layer and asserts the two agree in both
/// directions.
///
/// @note **The `SHORTCUTS_VERSION_1` layer is not ported.** `SoundEditor::setShortcutsVersion`
///       (`sound_editor.cpp:111-137`) *mutates* six cells of `paramShortcutsForSounds` and four of
///       `paramShortcutsForAudioClips` in place when the user selects the pre-3.0 layout. These
///       tables are the default (`SHORTCUTS_VERSION_3`) layer, which is what the arrays hold as
///       written. Restoring the alternate layout is E2's, along with the `shortcuts_version`
///       setting node that selects it.

#include "scenes/menu/shortcuts.h"

#include "scenes/menu/node.h"
#include "scenes/menu/nodes/nodes.h"

#include <array>
#include <cstddef>

namespace deluge::scenes::menu {

namespace {

namespace n = nodes;

/// @brief One layer, row-major: `cells[y * kShortcutCols + x]`.
using Cells = std::array<const Node*, static_cast<size_t>(kShortcutCols) * kShortcutRows>;

constexpr size_t cell_index(uint8_t x, uint8_t y) {
	return static_cast<size_t>(y) * kShortcutCols + x;
}

/// @brief Transpose a legacy `[x][y]` table into row-major cells.
constexpr Cells from_columns(const Node* const (&cols)[kShortcutCols][kShortcutRows]) {
	Cells out{};
	for (uint8_t x = 0; x < kShortcutCols; x++) {
		for (uint8_t y = 0; y < kShortcutRows; y++) {
			out[cell_index(x, y)] = cols[x][y];
		}
	}
	return out;
}

/// @brief A legacy one-dimensional table, laid into column 11 -- the column
///        `potentialShortcutPadAction` (`sound_editor.cpp:1277`) and `updatePadLightsFor`
///        (`:836`, `:848`) index it at.
constexpr Cells from_column11(const Node* const (&col)[kShortcutRows]) {
	Cells out{};
	for (uint8_t y = 0; y < kShortcutRows; y++) {
		out[cell_index(11, y)] = col[y];
	}
	return out;
}

/// @brief @p base with the name-editor cell swapped for the drum one.
///
/// Legacy does this at lookup time rather than in the table: `potentialShortcutPadAction`
/// (`sound_editor.cpp:1279-1281`, `:1306-1308`) rewrites `editNameMenu` to `drumNameEditMenu` for a
/// kit output or a non-audio drum row, and `updatePadLightsFor` (`:843-845`, `:858-861`) does the
/// same on the way back. Baking it into a per-root layer keeps both directions of the lookup pure
/// table reads, and keeps `shortcut_layer` honest about what a root's grid actually holds.
constexpr Cells with_drum_name(Cells base) {
	base[cell_index(11, 5)] = &n::drum_name_edit;
	return base;
}

constexpr Cells kNoCells{};

// clang-format off

/// `paramShortcutsForSounds` (`gui/ui/menus.cpp:2172`).
constexpr const Node* kSoundColumns[kShortcutCols][kShortcutRows] = {
    {&n::sample0_repeat   , &n::sample0_reverse     , &n::sample0_time_stretch, &n::sample0_pitch_speed, &n::sample0_recorder     , &n::file0_selector     , &n::sample0_interpolation, &n::sample0_start}, // x=0
    {&n::sample1_repeat   , &n::sample1_reverse     , &n::sample1_time_stretch, &n::sample1_pitch_speed, &n::sample1_recorder     , &n::file1_selector     , &n::sample1_interpolation, &n::sample1_start}, // x=1
    {&n::source0_volume   , &n::source0_transpose   , &n::osc0_type           , &n::osc0_pulse_width   , &n::osc0_phase           , &n::source0_feedback   , &n::source0_wave_index   , &n::noise}, // x=2
    {&n::source1_volume   , &n::source1_transpose   , &n::osc1_type           , &n::osc1_pulse_width   , &n::osc1_phase           , &n::source1_feedback   , &n::source1_wave_index   , &n::osc_sync}, // x=3
    {&n::modulator0_volume, &n::modulator0_transpose, nullptr                 , nullptr                , &n::modulator0_phase_node, &n::modulator0_feedback, nullptr                  , &n::sequence_direction}, // x=4; comingSoonMenu at y=2,3,6
    {&n::modulator1_volume, &n::modulator1_transpose, nullptr                 , nullptr                , &n::modulator1_phase_node, &n::modulator1_feedback, &n::modulator_dest       , &n::stutter_rate}, // x=5; comingSoonMenu at y=2,3
    {&n::master_volume    , &n::master_transpose    , &n::vibrato             , &n::master_pan         , &n::synth_mode           , &n::decimation         , &n::bitcrush             , &n::saturation}, // x=6
    {&n::portamento       , &n::polyphony           , &n::voice_priority      , &n::unison_detune      , &n::unison_count         , &n::comp_threshold     , nullptr                  , &n::wavefold}, // x=7
    {&n::env1_release     , &n::env1_sustain        , &n::env1_decay          , &n::env1_attack        , &n::lpf_morph            , &n::lpf_mode           , &n::lpf_res              , &n::lpf_freq}, // x=8
    {&n::env2_release     , &n::env2_sustain        , &n::env2_decay          , &n::env2_attack        , &n::hpf_morph            , &n::hpf_mode           , &n::hpf_res              , &n::hpf_freq}, // x=9
    {&n::sidechain_release, &n::sidechain_sync      , &n::sidechain_volume    , &n::sidechain_attack   , &n::sidechain_shape      , &n::sidechain_send     , &n::eq_bass              , &n::eq_bass_freq}, // x=10
    {&n::arp_rate         , &n::arp_sync            , &n::arp_gate            , &n::arp_octaves        , &n::arp_preset           , &n::edit_name          , &n::eq_treble            , &n::eq_treble_freq}, // x=11
    {&n::lfo1_rate        , &n::lfo1_sync           , &n::lfo1_type           , &n::mod_fx_type        , &n::mod_fx_offset        , &n::mod_fx_feedback    , &n::mod_fx_depth         , &n::mod_fx_rate}, // x=12
    {&n::lfo2_rate        , &n::lfo2_sync           , &n::lfo2_type           , &n::reverb_amount      , &n::reverb_pan           , &n::reverb_width       , &n::reverb_damping       , &n::reverb_room_size}, // x=13
    {&n::delay_rate       , &n::delay_sync          , &n::delay_type          , &n::delay_amount       , &n::delay_ping_pong      , nullptr                , nullptr                  , nullptr}, // x=14
    {nullptr              , &n::spread_velocity     , &n::randomizer_lock     , &n::note_probability   , nullptr                  , nullptr                , nullptr                  , nullptr}, // x=15
};

/// `paramShortcutsForSoundsSecondLayer` (`gui/ui/menus.cpp:2192`).
constexpr const Node* kSoundSecondColumns[kShortcutCols][kShortcutRows] = {
    {nullptr         , nullptr         , nullptr       , nullptr        , nullptr          , nullptr       , nullptr, nullptr}, // x=0
    {nullptr         , nullptr         , nullptr       , nullptr        , nullptr          , nullptr       , nullptr, nullptr}, // x=1
    {nullptr         , nullptr         , nullptr       , nullptr        , nullptr          , nullptr       , nullptr, nullptr}, // x=2
    {nullptr         , nullptr         , nullptr       , nullptr        , nullptr          , nullptr       , nullptr, nullptr}, // x=3
    {nullptr         , nullptr         , nullptr       , nullptr        , nullptr          , nullptr       , nullptr, nullptr}, // x=4
    {nullptr         , nullptr         , nullptr       , nullptr        , nullptr          , nullptr       , nullptr, &n::stutter_direction}, // x=5
    {nullptr         , nullptr         , nullptr       , nullptr        , nullptr          , nullptr       , nullptr, nullptr}, // x=6
    {nullptr         , &n::voice_count , nullptr       , nullptr        , &n::unison_spread, &n::comp_ratio, nullptr, nullptr}, // x=7
    {&n::env3_release, &n::env3_sustain, &n::env3_decay, &n::env3_attack, nullptr          , nullptr       , nullptr, nullptr}, // x=8
    {&n::env4_release, &n::env4_sustain, &n::env4_decay, &n::env4_attack, nullptr          , nullptr       , nullptr, nullptr}, // x=9
    {nullptr         , nullptr         , nullptr       , nullptr        , nullptr          , nullptr       , nullptr, nullptr}, // x=10
    {nullptr         , nullptr         , nullptr       , nullptr        , nullptr          , nullptr       , nullptr, nullptr}, // x=11
    {&n::lfo3_rate   , &n::lfo3_sync   , &n::lfo3_type , nullptr        , nullptr          , nullptr       , nullptr, nullptr}, // x=12
    {&n::lfo4_rate   , &n::lfo4_sync   , &n::lfo4_type , nullptr        , nullptr          , nullptr       , nullptr, nullptr}, // x=13
    {nullptr         , nullptr         , nullptr       , nullptr        , nullptr          , nullptr       , nullptr, nullptr}, // x=14
    {nullptr         , nullptr         , nullptr       , nullptr        , nullptr          , nullptr       , nullptr, nullptr}, // x=15
};

/// `paramShortcutsForAudioClips` (`gui/ui/menus.cpp:2211`).
///
/// `audio_clip_waveform` stands in for `audioClipSampleMarkerEditorMenuEnd`: the port has one
/// waveform/marker-editor node where legacy has a start and an end item, and only the end one is in
/// this table under `SHORTCUTS_VERSION_3`.
constexpr const Node* kAudioClipColumns[kShortcutCols][kShortcutRows] = {
    {nullptr              , &n::audio_clip_reverse  , nullptr                    , &n::sample0_pitch_speed , nullptr             , &n::file0_selector , &n::audio_clip_interpolation, &n::audio_clip_waveform}, // x=0
    {nullptr              , &n::audio_clip_reverse  , nullptr                    , &n::sample0_pitch_speed , nullptr             , &n::file0_selector , &n::audio_clip_interpolation, &n::audio_clip_waveform}, // x=1
    {nullptr              , &n::audio_clip_transpose, nullptr                    , nullptr                 , nullptr             , nullptr            , nullptr                     , nullptr}, // x=2
    {nullptr              , &n::audio_clip_transpose, nullptr                    , nullptr                 , nullptr             , nullptr            , nullptr                     , nullptr}, // x=3
    {nullptr              , nullptr                 , nullptr                    , nullptr                 , nullptr             , nullptr            , nullptr                     , nullptr}, // x=4
    {nullptr              , nullptr                 , nullptr                    , nullptr                 , nullptr             , nullptr            , nullptr                     , &n::stutter_rate}, // x=5
    {&n::global_level     , &n::audio_clip_transpose, nullptr                    , &n::global_pan          , nullptr             , &n::decimation     , &n::bitcrush                , &n::saturation}, // x=6
    {nullptr              , nullptr                 , &n::voice_priority         , nullptr                 , nullptr             , &n::comp_threshold , nullptr                     , nullptr}, // x=7; comingSoonMenu at y=7
    {nullptr              , nullptr                 , nullptr                    , &n::audio_clip_attack   , &n::global_lpf_morph, &n::lpf_mode       , &n::global_lpf_res          , &n::global_lpf_freq}, // x=8
    {nullptr              , nullptr                 , nullptr                    , &n::audio_clip_attack   , &n::global_hpf_morph, &n::hpf_mode       , &n::global_hpf_res          , &n::global_hpf_freq}, // x=9
    {&n::sidechain_release, &n::sidechain_sync      , &n::global_sidechain_volume, &n::sidechain_attack    , &n::sidechain_shape , nullptr            , &n::eq_bass                 , &n::eq_bass_freq}, // x=10
    {nullptr              , nullptr                 , nullptr                    , nullptr                 , nullptr             , &n::edit_name      , &n::eq_treble               , &n::eq_treble_freq}, // x=11
    {nullptr              , nullptr                 , nullptr                    , &n::mod_fx_type         , &n::mod_fx_offset   , &n::mod_fx_feedback, &n::global_mod_fx_depth     , &n::global_mod_fx_rate}, // x=12
    {nullptr              , nullptr                 , nullptr                    , &n::global_reverb_amount, &n::reverb_pan      , &n::reverb_width   , &n::reverb_damping          , &n::reverb_room_size}, // x=13
    {&n::global_delay_rate, &n::delay_sync          , &n::delay_type             , &n::global_delay_amount , &n::delay_ping_pong , nullptr            , nullptr                     , nullptr}, // x=14
    {nullptr              , nullptr                 , nullptr                    , nullptr                 , nullptr             , nullptr            , nullptr                     , nullptr}, // x=15
};

/// `paramShortcutsForSongView` (`gui/ui/menus.cpp:2230`).
constexpr const Node* kSongColumns[kShortcutCols][kShortcutRows] = {
    {nullptr              , nullptr       , nullptr       , nullptr                 , nullptr             , nullptr            , nullptr                , nullptr}, // x=0
    {nullptr              , nullptr       , nullptr       , nullptr                 , nullptr             , nullptr            , nullptr                , nullptr}, // x=1
    {nullptr              , nullptr       , nullptr       , nullptr                 , nullptr             , nullptr            , nullptr                , nullptr}, // x=2
    {nullptr              , nullptr       , nullptr       , nullptr                 , nullptr             , nullptr            , nullptr                , nullptr}, // x=3
    {nullptr              , nullptr       , nullptr       , nullptr                 , nullptr             , nullptr            , nullptr                , nullptr}, // x=4
    {nullptr              , nullptr       , nullptr       , nullptr                 , nullptr             , nullptr            , nullptr                , &n::stutter_rate}, // x=5
    {&n::global_level     , nullptr       , nullptr       , &n::global_pan          , nullptr             , &n::decimation     , &n::bitcrush           , nullptr}, // x=6
    {nullptr              , nullptr       , nullptr       , nullptr                 , nullptr             , &n::comp_threshold , nullptr                , nullptr}, // x=7
    {nullptr              , nullptr       , nullptr       , nullptr                 , &n::global_lpf_morph, &n::lpf_mode       , &n::global_lpf_res     , &n::global_lpf_freq}, // x=8
    {nullptr              , nullptr       , nullptr       , nullptr                 , &n::global_hpf_morph, &n::hpf_mode       , &n::global_hpf_res     , &n::global_hpf_freq}, // x=9
    {nullptr              , nullptr       , nullptr       , nullptr                 , nullptr             , nullptr            , &n::eq_bass            , &n::eq_bass_freq}, // x=10
    {nullptr              , nullptr       , nullptr       , nullptr                 , nullptr             , nullptr            , &n::eq_treble          , &n::eq_treble_freq}, // x=11
    {nullptr              , nullptr       , nullptr       , &n::mod_fx_type         , &n::mod_fx_offset   , &n::mod_fx_feedback, &n::global_mod_fx_depth, &n::global_mod_fx_rate}, // x=12
    {nullptr              , nullptr       , nullptr       , &n::global_reverb_amount, &n::reverb_pan      , &n::reverb_width   , &n::reverb_damping     , &n::reverb_room_size}, // x=13
    {&n::global_delay_rate, &n::delay_sync, &n::delay_type, &n::global_delay_amount , &n::delay_ping_pong , nullptr            , nullptr                , nullptr}, // x=14
    {nullptr              , nullptr       , nullptr       , nullptr                 , nullptr             , nullptr            , nullptr                , nullptr}, // x=15
};

/// `paramShortcutsForKitGlobalFX` (`gui/ui/menus.cpp:2249`).
constexpr const Node* kKitGlobalFxColumns[kShortcutCols][kShortcutRows] = {
    {nullptr              , nullptr            , nullptr                    , nullptr                 , nullptr             , nullptr            , nullptr                , nullptr}, // x=0
    {nullptr              , nullptr            , nullptr                    , nullptr                 , nullptr             , nullptr            , nullptr                , nullptr}, // x=1
    {nullptr              , nullptr            , nullptr                    , nullptr                 , nullptr             , nullptr            , nullptr                , nullptr}, // x=2
    {nullptr              , nullptr            , nullptr                    , nullptr                 , nullptr             , nullptr            , nullptr                , nullptr}, // x=3
    {nullptr              , nullptr            , nullptr                    , nullptr                 , nullptr             , nullptr            , nullptr                , nullptr}, // x=4
    {nullptr              , nullptr            , nullptr                    , nullptr                 , nullptr             , nullptr            , nullptr                , nullptr}, // x=5
    {&n::global_level     , &n::global_pitch   , nullptr                    , &n::global_pan          , nullptr             , &n::decimation     , &n::bitcrush           , nullptr}, // x=6
    {nullptr              , nullptr            , nullptr                    , nullptr                 , nullptr             , &n::comp_threshold , nullptr                , nullptr}, // x=7
    {nullptr              , nullptr            , nullptr                    , nullptr                 , &n::global_lpf_morph, &n::lpf_mode       , &n::global_lpf_res     , &n::global_lpf_freq}, // x=8
    {nullptr              , nullptr            , nullptr                    , nullptr                 , &n::global_hpf_morph, &n::hpf_mode       , &n::global_hpf_res     , &n::global_hpf_freq}, // x=9
    {&n::sidechain_release, &n::sidechain_sync , &n::global_sidechain_volume, &n::sidechain_attack    , &n::sidechain_shape , nullptr            , &n::eq_bass            , &n::eq_bass_freq}, // x=10
    {&n::arp_rate         , &n::arp_sync       , &n::arp_gate               , &n::arp_octaves         , &n::arp_preset      , &n::edit_name      , &n::eq_treble          , &n::eq_treble_freq}, // x=11
    {nullptr              , nullptr            , nullptr                    , &n::mod_fx_type         , &n::mod_fx_offset   , &n::mod_fx_feedback, &n::global_mod_fx_depth, &n::global_mod_fx_rate}, // x=12
    {nullptr              , nullptr            , nullptr                    , &n::global_reverb_amount, &n::reverb_pan      , &n::reverb_width   , &n::reverb_damping     , &n::reverb_room_size}, // x=13
    {&n::global_delay_rate, &n::delay_sync     , &n::delay_type             , &n::global_delay_amount , &n::delay_ping_pong , nullptr            , nullptr                , nullptr}, // x=14
    {nullptr              , &n::spread_velocity, &n::randomizer_lock        , &n::note_probability    , nullptr             , nullptr            , nullptr                , nullptr}, // x=15
};

/// `midiOrCVParamShortcuts` (`gui/ui/menus.cpp:1200`) -- one column, `y = 0..7`.
constexpr const Node* kMidiOrCvColumn[kShortcutRows] = {
    &n::arp_rate_midi, &n::arp_sync, &n::arp_gate_midi, &n::arp_octaves, &n::arp_preset, &n::edit_name, nullptr, nullptr,
};

/// `gateDrumParamShortcuts` (`gui/ui/menus.cpp:1211`) -- one column, `y = 0..7`.
constexpr const Node* kGateDrumColumn[kShortcutRows] = {
    &n::arp_rate_midi, &n::arp_sync, &n::arp_gate_midi, &n::arp_rhythm_midi, &n::arp_mode, &n::drum_name_edit, nullptr, nullptr,
};

// clang-format on

constexpr Cells kSoundCells = from_columns(kSoundColumns);
constexpr Cells kSoundSecondCells = from_columns(kSoundSecondColumns);
constexpr Cells kAudioClipCells = from_columns(kAudioClipColumns);
constexpr Cells kSongCells = from_columns(kSongColumns);
constexpr Cells kKitGlobalFxCells = from_columns(kKitGlobalFxColumns);
constexpr Cells kMidiOrCvCells = from_column11(kMidiOrCvColumn);
constexpr Cells kGateDrumCells = from_column11(kGateDrumColumn);

// The two drum variants exist only because of the name-editor swap; if the cell they rewrite ever
// stops being the name editor, the swap is rewriting the wrong pad and this stops the build.
static_assert(kSoundCells[cell_index(11, 5)] == &n::edit_name);
static_assert(kMidiOrCvCells[cell_index(11, 5)] == &n::edit_name);
constexpr Cells kDrumCells = with_drum_name(kSoundCells);
constexpr Cells kMidiDrumCells = with_drum_name(kMidiOrCvCells);

/// @brief The four cells the MIDI/CV and non-audio-drum arm resolves that are **not** in any legacy
///        table -- `potentialShortcutPadAction`'s own `else if` chain (`sound_editor.cpp:1282-1302`).
///
/// Legacy spells them inline rather than in a table: `(4, 7)` opens `sequenceDirectionMenu` and
/// `(15, 1..3)` the MIDI/CV randomizer trio, for `editingCVOrMIDIClip() || editingNonAudioDrumRow()`
/// -- i.e. `Root::MidiOrCv`, `Root::MidiDrum` and `Root::GateDrum`.
///
/// **Resolve-only, and that asymmetry is deliberate.** `updatePadLightsFor`'s MIDI/CV and gate-drum
/// arms (`sound_editor.cpp:833-853`) scan column 11 and *nothing else*, so legacy never blinks these
/// four pads even though pressing them navigates. `shortcut_node` therefore consults this overlay;
/// `shortcut_layer` and `shortcut_pos` do not, so the reverse lookup blinks exactly the pads legacy
/// blinks and no more.
///
/// The other two arms of that same `if` stay out, because neither is a table cell: the DX7 pad
/// grid (`:1170-1177`, which `dxParam` answers for itself) and the patching-source column
/// (`x in {14, 15}`, `:1180-1268`, which walks `patchingSourceShortcutPress` up the menu stack).
constexpr Cells kMidiExtraCells = [] {
	Cells out{};
	out[cell_index(4, 7)] = &n::sequence_direction;     // `sound_editor.cpp:1296-1298`
	out[cell_index(15, 1)] = &n::spread_velocity_midi;  // `:1288-1289`
	out[cell_index(15, 2)] = &n::randomizer_lock;       // `:1290-1291`
	out[cell_index(15, 3)] = &n::note_probability_midi; // `:1292-1293`
	return out;
}();

/// @brief Does a press at (@p x, @p y) reach `paramShortcutsForKitGlobalFX` at all?
///
/// The mirror of `kMidiExtraCells`, and the other half of the same guard asymmetry. The kit
/// global-FX arm is gated `if ((x <= (kDisplayWidth - 2)) || (x == 15 && y == 1))`
/// (`sound_editor.cpp:1153-1157`), so **column 15 is admitted only at y == 1** -- legacy can never
/// press the table's own `(15, 2)` and `(15, 3)` cells (`randomizerLockMenu`,
/// `randomizerNoteProbabilityMenu`), even though it transcribes them. The blink side does not share
/// the guard: `setupShortcutsBlinkFromTable` (`sound_editor.cpp:825`, `:786-795`) scans the whole
/// 16x8 table, so those two cells *do* blink when their item is open.
///
/// So this is press-only, exactly as `kMidiExtraCells` is: there the press side sees pads the blink
/// side does not, here the blink side sees cells the press side does not. Both are read by
/// `shortcut_node` and by nothing else, which is what keeps `shortcut_pos` a faithful port of
/// `updatePadLightsFor`.
///
/// The other four roots' guards were checked against their tables and need none: the song arm
/// (`:1142-1147`) and the audio-clip arm (`:1160-1165`) admit `x <= 14` / `x <= 14` and neither
/// table has a column-15 cell, and the sound arm's guard (`:1271-1273`) admits exactly the cells
/// `paramShortcutsForSounds` fills there.
using PressGateFn = bool (*)(uint8_t x, uint8_t y);

constexpr bool kit_global_fx_press_gate(uint8_t x, uint8_t y) {
	return x <= kShortcutCols - 2 || (x == 15 && y == 1);
}

/// @brief A root's two layers, plus any resolve-only overlay and any press-only gate.
///
/// `second` is `first` for every root without a second legacy table. `resolve_only` is null except
/// on the three roots that carry `kMidiExtraCells`; `press_gate` is null except on
/// `Root::KitGlobalFx`. Both are read by `shortcut_node` alone.
struct Layers {
	const Cells* first;
	const Cells* second;
	const Cells* resolve_only = nullptr;
	PressGateFn press_gate = nullptr;
};

/// @brief Which table each root reads, i.e. legacy's chain of context tests
///        (`sound_editor.cpp:1140-1170` for the jump, `:812-848` for the blink) resolved once.
///
/// * `Sound` / `Drum` -- `paramShortcutsForSounds`, the `else` arm; `Drum` is the kit-drum output
///   whose name editor is the drum one. Both carry the second layer.
/// * `MidiOrCv` / `MidiDrum` -- `midiOrCVParamShortcuts`; `MidiDrum` again swaps the name editor.
/// * `GateDrum` -- `gateDrumParamShortcuts` (`:1277`'s `editingGateDrumRow()`).
/// * `AudioClip` -- `paramShortcutsForAudioClips`.
/// * `Performance` / `Song` -- `paramShortcutsForSongView`, legacy's `!rootUIIsClipMinderScreen()`.
/// * `KitGlobalFx` -- `paramShortcutsForKitGlobalFX` (`setupKitGlobalFXMenu`).
/// * `Settings` -- nothing, per `updatePadLightsFor`'s `!inSettingsMenu()` guard.
///
/// The three MIDI/CV-and-drum roots additionally carry `kMidiExtraCells` as a resolve-only overlay.
constexpr Layers kByRoot[static_cast<size_t>(MenuContext::Root::Count)] = {
    {&kSoundCells, &kSoundSecondCells},                                          // Sound
    {&kDrumCells, &kSoundSecondCells},                                           // Drum
    {&kMidiOrCvCells, &kMidiOrCvCells, &kMidiExtraCells},                        // MidiOrCv
    {&kMidiDrumCells, &kMidiDrumCells, &kMidiExtraCells},                        // MidiDrum
    {&kGateDrumCells, &kGateDrumCells, &kMidiExtraCells},                        // GateDrum
    {&kAudioClipCells, &kAudioClipCells},                                        // AudioClip
    {&kSongCells, &kSongCells},                                                  // Performance
    {&kSongCells, &kSongCells},                                                  // Song
    {&kKitGlobalFxCells, &kKitGlobalFxCells, nullptr, kit_global_fx_press_gate}, // KitGlobalFx
    {&kNoCells, &kNoCells},                                                      // Settings
};

const Layers& layers_for(MenuContext::Root root) {
	const auto i = static_cast<size_t>(root);
	return i < std::size(kByRoot) ? kByRoot[i] : kByRoot[static_cast<size_t>(MenuContext::Root::Settings)];
}

const Node* cell_of(const Cells& cells, uint8_t x, uint8_t y) {
	if (x >= kShortcutCols || y >= kShortcutRows) {
		return nullptr;
	}
	return cells[cell_index(x, y)];
}

/// @brief The first pad holding @p node, scanned **column-major** -- `x` outer, `y` inner.
///
/// The order is `updatePadLightsFor`'s own (`sound_editor.cpp:865-872`), not an implementation
/// detail: it decides which pad blinks for a node that occupies more than one cell, and the
/// audio-clip table has five such nodes (the two sample columns, `x = 0` and `x = 1`, are
/// duplicates). Today every one of them differs only in `x`, so both scan orders would agree --
/// matching legacy's order means they still agree if a future table breaks that.
ShortcutPos find_in(const Cells& cells, const Node& node) {
	for (uint8_t x = 0; x < kShortcutCols; x++) {
		for (uint8_t y = 0; y < kShortcutRows; y++) {
			if (cells[cell_index(x, y)] == &node) {
				return ShortcutPos{x, y, true};
			}
		}
	}
	return ShortcutPos{};
}

} // namespace

std::span<const Node* const> shortcut_layer(MenuContext::Root root, bool second_layer) {
	const Layers& l = layers_for(root);
	return second_layer ? std::span<const Node* const>{*l.second} : std::span<const Node* const>{*l.first};
}

const Node* shortcut_node(MenuContext::Root root, bool second_layer, uint8_t x, uint8_t y) {
	const Layers& l = layers_for(root);
	// First: the arm's own guard on which pads reach the table at all -- a cell the guard excludes
	// is transcribed (and so still blinks) but was never pressable (`kit_global_fx_press_gate`).
	if (l.press_gate != nullptr && !l.press_gate(x, y)) {
		return nullptr;
	}
	if (second_layer) {
		// Legacy's own fallback: the second layer *replaces* the cell only where it has one
		// (`sound_editor.cpp:1320-1323`), so a second press of a pad with no second-layer item
		// re-opens the first-layer item rather than nothing.
		if (const Node* second = cell_of(*l.second, x, y); second != nullptr) {
			return second;
		}
	}
	if (const Node* first = cell_of(*l.first, x, y); first != nullptr) {
		return first;
	}
	// Last: the resolve-only cells legacy spells inline rather than in a table (`kMidiExtraCells`).
	// They are disjoint from the table's own column, so the order only decides what happens if that
	// ever stops being true -- and the table has to win, because it is the transcription.
	return l.resolve_only != nullptr ? cell_of(*l.resolve_only, x, y) : nullptr;
}

ShortcutPos shortcut_pos(MenuContext::Root root, const Node& node) {
	const Layers& l = layers_for(root);
	// Second layer first, the order `updatePadLightsFor` scans in (`sound_editor.cpp:867` before
	// `:872`) -- an item that is in both layers blinks at the pad it is reached from second.
	if (l.second != l.first) {
		if (const ShortcutPos pos = find_in(*l.second, node); pos.valid) {
			return pos;
		}
	}
	return find_in(*l.first, node);
}

std::span<const ShortcutPos> source_shortcut_pads(MenuContext::Root /*root*/, const Node& /*node*/) {
	// Empty in E1 -- see the header's note: which sources blink comes from the live patch matrix,
	// not from a table, and every patch-cable item is a placeholder until E2.
	return {};
}

} // namespace deluge::scenes::menu
