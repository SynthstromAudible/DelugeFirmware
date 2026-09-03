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
///       `SoundEditor::setCurrentSource`, `sound_editor.cpp:204`), so the field would still say 0.
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

/// @brief Is the low-pass filter on? `filter::FilterParam::isRelevant` (`filter/param.h:42-44`),
///        i.e. `FilterInfo::isOn()` (`filter/info.h:91`) for the LPF slot.
bool has_lpf(const MenuContext& ctx);
/// @brief Is the high-pass filter on? The same override for the HPF slot (`filter/param.h:42-44`,
///        `filter/info.h:91`).
bool has_hpf(const MenuContext& ctx);
/// @brief Is the sound in FM mode? `modulator::Transpose::isRelevant` (`modulator/transpose.h:76-79`),
///        `source::patched_param::ModulatorFeedback` (`modulator_feedback.h:31-34`),
///        `ModulatorLevel` (`modulator_level.h:31-34`), `submenu::Modulator` (`submenu/modulator.h:25-28`)
///        and `modulator::Destination` (`modulator/destination.h:65-68`) -- the last two spell it
///        `sound->synthMode`, the first three `sound->getSynthMode()`, which is the same member
///        (`processing/sound/sound.h:229`).
bool is_fm(const MenuContext& ctx);
/// @brief Is the sound NOT in FM mode? `osc::Type::isRelevant` (`osc/type.h:115-118`) and
///        `patched_param::IntegerNonFM::isRelevant` (`patched_param/integer_non_fm.h:25-28`).
bool not_fm(const MenuContext& ctx);
/// @brief Is the sound NOT in ring-mod mode? `osc::source::Volume::isRelevant`
///        (`osc/source/volume.h:32-35`).
bool not_ringmod(const MenuContext& ctx);
/// @brief Is the sound in subtractive mode? `osc::AudioRecorder::isRelevant`
///        (`osc/audio_recorder.h:60-63`).
bool is_subtractive(const MenuContext& ctx);
/// @brief May oscillator sync be used? `osc::Sync::isRelevant` (`osc/sync.h:56-60`): not FM, and
///        neither source is a sample.
bool osc_sync_allowed(const MenuContext& ctx);
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

/// @brief `osc::source::WaveIndex::isRelevant` (`osc/source/wave_index.h:30-35`): not FM, wavetable,
///        and an audio file loaded.
/// @{
bool osc0_wave_index(const MenuContext& ctx);
bool osc1_wave_index(const MenuContext& ctx);
/// @}

/// @brief `osc::PulseWidth::isRelevant` (`osc/pulse_width.h:39-52`): not FM; a wavetable source
///        needs a loaded file; sample and the three input modes are out.
/// @{
bool osc0_pulse_width(const MenuContext& ctx);
bool osc1_pulse_width(const MenuContext& ctx);
/// @}

/// @brief `osc::source::Feedback::isRelevant` (`osc/source/feedback.h:31-34`): FM, or the source is
///        a DX7 operator stack.
/// @{
bool osc0_carrier_feedback(const MenuContext& ctx);
bool osc1_carrier_feedback(const MenuContext& ctx);
/// @}

/// @brief `osc::RetriggerPhase::isRelevant` with `for_modulator == false`
///        (`osc/retrigger_phase.h:76-87`).
/// @{
bool osc0_retrigger_phase(const MenuContext& ctx);
bool osc1_retrigger_phase(const MenuContext& ctx);
/// @}

/// @brief The same override with `for_modulator == true` (`osc/retrigger_phase.h:76-87`): FM only,
///        then the source test.
/// @{
bool modulator0_retrigger_phase(const MenuContext& ctx);
bool modulator1_retrigger_phase(const MenuContext& ctx);
/// @}

/// @brief `isSampleModeSample` (`gui/menu_item/sample/utils.cpp:9-15`) -- the predicate
///        `sample::Reverse` (`sample/reverse.h:34-36`), `Repeat` (`sample/repeat.h:39-41`),
///        `TimeStretch` (`sample/time_stretch.h:36-38`) and `LoopPoint`
///        (`sample/loop_point.cpp:34-36`, i.e. `Start`/`End`) all delegate to.
/// @{
bool osc0_is_sample(const MenuContext& ctx);
bool osc1_is_sample(const MenuContext& ctx);
/// @}

/// @brief `sample::PitchSpeed::isRelevant` (`sample/pitch_speed.h:35-37`): an audio clip, or
///        `isSampleModeSample`.
/// @{
bool osc0_pitch_speed(const MenuContext& ctx);
bool osc1_pitch_speed(const MenuContext& ctx);
/// @}

/// @brief `sample::Interpolation::isRelevant` (`sample/interpolation.h:61-71`): an audio clip, or a
///        subtractive sound whose source is a loaded sample or one of the three input modes.
/// @{
bool osc0_interpolation(const MenuContext& ctx);
bool osc1_interpolation(const MenuContext& ctx);
/// @}

/// @}

/// @name Batch-2 predicates -- the rest of the sound-editor root (Task 7)
///
/// Where batch 1's predicates all ask about the *sound* (its synth mode, its sources), most of
/// these ask about the *clip*: is this a kit row, a kit's global FX page, a MIDI or CV clip, a
/// gate drum. `MenuContext` carries every one of those facts (`is_kit`, `is_kit_global_fx`,
/// `output`, `arp`), so the `SoundEditor::editingKit()` / `editingKitRow()` /
/// `editingCVOrMIDIClip()` / `editingNonAudioDrumRow()` / `editingGateDrumRow()` family
/// (`gui/ui/sound_editor.cpp:161-202`) is transcribed against it rather than against globals.
/// @{

/// @brief May the sound be switched between synth modes? `SynthModeSelection::isRelevant`
///        (`gui/menu_item/synth_mode.h:69-73`): both sources' `oscType` are ring-moddable.
bool ringmod_allowed(const MenuContext& ctx);

/// @brief Is a kit being edited? `sidechain::Send::isRelevant` (`sidechain/send.h:65`), i.e.
///        `SoundEditor::editingKit()` (`sound_editor.cpp:161-163`).
bool is_kit(const MenuContext& ctx);
/// @brief Is a kit *row* being edited? `IncludeInKitArp::isRelevant` (`include_in_kit_arp.h:65-67`),
///        i.e. `editingKitRow()` (`sound_editor.cpp:169-171`).
bool is_kit_row(const MenuContext& ctx);
/// @brief The negation, `arpeggiator::NoteMode::isRelevant` (`arpeggiator/note_mode.h:37-39`).
bool not_kit_row(const MenuContext& ctx);
/// @brief A kit row that is not a gate drum. `arpeggiator::ChordType::isRelevant`
///        (`arpeggiator/chord_type.h:46-48`) and `NoteModeForDrums` (`note_mode_for_drums.h:43-45`).
bool kit_row_not_gate(const MenuContext& ctx);
/// @brief Not a gate drum row. `arpeggiator::StepRepeat::isRelevant` (`step_repeat.h:39-41`) and
///        `ArpMpeVelocity` (`mpe_velocity.h:37-39`).
bool not_gate_drum_row(const MenuContext& ctx);
/// @brief Neither a gate drum row nor a kit's global-FX page. `arpeggiator::Octaves::isRelevant`
///        (`octaves.h:39-41`), `OctaveMode` (`octave_mode.h:44-46`) and `submenu::ArpMpeSubmenu`
///        (`submenu/arp_mpe_submenu.h:22-24`) -- one predicate, three items.
bool not_gate_drum_or_kit_fx(const MenuContext& ctx);

/// @brief A sound (not MIDI/CV, not a non-audio drum row). `ArpUnpatchedParam::isRelevant`
///        (`arpeggiator/arp_unpatched_param.h:29-31`).
bool arp_sound_only(const MenuContext& ctx);
/// @brief The same, and not the kit's global-FX page. `arpeggiator::Rate::isRelevant`
///        (`arpeggiator/rate.h:26-29`).
bool arp_rate_sound(const MenuContext& ctx);
/// @brief The complement: a MIDI/CV clip or a non-audio drum row.
///        `arpeggiator::midi_cv::ArpNonSoundInteger::isRelevant` (`midi_cv/arp_integer.h:27-29`),
///        and `randomizer::midi_cv::NoteProbability` (`midi_cv/note_probability.h:32-34`), which
///        spells the same test.
bool arp_non_sound(const MenuContext& ctx);
/// @brief A kit's global-FX page. `arpeggiator::KitRate::isRelevant` (`arpeggiator/rate.h:26-29`),
///        i.e. `editingKitAffectEntire()` (`sound_editor.cpp:165-167`). Declared for Task 8's kit
///        root; batch 2 reaches it through no node.
bool is_kit_global_fx(const MenuContext& ctx);

/// @brief A sound randomizer parameter that is off when the arpeggiator is.
///        `RandomizerUnpatchedParam::isRelevant` (`randomizer/randomizer_unpatched_param.h:14-25`)
///        on a *non*-global parameter id.
bool randomizer_param(const MenuContext& ctx);
/// @brief The same override on one of its three global ids (`UNPATCHED_SPREAD_VELOCITY`,
///        `UNPATCHED_NOTE_PROBABILITY`, `UNPATCHED_REVERSE_PROBABILITY`), which stay on with the
///        arpeggiator off (`randomizer_unpatched_param.h:20-24`).
bool randomizer_global_param(const MenuContext& ctx);
/// @brief `RandomizerSoundOnlyUnpatchedParam::isRelevant` (`randomizer_unpatched_param.h:37-40`).
bool randomizer_sound_only(const MenuContext& ctx);
/// @brief `RandomizerNonKitSoundUnpatchedParam::isRelevant` (`randomizer_unpatched_param.h:52-55`).
bool randomizer_non_kit(const MenuContext& ctx);
/// @brief `randomizer::midi_cv::RandomizerNonSoundInteger::isRelevant`
///        (`randomizer/midi_cv/randomizer_integer.h:27-30`).
bool randomizer_non_sound(const MenuContext& ctx);
/// @brief A MIDI/CV clip or a MIDI drum row, with the arpeggiator on -- the override shared by
///        `midi_cv::SpreadOctave` (`spread_octave.h:32-35`), `SwapProbability`
///        (`swap_probability.h:32-35`), `BassProbability` (`bass_probability.h:33-36`) and
///        `GlideProbability` (`glide_probability.h:33-36`).
bool randomizer_midi_target(const MenuContext& ctx);
/// @brief The same without the arpeggiator test. `midi_cv::SpreadVelocity::isRelevant`
///        (`spread_velocity.h:32-34`).
bool is_cv_midi_or_midi_drum(const MenuContext& ctx);
/// @brief A MIDI/CV *clip* with the arpeggiator on. `midi_cv::ChordPolyphony::isRelevant`
///        (`chord_polyphony.h:31-33`) and `ChordProbability` (`chord_probability.h:32-34`).
bool cv_midi_clip_arp_on(const MenuContext& ctx);

/// @brief Is a mod-FX type selected at all? `mod_fx::Rate::isRelevant` (`mod_fx/rate.h:27-29`).
bool mod_fx_on(const MenuContext& ctx);
/// @brief Does the selected mod-FX type have a depth? `mod_fx::Depth_Patched::isRelevant`
///        (`mod_fx/depth_patched.h:24-28`).
bool mod_fx_has_depth(const MenuContext& ctx);
/// @brief ... a feedback? `mod_fx::Feedback::isRelevant` (`mod_fx/feedback.h:22-25`).
bool mod_fx_has_feedback(const MenuContext& ctx);
/// @brief ... an offset? `mod_fx::Offset::isRelevant` (`mod_fx/offset.h:23-27`).
bool mod_fx_has_offset(const MenuContext& ctx);

/// @brief Does the selected reverb model have filters? `reverb::HPF::isRelevant`
///        (`reverb/hpf.h:26-29`) and `LPF` (`reverb/lpf.h:26-29`): MUTABLE or DIGITAL.
bool reverb_filters(const MenuContext& ctx);
/// @brief Is reverb ducking set to something other than AUTO? `reverb::sidechain::Shape::isRelevant`
///        (`reverb/sidechain/shape.h:31-33`), `sidechain::Attack` / `Release` / `Sync` in their
///        reverb spelling (`sidechain/attack.h:57-59`, `release.h:57-59`, `sync.h:56-58`).
bool reverb_sidechain_on(const MenuContext& ctx);

/// @brief May stutter quantization be edited? `stutter::QuantizedStutter::isRelevant`
///        (`stutter/quantized.h:44-47`).
bool stutter_quantize_allowed(const MenuContext& ctx);

/// @brief Is the sound polyphonic? `voice::VoiceCount::isRelevant` (`voice/polyphony.h:62-65`).
bool is_polyphonic(const MenuContext& ctx);

/// @brief Does this output get the two-item bend-range submenu? `submenu::Bend::isRelevant`
///        (`submenu/bend.h:26-31`): SYNTH, CV or MIDI_OUT.
bool bend_submenu(const MenuContext& ctx);
/// @brief `bend_range::PerFinger::isRelevant` (`bend_range/per_finger.h:41`) **at menu depth 0**.
///
/// Legacy is `soundEditor.navigationDepth == 1 || soundEditor.editingKit()`. The depth is a fact
/// about *where the item sits in the tree*, not about what is being edited, so it is resolved
/// statically rather than carried on `MenuContext`: this predicate is the depth-0 reading, used by
/// `drum_bend_range` (a root child), and the depth-1 reading -- unconditionally true -- is a null
/// `relevant` on `bend_range_per_finger`, which is only reachable inside `bendMenu`. See
/// `sound_bend.h`'s two notes (review round 1, I1).
bool per_finger_bend(const MenuContext& ctx);

/// @brief `midi::sound::OutputMidiNoteForDrum::isRelevant` (`midi/sound/note_for_drum.h:34-36`):
///        a kit row that is not a non-audio drum.
bool output_midi_note(const MenuContext& ctx);
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

/// @brief Is this a CV output whose channel is `both`? `cv::DualCVSelection::isRelevant`
///        (`gui/menu_item/cv/cv2Mapping.h:44-48`) -- the CV2 source picker only applies to the
///        dual-channel CV instrument.
bool cv2_dual(const MenuContext& ctx);

/// @brief Is the battery readout switched on? `battery::Level::isRelevant`
///        (`gui/menu_item/battery/level.h:25-27`), i.e.
///        `runtimeFeatureSettings.isOn(RuntimeFeatureSettingType::ShowBatteryLevel)`.
bool battery_shown(const MenuContext& ctx);

/// @brief Is the audio clip's output recording from one specific track?
///        `audio_clip::SpecificSourceOutputSelector::isRelevant`
///        (`gui/menu_item/audio_clip/specific_output_source_selector.h:86-89`):
///        `AudioOutput::inputChannel == AudioInputChannel::SPECIFIC_OUTPUT`.
bool audio_source_is_specific(const MenuContext& ctx);
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
