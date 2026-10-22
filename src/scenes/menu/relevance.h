#pragma once

/// @brief The relevance predicates the batch-1 tables use -- one per legacy `MenuItem::isRelevant`
///        override the six generated subtrees reach (spec E §4's "the 84 relevance predicates").
///
/// Legacy asked `item->isRelevant(soundEditor.currentModControllable, soundEditor.currentSourceIndex)`
/// (`gui/menu_item/submenu.cpp:63`); a `Node` carries a `RelevantFn` instead, called with the
/// `MenuContext` the scene resolved. Every predicate below names the legacy override it ports and
/// the line it lives on, and is a transcription -- `gui/menu_item/` is behind the
/// `scenes-includes-outside` wall.
///
/// @note **Why a source index is baked into a predicate's *name* rather than read from the
///       context.** A legacy oscillator item holds its own `source_id_` member and ignores the
///       `whichThing` argument it is handed (`osc/pulse_width.h:39-52`, `sample/transpose.h:139`,
///       ...), so `osc0PulseWidthMenu` asks about source 0 wherever it is shown. `RelevantFn` takes
///       only a `const MenuContext&` and a `Node` has no per-item payload, so the two spellings of
///       each per-source predicate (`osc0_*` / `osc1_*`) are how that member survives. Reading
///       `MenuContext::source_index` instead would be wrong today: nothing re-resolves the context
///       on the way into `source1Menu` (legacy did it in `submenu::ActualSource::beginSession` ->
///       `SoundEditor::setCurrentSource`, `sound_editor.cpp:242`), so the field would still say 0.
///
/// @note **`name_of`** exists for Task 10's `deluge_menu_dump`, which has to print *which*
///       predicate guards a node, and for the specs, which assert the registry covers every
///       predicate this header declares.

#include "scenes/menu/node.h" // RelevantFn

#include <span>
#include <string_view>

namespace deluge::scenes::menu {
struct MenuContext;
} // namespace deluge::scenes::menu

namespace deluge::scenes::menu::relevance {

/// @name Sound-wide predicates
/// @{

/// @brief Is the sound in FM mode? `modulator::Transpose::isRelevant` (`modulator/transpose.h:76-79`),
///        `source::patched_param::ModulatorFeedback` (`modulator_feedback.h:31-34`),
///        `ModulatorLevel` (`modulator_level.h:31-34`), `submenu::Modulator` (`submenu/modulator.h:25-28`)
///        and `modulator::Destination` (`modulator/destination.h:65-68`) -- the last two spell it
///        `sound->synthMode`, the first three `sound->getSynthMode()`, which is the same member
///        (`processing/sound/sound.h:229`).
bool is_fm(const MenuContext& ctx);
/// @brief Is the sound in subtractive mode? `osc::AudioRecorder::isRelevant`
///        (`osc/audio_recorder.h:60-63`).
bool is_subtractive(const MenuContext& ctx);
/// @}

/// @name Per-source predicates -- `osc0_*` is legacy's `source_id_ == 0`, `osc1_*` its `== 1`
/// @{

/// @brief `FileSelector::isRelevant` (`gui/menu_item/file_selector.cpp:54-67`): always on an audio
///        clip; on a sound, wavetable needs only "not FM", sample needs subtractive.
/// @{
bool osc0_file_selector(const MenuContext& ctx);
bool osc1_file_selector(const MenuContext& ctx);
/// @}

/// @brief `sample::Transpose::isRelevant` (`sample/transpose.h:139-145`): a sample or wavetable
///        source needs a loaded audio file; anything else is always relevant.
/// @{
bool osc0_transpose(const MenuContext& ctx);
bool osc1_transpose(const MenuContext& ctx);
/// @}

/// @}

/// @}

/// @}

/// @brief `isSampleModeSample` (`gui/menu_item/sample/utils.cpp:9-15`) -- the predicate
///        `sample::Reverse` (`sample/reverse.h:34-36`), `Repeat` (`sample/repeat.h:39-41`),
///        `TimeStretch` (`sample/time_stretch.h:36-38`) and `LoopPoint`
///        (`sample/loop_point.cpp:34-36`, i.e. `Start`/`End`) all delegate to.
/// @{
bool osc0_is_sample(const MenuContext& ctx);
bool osc1_is_sample(const MenuContext& ctx);
/// @}

/// @}

/// @}

/// @name Batch-2 predicates -- the rest of the sound-editor root (Task 7)
///
/// Where batch 1's predicates all ask about the *sound* (its synth mode, its sources), most of
/// these ask about the *clip*: is this a kit row, a kit's global FX page, a MIDI or CV clip, a
/// gate drum. `MenuContext` carries every one of those facts (`is_kit`, `is_kit_global_fx`,
/// `output`, `arp`), so the `SoundEditor::editingKit()` / `editingKitRow()` /
/// `editingCVOrMIDIClip()` / `editingNonAudioDrumRow()` / `editingGateDrumRow()` family
/// (`gui/ui/sound_editor.cpp:199-240`) is transcribed against it rather than against globals.
/// @{

/// @brief Is a kit *row* being edited? `IncludeInKitArp::isRelevant` (`include_in_kit_arp.h:81-84`),
///        i.e. `editingKitRow()` (`sound_editor.cpp:207-209`).
bool is_kit_row(const MenuContext& ctx);
/// @brief Neither a gate drum row nor a kit's global-FX page. `arpeggiator::Octaves::isRelevant`
///        (`octaves.h:39-41`), `OctaveMode` (`octave_mode.h:44-46`) and `submenu::ArpMpeSubmenu`
///        (`submenu/arp_mpe_submenu.h:22-24`) -- one predicate, three items.
bool not_gate_drum_or_kit_fx(const MenuContext& ctx);

/// @brief Is reverb ducking set to something other than AUTO? `reverb::sidechain::Shape::isRelevant`
///        (`reverb/sidechain/shape.h:37-39`), `sidechain::Attack` / `Release` / `Sync` in their
///        reverb spelling (`sidechain/attack.h:57-59`, `release.h:57-59`, `sync.h:56-58`).
bool reverb_sidechain_on(const MenuContext& ctx);

/// @brief May stutter quantization be edited? `stutter::QuantizedStutter::isRelevant`
///        (`stutter/quantized.h:44-47`).
bool stutter_quantize_allowed(const MenuContext& ctx);

/// @brief Does this output get the two-item bend-range submenu? `submenu::Bend::isRelevant`
///        (`submenu/bend.h:26-31`): SYNTH, CV or MIDI_OUT.
bool bend_submenu(const MenuContext& ctx);

/// @}

/// @name Batch-3 predicates -- the settings root and the nine other roots (Task 8)
/// @{

/// @brief Is the output a MIDI instrument? The override shared by `midi::Preset`
///        (`gui/menu_item/midi/preset.h:31-33`, i.e. the bank / sub-bank / PGM items),
///        `midi::MPEYToModWheel` (`midi/y_axis_to_cc1.h:29-33`), `submenu::PolyMonoConversion`
///        (`submenu/MPE.h:22-26`), `device_definition::DeviceDefinitionSubmenu`
///        (`midi/device_definition/submenu.h:26-34`), `Linked` (`linked.h:44-47`) and
///        `HideUnlabeled` (`hide_unlabeled.h:33-36`) -- six items, one test.
bool is_midi_out(const MenuContext& ctx);

/// @brief Is the battery readout switched on? `battery::Level::isRelevant`
///        (`gui/menu_item/battery/level.h:33-35`), i.e.
///        `runtimeFeatureSettings.isOn(RuntimeFeatureSettingType::ShowBatteryLevel)`.
bool battery_shown(const MenuContext& ctx);

/// @brief Is the audio clip's output recording from one specific track?
///        `audio_clip::SpecificSourceOutputSelector::isRelevant`
///        (`gui/menu_item/audio_clip/specific_output_source_selector.h:102-106`):
///        `AudioOutput::inputChannel == AudioInputChannel::SPECIFIC_OUTPUT`.
bool audio_source_is_specific(const MenuContext& ctx);
/// @brief Has the audio clip a sample loaded? `audio_clip::SampleMarkerEditor::isRelevant`
///        (`gui/menu_item/audio_clip/sample_marker_editor.cpp`): `sampleHolder.audioFile != nullptr`, so the
///        waveform item, and its shortcut, show only once there is a sample to edit.
bool audio_clip_has_sample(const MenuContext& ctx);
/// @}

/// @name Task 9's predicate -- the song root's two session-only items
/// @{

/// @brief Is the root UI the session view? `song::MidiLearn::isRelevant`
///        (`gui/menu_item/song/midi_learn.h:35-37`) and `song::ConfigureMacros::isRelevant`
///        (`gui/menu_item/song/configure_macros.h:35-37`) -- both are `getRootUI() == &sessionView`,
///        verbatim, and nothing else in the tree spells it.
///
/// This is the one predicate that reads a *UI* fact rather than a model one, and it is only able to
/// because `MenuContext::is_session_view` carries it: task-3 ruling 2 keeps `getRootUI()` itself out
/// of `src/scenes`, so the caller reduces the read to `context_for`'s `session_view_root` argument
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
/// so Task 10's dump can enumerate predicates as well as resolve them.
std::span<const NamedPredicate> registry();

/// @brief The registered name of @p fn, or `"?"` for null / an unregistered predicate.
///
/// A node with no `relevant` hook is always relevant; `name_of(nullptr)` answers `"always"` so a
/// dump can print something meaningful for it.
std::string_view name_of(RelevantFn fn);

} // namespace deluge::scenes::menu::relevance
