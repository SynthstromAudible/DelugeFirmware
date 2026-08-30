#pragma once

/// @brief The arpeggiator subtree as data: `arp` and its five children, two of which are pages.
///
/// `inline constexpr` for the reason `sound_filters.h` explains (task-6 fix round 1, I3). From
/// `gui/ui/menus.cpp`'s `arpMenu` (`:654-668`), `arpBasicMenu` (`:626-627`), `arpPatternMenu`
/// (`:637-644`) and `arpMpeMenu` (`:652`):
///
// clang-format off
/// | node | legacy global | legacy class | kind |
/// |---|---|---|---|
/// | `arp_mode` | `arpModeMenu` (`:501`) | `arpeggiator::Mode` (`arpeggiator/mode.h:14`) | Enum (toggle) |
/// | `arp_preset` | `arpPresetModeMenu` (`:499`) | `arpeggiator::PresetMode` (`preset_mode.h:18`) | Enum |
/// | `arp_gate` | `arpGateMenu` (`:527`) | `arpeggiator::ArpUnpatchedParam`, `UNPATCHED_ARP_GATE` | Int (param) |
/// | `arp_sync` | `arpSyncMenu` (`:502`) | `arpeggiator::Sync` (`arpeggiator/sync.h:10`) | SyncLevel |
/// | `arp_rate` | `arpRateMenu` (`:503`) | `arpeggiator::Rate` (`arpeggiator/rate.h:7`), `GLOBAL_ARP_RATE` | Int (param) |
/// | `arp_basic` | `arpBasicMenu` (`:626`) | `HorizontalMenu` | Submenu (Horizontal) |
/// | `arp_octaves` | `arpOctavesMenu` (`:508`) | `arpeggiator::Octaves` (`octaves.h:10`) | Int |
/// | `arp_step_repeat` | `arpStepRepeatMenu` (`:524`) | `arpeggiator::StepRepeat` (`step_repeat.h:10`) | Int |
/// | `arp_octave_mode` | `arpOctaveModeMenu` (`:509`) | `arpeggiator::OctaveMode` (`octave_mode.h:13`) | Enum |
/// | `arp_note_mode` | `arpNoteModeMenu` (`:515`) | `arpeggiator::NoteMode` (`note_mode.h:9`) | Enum |
/// | `arp_note_mode_drums` | `arpNoteModeMenuForDrums` (`:516`) | `arpeggiator::NoteModeForDrums` (`note_mode_for_drums.h:12`) | Enum |
/// | `arp_chord_simulator` | `arpChordSimulatorMenuKit` (`:522`) | `arpeggiator::ChordType` (`chord_type.h:14`) | Enum |
/// | `arp_rhythm` | `arpRhythmMenu` (`:530`) | `arpeggiator::Rhythm` (`rhythm.h:12`), `UNPATCHED_ARP_RHYTHM` | Int (param), 2 slots |
/// | `arp_rhythm_midi` | `arpRhythmMenuMIDIOrCV` (`:532`) | `arpeggiator::midi_cv::Rhythm` (`midi_cv/rhythm.h:14`) | Int, 2 slots |
/// | `arp_sequence_length` | `arpSequenceLengthMenu` (`:534`) | `arpeggiator::SequenceLength` (`sequence_length.h:5`) | Int (param) |
/// | `arp_sequence_length_midi` | `arpSequenceLengthMenuMIDIOrCV` (`:536`) | `midi_cv::SequenceLength` (`midi_cv/sequence_length.h:9`) | Int |
/// | `arp_pattern` | `arpPatternMenu` (`:637`) | `HorizontalMenu` | Submenu (Horizontal) |
/// | `arp_mpe_velocity` | `arpMpeVelocityMenu` (`:651`) | `arpeggiator::ArpMpeVelocity` (`mpe_velocity.h:10`) | Enum |
/// | `arp_mpe` | `arpMpeMenu` (`:652`) | `submenu::ArpMpeSubmenu` (`submenu/arp_mpe_submenu.h:5`) | Submenu (Vertical) |
/// | `arp_include_in_kit_arp` | `arpIncludeInKitArpMenu` (`:539`) | `arpeggiator::IncludeInKitArp` (`include_in_kit_arp.h:12`) | Enum (toggle) |
/// | `arp` | `arpMenu` (`:654`) | `Submenu` | Submenu (Vertical) |
// clang-format on
///
/// Everything here reads and writes `ArpeggiatorSettings` -- `MenuContext::arp`, which
/// `context_for` resolves to the drum's, the kit's or the clip's settings exactly as
/// `SoundEditor::setup` did (`context.cpp:176`, `:195`, `:201`, `:225`).
///
/// @note **`isToggle()` is not a `Kind`.** `arpeggiator::Mode` and `IncludeInKitArp` are
///       `Selection`s with two options that legacy draws as a checkbox (`mode.h:83-84`,
///       `include_in_kit_arp.h:71-72`) and refuses to open as a submenu
///       (`shouldEnterSubmenu()`, `:87`/`:75`). `Kind::Enum` keeps the option list -- which is what
///       the spec compares against `Selection::getOptions` -- and the checkbox rendering is a
///       `Hint`-level fact this generation's interpreter does not draw. Same call task 6 made for
///       `sample::PitchSpeed` (its S6).
///
/// @note **Three legacy `selectButtonPress()` jumps live here, and four pages exist only because of
///       them.** `PresetMode` opens the custom octave-mode page when the preset is CUSTOM
///       (`preset_mode.h:109-118`), and `OctaveModeToNoteMode` / `OctaveModeToNoteModeForDrums`
///       chain on to the note mode (`octave_mode.h:90`, `:99`). `arpPatternMenu` names the *plain*
///       `arpOctaveModeMenu` and `arpNoteModeMenu`, not those chaining subclasses, so all four of
///       the subclasses' globals (`menus.cpp:511-521`) are unreachable from any submenu literal --
///       which is why the port declares them below, next to the hooks that name them, rather than
///       in a children span. The complete sweep of every `selectButtonPress` override in
///       `gui/menu_item/` is the design spec's `selectButtonPress()` amendment.
///
/// @note **Bespoke renderers with no `Hint`.** `PresetMode` draws an icon per preset
///       (`preset_mode.h:109-140`) and both rhythm items draw the pattern as characters
///       (`rhythm.h:26-45`, `midi_cv/rhythm.h:39-55`); `SequenceLength` draws "OFF" at zero
///       (`sequence_length.h:11-19`). All four take `Hint::None`, which is the honest answer for
///       art the toolkit has no meter for.

#include "scenes/menu/node.h"
#include "scenes/menu/nodes/detail.h"
#include "scenes/menu/nodes/patch_cables.h" // with_patch_chain -- every PATCHED param chains
#include "scenes/menu/nodes/sound_lfo.h"    // kSyncLevelRange
#include "scenes/menu/relevance.h"

#include "definitions_cxx.hpp" // ArpPreset -- `arp_preset_chain`'s CUSTOM test

#include <span>

namespace deluge::scenes::menu {
struct MenuContext;
} // namespace deluge::scenes::menu

namespace deluge::scenes::menu::nodes {

/// @name The arpeggiator hooks. Bodies (and their citations) in the `.cpp`.
/// @{
int32_t get_arp_mode(const MenuContext& ctx);
void set_arp_mode(const MenuContext& ctx, int32_t value);
int32_t get_arp_preset(const MenuContext& ctx);
void set_arp_preset(const MenuContext& ctx, int32_t value);
int32_t get_arp_sync(const MenuContext& ctx);
void set_arp_sync(const MenuContext& ctx, int32_t value);
int32_t get_arp_octaves(const MenuContext& ctx);
void set_arp_octaves(const MenuContext& ctx, int32_t value);
int32_t get_arp_step_repeat(const MenuContext& ctx);
void set_arp_step_repeat(const MenuContext& ctx, int32_t value);
int32_t get_arp_octave_mode(const MenuContext& ctx);
void set_arp_octave_mode(const MenuContext& ctx, int32_t value);
int32_t get_arp_note_mode(const MenuContext& ctx);
void set_arp_note_mode(const MenuContext& ctx, int32_t value);
int32_t get_arp_note_mode_drums(const MenuContext& ctx);
void set_arp_note_mode_drums(const MenuContext& ctx, int32_t value);
int32_t get_arp_chord_simulator(const MenuContext& ctx);
void set_arp_chord_simulator(const MenuContext& ctx, int32_t value);
int32_t get_arp_rhythm_midi(const MenuContext& ctx);
void set_arp_rhythm_midi(const MenuContext& ctx, int32_t value);
int32_t get_arp_sequence_length_midi(const MenuContext& ctx);
void set_arp_sequence_length_midi(const MenuContext& ctx, int32_t value);
int32_t get_arp_mpe_velocity(const MenuContext& ctx);
void set_arp_mpe_velocity(const MenuContext& ctx, int32_t value);
int32_t get_arp_include_in_kit(const MenuContext& ctx);
void set_arp_include_in_kit(const MenuContext& ctx, int32_t value);
/// @}

/// `arpeggiator::Mode::getOptions` (`arpeggiator/mode.h:74-81`), and `IncludeInKitArp`'s
/// (`include_in_kit_arp.h:56-63`) -- the same two strings.
inline constexpr uint16_t kOffOnNames[] = {
    sid(deluge::l10n::String::STRING_FOR_OFF),
    sid(deluge::l10n::String::STRING_FOR_ON),
};

/// `arpeggiator::PresetMode::getOptions` (`preset_mode.h:80-92`).
inline constexpr uint16_t kArpPresetNames[] = {
    sid(deluge::l10n::String::STRING_FOR_OFF),    sid(deluge::l10n::String::STRING_FOR_UP),
    sid(deluge::l10n::String::STRING_FOR_DOWN),   sid(deluge::l10n::String::STRING_FOR_BOTH),
    sid(deluge::l10n::String::STRING_FOR_RANDOM), sid(deluge::l10n::String::STRING_FOR_WALK),
    sid(deluge::l10n::String::STRING_FOR_CUSTOM),
};

/// `arpeggiator::OctaveMode::getOptions` (`octave_mode.h:49-67`), FULL spelling.
inline constexpr uint16_t kArpOctaveModeNames[] = {
    sid(deluge::l10n::String::STRING_FOR_UP),      sid(deluge::l10n::String::STRING_FOR_DOWN),
    sid(deluge::l10n::String::STRING_FOR_UP_DOWN), sid(deluge::l10n::String::STRING_FOR_ALTERNATE),
    sid(deluge::l10n::String::STRING_FOR_RANDOM),
};

/// `arpeggiator::NoteMode::getOptions` (`note_mode.h:26-52`), FULL spelling.
inline constexpr uint16_t kArpNoteModeNames[] = {
    sid(deluge::l10n::String::STRING_FOR_UP),      sid(deluge::l10n::String::STRING_FOR_DOWN),
    sid(deluge::l10n::String::STRING_FOR_UP_DOWN), sid(deluge::l10n::String::STRING_FOR_RANDOM),
    sid(deluge::l10n::String::STRING_FOR_WALK1),   sid(deluge::l10n::String::STRING_FOR_WALK2),
    sid(deluge::l10n::String::STRING_FOR_WALK3),   sid(deluge::l10n::String::STRING_FOR_AS_PLAYED),
    sid(deluge::l10n::String::STRING_FOR_PATTERN),
};

/// `arpeggiator::NoteModeForDrums::getOptions` (`note_mode_for_drums.h:48-60`) -- the same list
/// without AS PLAYED and PATTERN, which a drum's single note cannot express.
inline constexpr uint16_t kArpNoteModeDrumNames[] = {
    sid(deluge::l10n::String::STRING_FOR_UP),      sid(deluge::l10n::String::STRING_FOR_DOWN),
    sid(deluge::l10n::String::STRING_FOR_UP_DOWN), sid(deluge::l10n::String::STRING_FOR_RANDOM),
    sid(deluge::l10n::String::STRING_FOR_WALK1),   sid(deluge::l10n::String::STRING_FOR_WALK2),
    sid(deluge::l10n::String::STRING_FOR_WALK3),
};

/// `arpeggiator::ChordType::getOptions` (`chord_type.h:51-65`).
inline constexpr uint16_t kArpChordNames[] = {
    sid(deluge::l10n::String::STRING_FOR_NONE),   sid(deluge::l10n::String::STRING_FOR_FIFTH),
    sid(deluge::l10n::String::STRING_FOR_SUS2),   sid(deluge::l10n::String::STRING_FOR_MINOR),
    sid(deluge::l10n::String::STRING_FOR_MAJOR),  sid(deluge::l10n::String::STRING_FOR_SUS4),
    sid(deluge::l10n::String::STRING_FOR_MINOR7), sid(deluge::l10n::String::STRING_FOR_DOMINANT7),
    sid(deluge::l10n::String::STRING_FOR_MAJOR7),
};

/// `arpeggiator::ArpMpeVelocity::getOptions` (`mpe_velocity.h:41-49`).
inline constexpr uint16_t kArpMpeNames[] = {
    sid(deluge::l10n::String::STRING_FOR_DISABLED),
    sid(deluge::l10n::String::STRING_FOR_PATCH_SOURCE_AFTERTOUCH),
    sid(deluge::l10n::String::STRING_FOR_PATCH_SOURCE_Y),
};

// --- the mode toggle -------------------------------------------------------------------------

inline constexpr Node arp_mode =
    with_title(enumeration(sid(deluge::l10n::String::STRING_FOR_ENABLED), kOffOnNames, get_arp_mode, set_arp_mode),
               sid(deluge::l10n::String::STRING_FOR_ARP_MODE_MENU_TITLE));

// --- the four chain-only pages ---------------------------------------------------------------
//
// `arpOctaveModeToNoteModeMenu`, `arpOctaveModeToNoteModeMenuForDrums`,
// `arpNoteModeFromOctaveModeMenu` and `arpNoteModeFromOctaveModeMenuForDrums`
// (`gui/ui/menus.cpp:511-521`) appear in **no** submenu literal: `arpPatternMenu` names the plain
// `arpOctaveModeMenu` / `arpNoteModeMenu` instead, and legacy reaches these four only through
// `selectButtonPress()`. They are declared here, ahead of `arp_preset`, because that is what a
// chain hook needs an address of; `Node::chain_targets` is what puts them in the reachability walk
// so they get node ids like any other page.
//
// Each is behaviourally its base class -- `OctaveModeToNoteMode` and its drum twin derive from
// `OctaveMode` and override nothing but `selectButtonPress` (`arpeggiator/octave_mode.h:85-100`),
// `NoteModeFromOctaveMode` derives from `NoteMode` (`note_mode.h:70-76`) and
// `NoteModeFromOctaveModeForDrums` from `NoteModeForDrums` (`note_mode_for_drums.h:78-84`) -- so
// each shares its base's name, title, options, hooks and relevance exactly.

inline constexpr Node arp_note_mode_from_octave_mode =
    with_title(enumeration(sid(deluge::l10n::String::STRING_FOR_NOTE_MODE), kArpNoteModeNames, get_arp_note_mode,
                           set_arp_note_mode, relevance::not_kit_row),
               sid(deluge::l10n::String::STRING_FOR_ARP_NOTE_MODE_MENU_TITLE));
inline constexpr Node arp_note_mode_from_octave_mode_drums =
    with_title(enumeration(sid(deluge::l10n::String::STRING_FOR_NOTE_MODE), kArpNoteModeDrumNames,
                           get_arp_note_mode_drums, set_arp_note_mode_drums, relevance::kit_row_not_gate),
               sid(deluge::l10n::String::STRING_FOR_ARP_NOTE_MODE_MENU_TITLE));

/// @brief `OctaveModeToNoteMode::selectButtonPress()` (`octave_mode.h:90`) and its drum twin's
///        (`:99`): both chain on unconditionally, differing only in which note-mode page they name.
/// @{
inline constexpr const Node* kArpOctaveModeToNoteModeChain[] = {&arp_note_mode_from_octave_mode};
inline constexpr const Node* kArpOctaveModeToNoteModeDrumsChain[] = {&arp_note_mode_from_octave_mode_drums};
inline const Node* arp_octave_mode_to_note_mode_chain(const MenuContext&, const MenuState&) {
	return &arp_note_mode_from_octave_mode;
}
inline const Node* arp_octave_mode_to_note_mode_drums_chain(const MenuContext&, const MenuState&) {
	return &arp_note_mode_from_octave_mode_drums;
}
/// @}

inline constexpr Node arp_octave_mode_to_note_mode = with_select_chain(
    with_title(enumeration(sid(deluge::l10n::String::STRING_FOR_OCTAVE_MODE), kArpOctaveModeNames, get_arp_octave_mode,
                           set_arp_octave_mode, relevance::not_gate_drum_or_kit_fx),
               sid(deluge::l10n::String::STRING_FOR_ARP_OCTAVE_MODE_MENU_TITLE)),
    arp_octave_mode_to_note_mode_chain, kArpOctaveModeToNoteModeChain);
inline constexpr Node arp_octave_mode_to_note_mode_drums = with_select_chain(
    with_title(enumeration(sid(deluge::l10n::String::STRING_FOR_OCTAVE_MODE), kArpOctaveModeNames, get_arp_octave_mode,
                           set_arp_octave_mode, relevance::not_gate_drum_or_kit_fx),
               sid(deluge::l10n::String::STRING_FOR_ARP_OCTAVE_MODE_MENU_TITLE)),
    arp_octave_mode_to_note_mode_drums_chain, kArpOctaveModeToNoteModeDrumsChain);

// --- the basic page --------------------------------------------------------------------------

/// @brief `PresetMode::selectButtonPress()` (`preset_mode.h:109-118`): only the CUSTOM preset opens
///        anything, and which page it opens depends on whether a kit row is being edited
///        (`soundEditor.editingKitRow()`, which `relevance::is_kit_row` ports). Every other preset
///        returns null, i.e. ascends.
inline constexpr const Node* kArpPresetChain[] = {&arp_octave_mode_to_note_mode, &arp_octave_mode_to_note_mode_drums};
inline const Node* arp_preset_chain(const MenuContext& ctx, const MenuState&) {
	if (get_arp_preset(ctx) != static_cast<int32_t>(ArpPreset::CUSTOM)) {
		return nullptr;
	}
	return relevance::is_kit_row(ctx) ? &arp_octave_mode_to_note_mode_drums : &arp_octave_mode_to_note_mode;
}

inline constexpr Node arp_preset =
    with_select_chain(with_title(enumeration(sid(deluge::l10n::String::STRING_FOR_PRESET), kArpPresetNames,
                                             get_arp_preset, set_arp_preset),
                                 sid(deluge::l10n::String::STRING_FOR_ARP_PRESET_MENU_TITLE)),
                      arp_preset_chain, kArpPresetChain);
inline constexpr Node arp_gate =
    param(sid(deluge::l10n::String::STRING_FOR_GATE),
          {deluge::modulation::params::Kind::UNPATCHED_SOUND, deluge::modulation::params::UNPATCHED_ARP_GATE},
          Hint::LengthSlider, relevance::arp_sound_only, sid(deluge::l10n::String::STRING_FOR_ARP_GATE_MENU_TITLE));
inline constexpr Node arp_sync = sync_level_item(sid(deluge::l10n::String::STRING_FOR_SYNC),
                                                 sid(deluge::l10n::String::STRING_FOR_ARP_SYNC_MENU_TITLE),
                                                 get_arp_sync, set_arp_sync, kSyncLevelRange);
inline constexpr Node arp_rate = with_patch_chain(
    param(sid(deluge::l10n::String::STRING_FOR_RATE),
          {deluge::modulation::params::Kind::PATCHED, deluge::modulation::params::GLOBAL_ARP_RATE}, Hint::Knob,
          relevance::arp_rate_sound, sid(deluge::l10n::String::STRING_FOR_ARP_RATE_MENU_TITLE)));

inline constexpr const Node* kArpBasicChildren[] = {&arp_preset, &arp_gate, &arp_sync, &arp_rate};
inline constexpr Node arp_basic =
    submenu(sid(deluge::l10n::String::STRING_FOR_BASIC), kArpBasicChildren, Layout::Horizontal, nullptr,
            sid(deluge::l10n::String::STRING_FOR_ARP_BASIC_MENU_TITLE));

// --- the pattern page ------------------------------------------------------------------------

// `Octaves::getMinValue`/`getMaxValue` (`octaves.h:37-38`) and `StepRepeat`'s (`step_repeat.h:36-37`);
// both render a plain NUMBER (`octaves.h:44`, `step_repeat.h:45`), hence `Hint::None`.
inline constexpr Node arp_octaves =
    with_title(int_item(sid(deluge::l10n::String::STRING_FOR_NUMBER_OF_OCTAVES), get_arp_octaves, set_arp_octaves,
                        IntRange{1, 8, 1}, Hint::None, relevance::not_gate_drum_or_kit_fx),
               sid(deluge::l10n::String::STRING_FOR_ARP_OCTAVES_MENU_TITLE));
inline constexpr Node arp_step_repeat =
    with_title(int_item(sid(deluge::l10n::String::STRING_FOR_STEP_REPEAT), get_arp_step_repeat, set_arp_step_repeat,
                        IntRange{1, 8, 1}, Hint::None, relevance::not_gate_drum_row),
               sid(deluge::l10n::String::STRING_FOR_ARP_STEP_REPEAT_MENU_TITLE));
inline constexpr Node arp_octave_mode =
    with_title(enumeration(sid(deluge::l10n::String::STRING_FOR_OCTAVE_MODE), kArpOctaveModeNames, get_arp_octave_mode,
                           set_arp_octave_mode, relevance::not_gate_drum_or_kit_fx),
               sid(deluge::l10n::String::STRING_FOR_ARP_OCTAVE_MODE_MENU_TITLE));
inline constexpr Node arp_note_mode =
    with_title(enumeration(sid(deluge::l10n::String::STRING_FOR_NOTE_MODE), kArpNoteModeNames, get_arp_note_mode,
                           set_arp_note_mode, relevance::not_kit_row),
               sid(deluge::l10n::String::STRING_FOR_ARP_NOTE_MODE_MENU_TITLE));
inline constexpr Node arp_note_mode_drums =
    with_title(enumeration(sid(deluge::l10n::String::STRING_FOR_NOTE_MODE), kArpNoteModeDrumNames,
                           get_arp_note_mode_drums, set_arp_note_mode_drums, relevance::kit_row_not_gate),
               sid(deluge::l10n::String::STRING_FOR_ARP_NOTE_MODE_MENU_TITLE));
inline constexpr Node arp_chord_simulator =
    with_title(enumeration(sid(deluge::l10n::String::STRING_FOR_CHORD_SIMULATOR), kArpChordNames,
                           get_arp_chord_simulator, set_arp_chord_simulator, relevance::kit_row_not_gate),
               sid(deluge::l10n::String::STRING_FOR_ARP_CHORD_SIMULATOR_MENU_TITLE));
// `Rhythm::getOccupiedSlots()` is 2 (`rhythm.h:51-52`), as is its MIDI/CV twin's
// (`midi_cv/rhythm.h:56-57`); the range is `{0, kMaxPresetArpRhythm}` (`rhythm.h:16-17`), and
// `kMaxPresetArpRhythm == kMaxMenuValue` (`modulation/arpeggiator_rhythms.h:28`), which is the
// param default -- so no `with_range` is needed on either.
inline constexpr Node arp_rhythm = with_slots(
    param(sid(deluge::l10n::String::STRING_FOR_RHYTHM),
          {deluge::modulation::params::Kind::UNPATCHED_SOUND, deluge::modulation::params::UNPATCHED_ARP_RHYTHM},
          Hint::None, relevance::arp_sound_only, sid(deluge::l10n::String::STRING_FOR_ARP_RHYTHM_MENU_TITLE)),
    2);
inline constexpr Node arp_rhythm_midi = with_slots(
    with_title(int_item(sid(deluge::l10n::String::STRING_FOR_RHYTHM), get_arp_rhythm_midi, set_arp_rhythm_midi,
                        IntRange{kMinMenuValue, kMaxMenuValue, 1}, Hint::None, relevance::arp_non_sound),
               sid(deluge::l10n::String::STRING_FOR_ARP_RHYTHM_MENU_TITLE)),
    2);
inline constexpr Node arp_sequence_length = param(
    sid(deluge::l10n::String::STRING_FOR_SEQUENCE_LENGTH),
    {deluge::modulation::params::Kind::UNPATCHED_SOUND, deluge::modulation::params::UNPATCHED_ARP_SEQUENCE_LENGTH},
    Hint::None, relevance::arp_sound_only, sid(deluge::l10n::String::STRING_FOR_ARP_SEQUENCE_LENGTH_MENU_TITLE));
inline constexpr Node arp_sequence_length_midi =
    with_title(int_item(sid(deluge::l10n::String::STRING_FOR_SEQUENCE_LENGTH), get_arp_sequence_length_midi,
                        set_arp_sequence_length_midi, IntRange{kMinMenuValue, kMaxMenuValue, 1}, Hint::None,
                        relevance::arp_non_sound),
               sid(deluge::l10n::String::STRING_FOR_ARP_SEQUENCE_LENGTH_MENU_TITLE));

inline constexpr const Node* kArpPatternChildren[] = {
    &arp_octaves,         &arp_step_repeat, &arp_octave_mode, &arp_note_mode,       &arp_note_mode_drums,
    &arp_chord_simulator, &arp_rhythm,      &arp_rhythm_midi, &arp_sequence_length, &arp_sequence_length_midi};
inline constexpr Node arp_pattern =
    submenu(sid(deluge::l10n::String::STRING_FOR_PATTERN), kArpPatternChildren, Layout::Horizontal, nullptr,
            sid(deluge::l10n::String::STRING_FOR_ARP_PATTERN_MENU_TITLE));

// --- the MPE page ----------------------------------------------------------------------------

inline constexpr Node arp_mpe_velocity =
    enumeration(sid(deluge::l10n::String::STRING_FOR_VELOCITY), kArpMpeNames, get_arp_mpe_velocity,
                set_arp_mpe_velocity, relevance::not_gate_drum_row);
inline constexpr const Node* kArpMpeChildren[] = {&arp_mpe_velocity};
inline constexpr Node arp_mpe = submenu(sid(deluge::l10n::String::STRING_FOR_MPE), kArpMpeChildren, Layout::Vertical,
                                        relevance::not_gate_drum_or_kit_fx);

// --- the page itself -------------------------------------------------------------------------

inline constexpr Node arp_include_in_kit_arp =
    enumeration(sid(deluge::l10n::String::STRING_FOR_INCLUDE_IN_KIT_ARP), kOffOnNames, get_arp_include_in_kit,
                set_arp_include_in_kit, relevance::is_kit_row);

inline constexpr const Node* kArpChildren[] = {&arp_mode, &arp_basic, &arp_pattern, &arp_mpe, &arp_include_in_kit_arp};
inline constexpr Node arp = submenu(sid(deluge::l10n::String::STRING_FOR_ARPEGGIATOR), kArpChildren);

/// @brief This file's contribution to the sound-editor root: `arpMenu`.
///
/// Defined in the matching `.cpp`, which is also where the `get`/`set` hook bodies live.
extern const std::span<const Node* const> kSoundArpChildren;

} // namespace deluge::scenes::menu::nodes
