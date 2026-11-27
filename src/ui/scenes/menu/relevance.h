#pragma once

/// @brief The relevance predicates the menu tables use -- one per legacy `MenuItem::isRelevant`
///        override the six generated subtrees reach.
///
/// Legacy asked `item->isRelevant(soundEditor.currentModControllable, soundEditor.currentSourceIndex)`
/// (`Submenu::drawPixelsForOled` in `origin/main:src/deluge/gui/menu_item/submenu.cpp`); a `Node` carries a
/// `RelevantFn` instead, called with the `MenuContext` the scene resolved. Every predicate below names the legacy
/// override it ports and the line it lives on, and is a transcription -- `gui/menu_item/` is behind the
/// `scenes-includes-outside` wall.
///
/// @note **Why a source index is baked into a predicate's *name* rather than read from the context.** A legacy
///       oscillator item holds its own `source_id_` member and ignores the `whichThing` argument it is handed
///       (`PulseWidth::isRelevant` in `origin/main:src/deluge/gui/menu_item/osc/pulse_width.h`,
///       `origin/main:src/deluge/gui/menu_item/sample/transpose.h`, ...), so `osc0PulseWidthMenu` asks about source 0
///       wherever it is shown. `RelevantFn` takes only a `const MenuContext&` and a `Node` has no per-item payload, so
///       the two spellings of each per-source predicate (`osc0_*` / `osc1_*`) are how that member survives. Reading
///       `MenuContext::source_index` instead would be wrong: nothing re-resolves the context on the way into
///       `source1Menu` (in main, items such as `FileSelector::beginSession`
///       (`origin/main:src/deluge/gui/menu_item/file_selector.cpp`) call `SoundEditor::setCurrentSource` themselves),
///       so the field would still say 0.
///
/// @note **`name_of`** exists for the menu dump, which has to print *which*
///       predicate guards a node, and for the specs, which assert the registry covers every
///       predicate this header declares.

#include "ui/scenes/menu/node.h" // RelevantFn

#include <span>
#include <string_view>

namespace deluge::ui::scenes::menu {
struct MenuContext;
} // namespace deluge::ui::scenes::menu

namespace deluge::ui::scenes::menu::relevance {

/// @name Sound-wide predicates
/// @{

/// @brief Is the sound in FM mode? `modulator::Transpose::isRelevant`
///        (`origin/main:src/deluge/gui/menu_item/modulator/transpose.h`), `source::patched_param::ModulatorFeedback`
///        (`origin/main:src/deluge/gui/menu_item/source/patched_param/modulator_feedback.h`), `ModulatorLevel`
///        (`origin/main:src/deluge/gui/menu_item/source/patched_param/modulator_level.h`), `submenu::Modulator`
///        (`origin/main:src/deluge/gui/menu_item/submenu/modulator.h`) and `modulator::Destination`
///        (`origin/main:src/deluge/gui/menu_item/modulator/destination.h`) -- the last two spell it `sound->synthMode`,
///        the first three `sound->getSynthMode()`, which is the same member (`Sound::getSynthMode`).
bool is_fm(const MenuContext& ctx);
/// @brief Is the sound in subtractive mode? `osc::AudioRecorder::isRelevant`
///        (`origin/main:src/deluge/gui/menu_item/osc/audio_recorder.h`).
bool is_subtractive(const MenuContext& ctx);
/// @brief Whether the edited sound plays the DX7 engine: its DX7 page shows, its oscillators' pages do not.
bool is_dx7(const MenuContext& ctx);
/// @brief Whether the edited sound plays an engine other than the DX7: its oscillators' pages show.
bool not_dx7(const MenuContext& ctx);
/// @}

/// @name Per-source predicates -- `osc0_*` is legacy's `source_id_ == 0`, `osc1_*` its `== 1`
/// @{

/// @brief `FileSelector::isRelevant` (`origin/main:src/deluge/gui/menu_item/file_selector.cpp`): always on an audio
///        clip; on a sound, wavetable needs only "not FM", sample needs subtractive.
/// @{
bool osc0_file_selector(const MenuContext& ctx);
bool osc1_file_selector(const MenuContext& ctx);
/// @}

/// @brief `sample::Transpose::isRelevant` (`origin/main:src/deluge/gui/menu_item/sample/transpose.h`): a sample or
///        wavetable source needs a loaded audio file; anything else is always relevant.
/// @{
bool osc0_transpose(const MenuContext& ctx);
bool osc1_transpose(const MenuContext& ctx);
/// @}

/// @}

/// @}

/// @}

/// @brief `isSampleModeSample` (`origin/main:src/deluge/gui/menu_item/sample/utils.cpp`) -- the predicate
///        `sample::Reverse` (`origin/main:src/deluge/gui/menu_item/sample/reverse.h`), `Repeat`
///        (`origin/main:src/deluge/gui/menu_item/sample/repeat.h`), `TimeStretch`
///        (`origin/main:src/deluge/gui/menu_item/sample/time_stretch.h`) and `LoopPoint`
///        (`origin/main:src/deluge/gui/menu_item/sample/loop_point.cpp`, i.e. `Start`/`End`) all delegate to.
/// @{
bool osc0_is_sample(const MenuContext& ctx);
bool osc1_is_sample(const MenuContext& ctx);
/// @}

/// @}

/// @}

/// @name Clip and output predicates -- the rest of the sound-editor root
///
/// Where the sound-wide predicates above all ask about the *sound* (its synth mode, its sources), most of
/// these ask about the *clip*: is this a kit row, a kit's global FX page, a MIDI or CV clip, a
/// gate drum. `MenuContext` carries every one of those facts (`is_kit`, `is_kit_global_fx`,
/// `output`, `arp`), so the `SoundEditor::editingKit()` / `editingKitRow()` /
/// `editingCVOrMIDIClip()` / `editingNonAudioDrumRow()` / `editingGateDrumRow()` family
/// (`origin/main:src/deluge/gui/ui/sound_editor.cpp`) is transcribed against it rather than against globals.
/// @{

/// @brief Is a kit *row* being edited? `IncludeInKitArp::isRelevant`
///        (`origin/main:src/deluge/gui/menu_item/arpeggiator/include_in_kit_arp.h`), i.e. `SoundEditor::editingKitRow`.
bool is_kit_row(const MenuContext& ctx);
/// @brief Neither a gate drum row nor a kit's global-FX page. `arpeggiator::Octaves::isRelevant`
///        (`origin/main:src/deluge/gui/menu_item/arpeggiator/octaves.h`), `OctaveMode`
///        (`origin/main:src/deluge/gui/menu_item/arpeggiator/octave_mode.h`) and `submenu::ArpMpeSubmenu`
///        (`origin/main:src/deluge/gui/menu_item/submenu/arp_mpe_submenu.h`) -- one predicate, three items.
bool not_gate_drum_or_kit_fx(const MenuContext& ctx);
/// @brief Neither a MIDI or gate drum row nor a kit's own effects: `arpeggiator::Rate::isRelevant`
///        (`origin/main:src/deluge/gui/menu_item/arpeggiator/rate.h`),
///        `!editingNonAudioDrumRow() && !editingKitAffectEntire()`. Its third term, `!editingCVOrMIDIClip()`, is the
///        param target's own: a MIDI or CV clip has no sound to hold the rate.
bool not_non_audio_drum_or_kit_fx(const MenuContext& ctx);
/// @brief Not a MIDI or gate drum row: `arpeggiator::ArpUnpatchedParam::isRelevant`
///        (`origin/main:src/deluge/gui/menu_item/arpeggiator/arp_unpatched_param.h`) and the first test of
///        `randomizer::RandomizerUnpatchedParam::isRelevant`
///        (`origin/main:src/deluge/gui/menu_item/randomizer/randomizer_unpatched_param.h`),
///        `!editingNonAudioDrumRow()`. Their `!editingCVOrMIDIClip()` is the param target's own.
bool not_non_audio_drum_row(const MenuContext& ctx);
/// @brief Not a gate drum row: `arpeggiator::StepRepeat::isRelevant`
///        (`origin/main:src/deluge/gui/menu_item/arpeggiator/step_repeat.h`), `!editingGateDrumRow()`, the kit's
///        selected drum whatever the menu edits.
bool not_gate_drum_row(const MenuContext& ctx);

/// @brief Is reverb ducking set to something other than AUTO? `reverb::sidechain::Shape::isRelevant`
///        (`origin/main:src/deluge/gui/menu_item/reverb/sidechain/shape.h`), `sidechain::Attack` / `Release` / `Sync`
///        in their reverb spelling (`Attack::isRelevant` in `origin/main:src/deluge/gui/menu_item/sidechain/attack.h`,
///        `Release::isRelevant` in `origin/main:src/deluge/gui/menu_item/sidechain/release.h`,
///        `origin/main:src/deluge/gui/menu_item/sidechain/sync.h` `Sync::isRelevant`).
bool reverb_sidechain_on(const MenuContext& ctx);

/// @brief May stutter quantization be edited? `stutter::QuantizedStutter::isRelevant`
///        (`origin/main:src/deluge/gui/menu_item/stutter/quantized.h`).
bool stutter_quantize_allowed(const MenuContext& ctx);

/// @brief Does this output get the two-item bend-range submenu? `submenu::Bend::isRelevant`
///        (`origin/main:src/deluge/gui/menu_item/submenu/bend.h`): SYNTH, CV or MIDI_OUT.
bool bend_submenu(const MenuContext& ctx);

/// @}

/// @name Settings-root and other-root predicates
/// @{

/// @brief Is the output a MIDI instrument? The override shared by `midi::Preset`
///        (`origin/main:src/deluge/gui/menu_item/midi/preset.h`, i.e. the bank / sub-bank / PGM items),
///        `midi::MPEYToModWheel` (`origin/main:src/deluge/gui/menu_item/midi/y_axis_to_cc1.h`),
///        `submenu::PolyMonoConversion` (`origin/main:src/deluge/gui/menu_item/submenu/MPE.h`),
///        `device_definition::DeviceDefinitionSubmenu`
///        (`origin/main:src/deluge/gui/menu_item/midi/device_definition/submenu.h`), `Linked`
///        (`origin/main:src/deluge/gui/menu_item/midi/device_definition/linked.h`) and `HideUnlabeled`
///        (`origin/main:src/deluge/gui/menu_item/midi/device_definition/hide_unlabeled.h`) -- six items, one test.
bool is_midi_out(const MenuContext& ctx);

/// @brief Is the battery readout switched on? `battery::Level::isRelevant`
///        (`origin/main:src/deluge/gui/menu_item/battery/level.h`): the device's `features/showBatteryLevel` row,
///        read through @p ctx's device port; the row's default, on, where it binds none.
bool battery_shown(const MenuContext& ctx);

/// @brief Is the audio clip's output recording from one specific track?
///        `audio_clip::SpecificSourceOutputSelector::isRelevant`
///        (`origin/main:src/deluge/gui/menu_item/audio_clip/specific_output_source_selector.h`):
///        `AudioOutput::inputChannel == AudioInputChannel::SPECIFIC_OUTPUT`.
bool audio_source_is_specific(const MenuContext& ctx);
/// @brief Has the audio clip a sample loaded? `audio_clip::SampleMarkerEditor::isRelevant`
///        (`origin/main:src/deluge/gui/menu_item/audio_clip/sample_marker_editor.cpp`):
///        `sampleHolder.audioFile != nullptr`, so the waveform item, and its shortcut, show only once there is a sample
///        to edit.
bool audio_clip_has_sample(const MenuContext& ctx);
/// @}

/// @name The song root's two session-only items
/// @{

/// @brief Is the root UI the session view? `song::MidiLearn::isRelevant`
///        (`origin/main:src/deluge/gui/menu_item/song/midi_learn.h`) and `song::ConfigureMacros::isRelevant`
///        (`origin/main:src/deluge/gui/menu_item/song/configure_macros.h`) -- both are `getRootUI() == &sessionView`,
///        verbatim, and nothing else in the tree spells it.
///
/// This is the one predicate that reads a *UI* fact rather than a model one, and it is only able to
/// because `MenuContext::is_session_view` carries it: `getRootUI()` itself stays out
/// of `src/ui/scenes`, so the caller reduces the read to `context_for`'s `session_view_root` argument
/// exactly as it does the other three. The wider `is_song_view` would be the wrong field -- it is
/// true in the arranger and the song automation view too, where legacy hides both items.
bool is_session_view(const MenuContext& ctx);
/// @}

/// @brief One `{predicate, name}` row of the registry `name_of` searches.
struct NamedPredicate {
	RelevantFn fn;
	std::string_view name;
};

/// @brief Every predicate this header declares, in declaration order.
///
/// Exposed (rather than hidden behind `name_of`) so a spec can assert the registry is complete and
/// so the menu dump can enumerate predicates as well as resolve them.
std::span<const NamedPredicate> registry();

/// @brief The registered name of @p fn, or `"?"` for null / an unregistered predicate.
///
/// A node with no `relevant` hook is always relevant; `name_of(nullptr)` answers `"always"` so a
/// dump can print something meaningful for it.
std::string_view name_of(RelevantFn fn);

} // namespace deluge::ui::scenes::menu::relevance
