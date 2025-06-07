#include "gui/l10n/strings.h"
#include "gui/menu_item/active_scales.h"
#include "gui/menu_item/arpeggiator/arp_unpatched_param.h"
#include "gui/menu_item/arpeggiator/chord_type.h"
#include "gui/menu_item/arpeggiator/include_in_kit_arp.h"
#include "gui/menu_item/arpeggiator/midi_cv/bass_probability.h"
#include "gui/menu_item/arpeggiator/midi_cv/chord_polyphony.h"
#include "gui/menu_item/arpeggiator/midi_cv/chord_probability.h"
#include "gui/menu_item/arpeggiator/midi_cv/gate.h"
#include "gui/menu_item/arpeggiator/midi_cv/note_probability.h"
#include "gui/menu_item/arpeggiator/midi_cv/ratchet_amount.h"
#include "gui/menu_item/arpeggiator/midi_cv/ratchet_probability.h"
#include "gui/menu_item/arpeggiator/midi_cv/rate.h"
#include "gui/menu_item/arpeggiator/midi_cv/rhythm.h"
#include "gui/menu_item/arpeggiator/midi_cv/sequence_length.h"
#include "gui/menu_item/arpeggiator/midi_cv/spread_gate.h"
#include "gui/menu_item/arpeggiator/midi_cv/spread_octave.h"
#include "gui/menu_item/arpeggiator/midi_cv/spread_velocity.h"
#include "gui/menu_item/arpeggiator/midi_cv/step_probability.h"
#include "gui/menu_item/arpeggiator/mode.h"
#include "gui/menu_item/arpeggiator/mpe_velocity.h"
#include "gui/menu_item/arpeggiator/note_mode.h"
#include "gui/menu_item/arpeggiator/note_mode_for_drums.h"
#include "gui/menu_item/arpeggiator/octave_mode.h"
#include "gui/menu_item/arpeggiator/octaves.h"
#include "gui/menu_item/arpeggiator/preset_mode.h"
#include "gui/menu_item/arpeggiator/randomizer_lock.h"
#include "gui/menu_item/arpeggiator/rate.h"
#include "gui/menu_item/arpeggiator/rhythm.h"
#include "gui/menu_item/arpeggiator/step_repeat.h"
#include "gui/menu_item/arpeggiator/sync.h"
#include "gui/menu_item/audio_clip/attack.h"
#include "gui/menu_item/audio_clip/audio_source_selector.h"
#include "gui/menu_item/audio_clip/reverse.h"
#include "gui/menu_item/audio_clip/sample_marker_editor.h"
#include "gui/menu_item/audio_clip/set_clip_length_equal_to_sample_length.h"
#include "gui/menu_item/audio_clip/transpose.h"
#include "gui/menu_item/audio_compressor/compressor_params.h"
#include "gui/menu_item/audio_compressor/compressor_values.h"
#include "gui/menu_item/audio_interpolation.h"
#include "gui/menu_item/bend_range/main.h"
#include "gui/menu_item/bend_range/per_finger.h"
#include "gui/menu_item/colour.h"
#include "gui/menu_item/cv/cv2Mapping.h"
#include "gui/menu_item/cv/selection.h"
#include "gui/menu_item/cv/submenu.h"
#include "gui/menu_item/cv/transpose.h"
#include "gui/menu_item/cv/volts.h"
#include "gui/menu_item/defaults/bend_range.h"
#include "gui/menu_item/defaults/favourites_layout.h"
#include "gui/menu_item/defaults/grid_default_active_mode.h"
#include "gui/menu_item/defaults/hold_time.h"
#include "gui/menu_item/defaults/keyboard_layout.h"
#include "gui/menu_item/defaults/magnitude.h"
#include "gui/menu_item/defaults/metronome_volume.h"
#include "gui/menu_item/defaults/pad_brightness.h"
#include "gui/menu_item/defaults/scale.h"
#include "gui/menu_item/defaults/session_layout.h"
#include "gui/menu_item/defaults/slice_mode.h"
#include "gui/menu_item/defaults/startup_song_mode.h"
#include "gui/menu_item/defaults/swing_interval.h"
#include "gui/menu_item/defaults/ui/clip_type/default_new_clip_type.h"
#include "gui/menu_item/defaults/velocity.h"
#include "gui/menu_item/delay/analog.h"
#include "gui/menu_item/delay/ping_pong.h"
#include "gui/menu_item/delay/sync.h"
#include "gui/menu_item/dx/browse.h"
#include "gui/menu_item/dx/engine_select.h"
#include "gui/menu_item/dx/global_params.h"
#include "gui/menu_item/dx/param.h"
#include "gui/menu_item/edit_name.h"
#include "gui/menu_item/envelope/segment.h"
#include "gui/menu_item/eq/eq_unpatched_param.h"
#include "gui/menu_item/file_selector.h"
#include "gui/menu_item/filter/mode.h"
#include "gui/menu_item/filter/param.h"
#include "gui/menu_item/filter_route.h"
#include "gui/menu_item/firmware/version.h"
#include "gui/menu_item/flash/status.h"
#include "gui/menu_item/fx/clipping.h"
#include "gui/menu_item/gate/mode.h"
#include "gui/menu_item/gate/off_time.h"
#include "gui/menu_item/gate/selection.h"
#include "gui/menu_item/integer_range.h"
#include "gui/menu_item/key_range.h"
#include "gui/menu_item/keyboard/layout.h"
#include "gui/menu_item/lfo/sync.h"
#include "gui/menu_item/lfo/type.h"
#include "gui/menu_item/master_transpose.h"
#include "gui/menu_item/menu_item.h"
#include "gui/menu_item/midi/after_touch_to_mono.h"
#include "gui/menu_item/midi/bank.h"
#include "gui/menu_item/midi/command.h"
#include "gui/menu_item/midi/default_velocity_to_level.h"
#include "gui/menu_item/midi/device.h"
#include "gui/menu_item/midi/device_definition/linked.h"
#include "gui/menu_item/midi/device_definition/submenu.h"
#include "gui/menu_item/midi/device_send_clock.h"
#include "gui/menu_item/midi/devices.h"
#include "gui/menu_item/midi/follow/follow_channel.h"
#include "gui/menu_item/midi/follow/follow_feedback_automation.h"
#include "gui/menu_item/midi/follow/follow_feedback_channel_type.h"
#include "gui/menu_item/midi/follow/follow_kit_root_note.h"
#include "gui/menu_item/midi/mpe_to_mono.h"
#include "gui/menu_item/midi/pgm.h"
#include "gui/menu_item/midi/program.h"
#include "gui/menu_item/midi/sound/channel.h"
#include "gui/menu_item/midi/sound/note_for_drum.h"
#include "gui/menu_item/midi/sub.h"
#include "gui/menu_item/midi/takeover.h"
#include "gui/menu_item/midi/transpose.h"
#include "gui/menu_item/mod_fx/depth_patched.h"
#include "gui/menu_item/mod_fx/depth_unpatched.h"
#include "gui/menu_item/mod_fx/feedback.h"
#include "gui/menu_item/mod_fx/offset.h"
#include "gui/menu_item/mod_fx/rate.h"
#include "gui/menu_item/mod_fx/rate_unpatched.h"
#include "gui/menu_item/mod_fx/type.h"
#include "gui/menu_item/modulator/destination.h"
#include "gui/menu_item/modulator/transpose.h"
#include "gui/menu_item/monitor/mode.h"
#include "gui/menu_item/mpe/direction_selector.h"
#include "gui/menu_item/note/fill.h"
#include "gui/menu_item/note/iterance_divisor.h"
#include "gui/menu_item/note/iterance_preset.h"
#include "gui/menu_item/note/iterance_step_toggle.h"
#include "gui/menu_item/note/probability.h"
#include "gui/menu_item/note/velocity.h"
#include "gui/menu_item/note_row/fill.h"
#include "gui/menu_item/note_row/iterance_divisor.h"
#include "gui/menu_item/note_row/iterance_preset.h"
#include "gui/menu_item/note_row/iterance_step_toggle.h"
#include "gui/menu_item/note_row/probability.h"
#include "gui/menu_item/osc/audio_recorder.h"
#include "gui/menu_item/osc/pulse_width.h"
#include "gui/menu_item/osc/retrigger_phase.h"
#include "gui/menu_item/osc/source/feedback.h"
#include "gui/menu_item/osc/source/volume.h"
#include "gui/menu_item/osc/source/wave_index.h"
#include "gui/menu_item/osc/sync.h"
#include "gui/menu_item/osc/type.h"
#include "gui/menu_item/patch_cable_strength/fixed.h"
#include "gui/menu_item/patch_cables.h"
#include "gui/menu_item/patched_param/integer.h"
#include "gui/menu_item/patched_param/integer_non_fm.h"
#include "gui/menu_item/patched_param/pan.h"
#include "gui/menu_item/performance_session_view/editing_mode.h"
#include "gui/menu_item/record/countin.h"
#include "gui/menu_item/record/loop_command.h"
#include "gui/menu_item/record/quantize.h"
#include "gui/menu_item/record/threshold_mode.h"
#include "gui/menu_item/reverb/damping.h"
#include "gui/menu_item/reverb/hpf.h"
#include "gui/menu_item/reverb/lpf.h"
#include "gui/menu_item/reverb/model.h"
#include "gui/menu_item/reverb/pan.h"
#include "gui/menu_item/reverb/room_size.h"
#include "gui/menu_item/reverb/sidechain/shape.h"
#include "gui/menu_item/reverb/sidechain/volume.h"
#include "gui/menu_item/reverb/width.h"
#include "gui/menu_item/runtime_feature/setting.h"
#include "gui/menu_item/runtime_feature/settings.h"
#include "gui/menu_item/sample/browser_preview/mode.h"
#include "gui/menu_item/sample/end.h"
#include "gui/menu_item/sample/interpolation.h"
#include "gui/menu_item/sample/pitch_speed.h"
#include "gui/menu_item/sample/repeat.h"
#include "gui/menu_item/sample/reverse.h"
#include "gui/menu_item/sample/start.h"
#include "gui/menu_item/sample/time_stretch.h"
#include "gui/menu_item/sample/transpose.h"
#include "gui/menu_item/sequence/direction.h"
#include "gui/menu_item/sequence/tempo.h"
#include "gui/menu_item/shortcuts/version.h"
#include "gui/menu_item/sidechain/attack.h"
#include "gui/menu_item/sidechain/release.h"
#include "gui/menu_item/sidechain/send.h"
#include "gui/menu_item/sidechain/sync.h"
#include "gui/menu_item/sidechain/volume.h"
#include "gui/menu_item/song/configure_macros.h"
#include "gui/menu_item/song/midi_learn.h"
#include "gui/menu_item/source/patched_param/fm.h"
#include "gui/menu_item/stem_export/start.h"
#include "gui/menu_item/stutter/direction.h"
#include "gui/menu_item/stutter/quantized.h"
#include "gui/menu_item/stutter/rate.h"
#include "gui/menu_item/submenu.h"
#include "gui/menu_item/submenu/MPE.h"
#include "gui/menu_item/submenu/actual_source.h"
#include "gui/menu_item/submenu/arp_mpe_submenu.h"
#include "gui/menu_item/submenu/bend.h"
#include "gui/menu_item/submenu/mod_fx.h"
#include "gui/menu_item/submenu/modulator.h"
#include "gui/menu_item/submenu/reverb_sidechain.h"
#include "gui/menu_item/submenu/sidechain.h"
#include "gui/menu_item/swing/interval.h"
#include "gui/menu_item/synth_mode.h"
#include "gui/menu_item/trigger/in/ppqn.h"
#include "gui/menu_item/trigger/out/ppqn.h"
#include "gui/menu_item/unison/count.h"
#include "gui/menu_item/unison/detune.h"
#include "gui/menu_item/unison/stereoSpread.h"
#include "gui/menu_item/unpatched_param.h"
#include "gui/menu_item/unpatched_param/pan.h"
#include "gui/menu_item/unpatched_param/updating_reverb_params.h"
#include "gui/menu_item/voice/polyphony.h"
#include "gui/menu_item/voice/portamento.h"
#include "gui/menu_item/voice/priority.h"
#include "io/midi/midi_device_manager.h"
#include "io/midi/midi_engine.h"
#include "model/song/song.h"
#include "modulation/params/param.h"
#include "playback/playback_handler.h"
#include "storage/flash_storage.h"
#include <new>

using enum l10n::String;
using namespace deluge::modulation;
using namespace deluge;
using namespace gui::menu_item;
using namespace gui;
namespace params = deluge::modulation::params;

// Include the autogenerated menu structures
#include "gui/menu_item/audio_clip/specific_output_source_selector.h"
#include "gui/menu_item/generate/g_menus.inc"
#include "gui/menu_item/midi/y_axis_to_cc1.h"

// Arp --------------------------------------------------------------------------------------
arpeggiator::PresetMode arpPresetModeMenu{STRING_FOR_PRESET, STRING_FOR_ARP_PRESET_MENU_TITLE};
// Rate
arpeggiator::Mode arpModeMenu{STRING_FOR_ENABLED, STRING_FOR_ARP_MODE_MENU_TITLE};
arpeggiator::Sync arpSyncMenu{STRING_FOR_SYNC, STRING_FOR_ARP_SYNC_MENU_TITLE};
arpeggiator::Rate arpRateMenu{STRING_FOR_RATE, STRING_FOR_ARP_RATE_MENU_TITLE, params::GLOBAL_ARP_RATE};
arpeggiator::KitRate arpKitRateMenu{STRING_FOR_RATE, STRING_FOR_ARP_RATE_MENU_TITLE, params::UNPATCHED_ARP_RATE};
arpeggiator::midi_cv::Rate arpRateMenuMIDIOrCV{STRING_FOR_RATE, STRING_FOR_ARP_RATE_MENU_TITLE};
// Pattern
arpeggiator::Octaves arpOctavesMenu{STRING_FOR_NUMBER_OF_OCTAVES, STRING_FOR_ARP_OCTAVES_MENU_TITLE};
arpeggiator::OctaveMode arpOctaveModeMenu{STRING_FOR_OCTAVE_MODE, STRING_FOR_ARP_OCTAVE_MODE_MENU_TITLE};
arpeggiator::OctaveModeToNoteMode arpeggiator::arpOctaveModeToNoteModeMenu{STRING_FOR_OCTAVE_MODE,
                                                                           STRING_FOR_ARP_OCTAVE_MODE_MENU_TITLE};
arpeggiator::OctaveModeToNoteModeForDrums arpeggiator::arpOctaveModeToNoteModeMenuForDrums{
    STRING_FOR_OCTAVE_MODE, STRING_FOR_ARP_OCTAVE_MODE_MENU_TITLE};
arpeggiator::NoteMode arpNoteModeMenu{STRING_FOR_NOTE_MODE, STRING_FOR_ARP_NOTE_MODE_MENU_TITLE};
arpeggiator::NoteModeForDrums arpNoteModeMenuForDrums{STRING_FOR_NOTE_MODE, STRING_FOR_ARP_NOTE_MODE_MENU_TITLE};
arpeggiator::NoteModeFromOctaveMode arpeggiator::arpNoteModeFromOctaveModeMenu{STRING_FOR_NOTE_MODE,
                                                                               STRING_FOR_ARP_NOTE_MODE_MENU_TITLE};
arpeggiator::NoteModeFromOctaveModeForDrums arpeggiator::arpNoteModeFromOctaveModeMenuForDrums{
    STRING_FOR_NOTE_MODE, STRING_FOR_ARP_NOTE_MODE_MENU_TITLE};
arpeggiator::ChordType arpChordSimulatorMenuKit{STRING_FOR_CHORD_SIMULATOR, STRING_FOR_ARP_CHORD_SIMULATOR_MENU_TITLE};
arpeggiator::StepRepeat arpStepRepeatMenu{STRING_FOR_STEP_REPEAT, STRING_FOR_ARP_STEP_REPEAT_MENU_TITLE};
// Note and rhythm settings
arpeggiator::ArpUnpatchedParam arpGateMenu{STRING_FOR_GATE, STRING_FOR_ARP_GATE_MENU_TITLE, params::UNPATCHED_ARP_GATE};
arpeggiator::midi_cv::Gate arpGateMenuMIDIOrCV{STRING_FOR_GATE, STRING_FOR_ARP_GATE_MENU_TITLE};
arpeggiator::Rhythm arpRhythmMenu{STRING_FOR_RHYTHM, STRING_FOR_ARP_RHYTHM_MENU_TITLE, params::UNPATCHED_ARP_RHYTHM};
arpeggiator::midi_cv::Rhythm arpRhythmMenuMIDIOrCV{STRING_FOR_RHYTHM, STRING_FOR_ARP_RHYTHM_MENU_TITLE};
arpeggiator::ArpNonKitSoundUnpatchedParam arpChordPolyphonyMenu{
    STRING_FOR_CHORD_POLYPHONY, STRING_FOR_ARP_CHORD_POLYPHONY_MENU_TITLE, params::UNPATCHED_ARP_CHORD_POLYPHONY};
arpeggiator::midi_cv::ChordPolyphony arpChordPolyphonyMenuMIDIOrCV{STRING_FOR_CHORD_POLYPHONY,
                                                                   STRING_FOR_ARP_CHORD_POLYPHONY_MENU_TITLE};
arpeggiator::ArpUnpatchedParam arpSequenceLengthMenu{
    STRING_FOR_SEQUENCE_LENGTH, STRING_FOR_ARP_SEQUENCE_LENGTH_MENU_TITLE, params::UNPATCHED_ARP_SEQUENCE_LENGTH};
arpeggiator::midi_cv::SequenceLength arpSequenceLengthMenuMIDIOrCV{STRING_FOR_SEQUENCE_LENGTH,
                                                                   STRING_FOR_ARP_SEQUENCE_LENGTH_MENU_TITLE};
arpeggiator::ArpUnpatchedParam arpRatchetAmountMenu{STRING_FOR_NUMBER_OF_RATCHETS, STRING_FOR_ARP_RATCHETS_MENU_TITLE,
                                                    params::UNPATCHED_ARP_RATCHET_AMOUNT};
arpeggiator::midi_cv::RatchetAmount arpRatchetAmountMenuMIDIOrCV{STRING_FOR_NUMBER_OF_RATCHETS,
                                                                 STRING_FOR_ARP_RATCHETS_MENU_TITLE};
// Randomizer
arpeggiator::IncludeInKitArp arpIncludeInKitArpMenu{STRING_FOR_INCLUDE_IN_KIT_ARP, STRING_FOR_INCLUDE_IN_KIT_ARP};
arpeggiator::RandomizerLock arpRandomizerLockMenu{STRING_FOR_RANDOMIZER_LOCK, STRING_FOR_ARP_RANDOMIZER_LOCK_TITLE};
arpeggiator::ArpUnpatchedParam arpNoteProbabilityMenu{
    STRING_FOR_NOTE_PROBABILITY, STRING_FOR_NOTE_PROBABILITY_MENU_TITLE, params::UNPATCHED_NOTE_PROBABILITY};
arpeggiator::midi_cv::NoteProbability arpNoteProbabilityMenuMIDIOrCV{STRING_FOR_NOTE_PROBABILITY,
                                                                     STRING_FOR_NOTE_PROBABILITY_MENU_TITLE};
arpeggiator::ArpUnpatchedParam arpBassProbabilityMenu{
    STRING_FOR_BASS_PROBABILITY, STRING_FOR_ARP_BASS_PROBABILITY_MENU_TITLE, params::UNPATCHED_ARP_BASS_PROBABILITY};
arpeggiator::midi_cv::BassProbability arpBassProbabilityMenuMIDIOrCV{STRING_FOR_BASS_PROBABILITY,
                                                                     STRING_FOR_ARP_BASS_PROBABILITY_MENU_TITLE};
arpeggiator::ArpUnpatchedParam arpStepProbabilityMenu{
    STRING_FOR_STEP_PROBABILITY, STRING_FOR_ARP_STEP_PROBABILITY_MENU_TITLE, params::UNPATCHED_ARP_STEP_PROBABILITY};
arpeggiator::midi_cv::StepProbability arpStepProbabilityMenuMIDIOrCV{STRING_FOR_STEP_PROBABILITY,
                                                                     STRING_FOR_ARP_STEP_PROBABILITY_MENU_TITLE};
arpeggiator::ArpNonKitSoundUnpatchedParam arpChordProbabilityMenu{
    STRING_FOR_CHORD_PROBABILITY, STRING_FOR_ARP_CHORD_PROBABILITY_MENU_TITLE, params::UNPATCHED_ARP_CHORD_PROBABILITY};
arpeggiator::midi_cv::ChordProbability arpChordProbabilityMenuMIDIOrCV{STRING_FOR_CHORD_PROBABILITY,
                                                                       STRING_FOR_ARP_CHORD_PROBABILITY_MENU_TITLE};
arpeggiator::ArpUnpatchedParam arpRatchetProbabilityMenu{STRING_FOR_RATCHET_PROBABILITY,
                                                         STRING_FOR_ARP_RATCHET_PROBABILITY_MENU_TITLE,
                                                         params::UNPATCHED_ARP_RATCHET_PROBABILITY};
arpeggiator::midi_cv::RatchetProbability arpRatchetProbabilityMenuMIDIOrCV{
    STRING_FOR_RATCHET_PROBABILITY, STRING_FOR_ARP_RATCHET_PROBABILITY_MENU_TITLE};
arpeggiator::ArpUnpatchedParam arpReverseProbabilityMenu{
    STRING_FOR_REVERSE_PROBABILITY, STRING_FOR_REVERSE_PROBABILITY_MENU_TITLE, params::UNPATCHED_REVERSE_PROBABILITY};
arpeggiator::ArpUnpatchedParam arpSpreadVelocityMenu{STRING_FOR_SPREAD_VELOCITY, STRING_FOR_SPREAD_VELOCITY_MENU_TITLE,
                                                     params::UNPATCHED_SPREAD_VELOCITY};
arpeggiator::midi_cv::SpreadVelocity arpSpreadVelocityMenuMIDIOrCV{STRING_FOR_SPREAD_VELOCITY,
                                                                   STRING_FOR_SPREAD_VELOCITY_MENU_TITLE};
arpeggiator::ArpUnpatchedParam arpSpreadGateMenu{STRING_FOR_SPREAD_GATE, STRING_FOR_ARP_SPREAD_GATE_MENU_TITLE,
                                                 params::UNPATCHED_ARP_SPREAD_GATE};
arpeggiator::midi_cv::SpreadGate arpSpreadGateMenuMIDIOrCV{STRING_FOR_SPREAD_GATE,
                                                           STRING_FOR_ARP_SPREAD_GATE_MENU_TITLE};
arpeggiator::ArpSoundOnlyUnpatchedParam arpSpreadOctaveMenu{
    STRING_FOR_SPREAD_OCTAVE, STRING_FOR_ARP_SPREAD_OCTAVE_MENU_TITLE, params::UNPATCHED_ARP_SPREAD_OCTAVE};
arpeggiator::midi_cv::SpreadOctave arpSpreadOctaveMenuMIDIOrCV{STRING_FOR_SPREAD_OCTAVE,
                                                               STRING_FOR_ARP_SPREAD_OCTAVE_MENU_TITLE};

// Arp: Basic
HorizontalMenu arpBasicMenu{
    STRING_FOR_BASIC, {&arpGateMenu, &arpSyncMenu, &arpRateMenu}, HorizontalMenu::Layout::FIXED};
HorizontalMenu arpBasicMenuKit{
    STRING_FOR_BASIC, {&arpGateMenu, &arpSyncMenu, &arpKitRateMenu}, HorizontalMenu::Layout::FIXED};
HorizontalMenu arpBasicMenuMIDIOrCV{
    STRING_FOR_BASIC, {&arpGateMenuMIDIOrCV, &arpSyncMenu, &arpRateMenuMIDIOrCV}, HorizontalMenu::Layout::FIXED};

// Arp: Pattern
HorizontalMenu arpPatternMenu{STRING_FOR_PATTERN,
                              {// Pattern
                               &arpOctavesMenu, &arpOctaveModeMenu, &arpChordSimulatorMenuKit, &arpNoteModeMenu,
                               &arpNoteModeMenuForDrums, &arpStepRepeatMenu,
                               // Note and rhythm settings
                               &arpRhythmMenu, &arpRhythmMenuMIDIOrCV, &arpSequenceLengthMenu,
                               &arpSequenceLengthMenuMIDIOrCV}};
// Arp: Randomizer
HorizontalMenu arpRandomizerMenu{STRING_FOR_RANDOMIZER,
                                 {// Lock
                                  &arpRandomizerLockMenu,
                                  // Spreads
                                  &arpSpreadGateMenu, &arpSpreadGateMenuMIDIOrCV, &arpSpreadOctaveMenu,
                                  &arpSpreadOctaveMenuMIDIOrCV, &arpSpreadVelocityMenu, &arpSpreadVelocityMenuMIDIOrCV,
                                  // Ratchets
                                  &arpRatchetAmountMenu, &arpRatchetAmountMenuMIDIOrCV, &arpRatchetProbabilityMenu,
                                  &arpRatchetProbabilityMenuMIDIOrCV,
                                  // Chords
                                  &arpChordPolyphonyMenu, &arpChordPolyphonyMenuMIDIOrCV, &arpChordProbabilityMenu,
                                  &arpChordProbabilityMenuMIDIOrCV,
                                  // Note
                                  &arpNoteProbabilityMenu, &arpNoteProbabilityMenuMIDIOrCV,
                                  // Bass
                                  &arpBassProbabilityMenu, &arpBassProbabilityMenuMIDIOrCV,
                                  // Step
                                  &arpStepProbabilityMenu, &arpStepProbabilityMenuMIDIOrCV,
                                  // Reverse
                                  &arpReverseProbabilityMenu}};
// Arp: Preset and Randomizer
HorizontalMenu arpPresetAndRandomizerMenu{STRING_FOR_ARPEGGIATOR, {&arpPresetModeMenu, &arpRandomizerMenu}};
// Global: Randomizer
HorizontalMenu globalRandomizerMenu{STRING_FOR_RANDOMIZER,
                                    {
                                        // Lock
                                        &arpRandomizerLockMenu,
                                        // Spreads
                                        &arpSpreadVelocityMenu,
                                        &arpSpreadVelocityMenuMIDIOrCV,
                                        // Note
                                        &arpNoteProbabilityMenu,
                                        &arpNoteProbabilityMenuMIDIOrCV,
                                        // Reverse
                                        &arpReverseProbabilityMenu,
                                    }};
// Arp: MPE
arpeggiator::ArpMpeVelocity arpMpeVelocityMenu{STRING_FOR_VELOCITY, STRING_FOR_VELOCITY};
submenu::ArpMpeSubmenu arpMpeMenu{STRING_FOR_MPE, {&arpMpeVelocityMenu}};

Submenu arpMenu{
    STRING_FOR_ARPEGGIATOR,
    {
        // Mode
        &arpModeMenu,
        // Basic
        &arpBasicMenu,
        // Pattern
        &arpPatternMenu,
        // Randomizer
        &arpRandomizerMenu,
        // MPE
        &arpMpeMenu,
        // Include in kit arp
        &arpIncludeInKitArpMenu,
    },
};

Submenu arpMenuMIDIOrCV{
    STRING_FOR_ARPEGGIATOR,
    {
        // Mode
        &arpModeMenu,
        // Basic
        &arpBasicMenuMIDIOrCV,
        // Pattern
        &arpPatternMenu,
        // Randomizer
        &arpRandomizerMenu,
        // MPE
        &arpMpeMenu,
        // Include in kit arp
        &arpIncludeInKitArpMenu,
    },
};

Submenu kitArpMenu{
    STRING_FOR_KIT_ARPEGGIATOR,
    {
        // Mode
        &arpModeMenu,
        // Basic
        &arpBasicMenuKit,
        // Pattern
        &arpPatternMenu,
        // Randomizer
        &arpRandomizerMenu,
    },
};

// Voice menu ----------------------------------------------------------------------------------------------------

voice::PolyphonyType polyphonyMenu{STRING_FOR_POLYPHONY};
voice::VoiceCount voice::polyphonicVoiceCountMenu{STRING_FOR_MAX_VOICES};
voice::Portamento portaMenu{STRING_FOR_PORTAMENTO};
voice::Priority priorityMenu{STRING_FOR_PRIORITY};

HorizontalMenu voiceMenu{STRING_FOR_VOICE,
                         {&priorityMenu, &polyphonyMenu, &voice::polyphonicVoiceCountMenu, &portaMenu, &unisonMenu},
                         HorizontalMenu::Layout::FIXED};

// Modulator menu -----------------------------------------------------------------------

modulator::Transpose modulatorTransposeMenu{STRING_FOR_TRANSPOSE, STRING_FOR_FM_MOD_TRAN_MENU_TITLE,
                                            params::LOCAL_MODULATOR_0_PITCH_ADJUST};
source::patched_param::FM modulatorVolume{STRING_FOR_AMOUNT_LEVEL, STRING_FOR_FM_MOD_LEVEL_MENU_TITLE,
                                          params::LOCAL_MODULATOR_0_VOLUME};
source::patched_param::FM modulatorFeedbackMenu{STRING_FOR_FEEDBACK, STRING_FOR_FM_MOD_FBACK_MENU_TITLE,
                                                params::LOCAL_MODULATOR_0_FEEDBACK};
modulator::Destination modulatorDestMenu{STRING_FOR_DESTINATION, STRING_FOR_FM_MOD2_DEST_MENU_TITLE};
osc::RetriggerPhase modulatorPhaseMenu{STRING_FOR_RETRIGGER_PHASE, STRING_FOR_FM_MOD_RETRIG_MENU_TITLE, true};

// Envelope 1 menu ---------------------------------------------------------------------------------
envelope::Segment env1AttackMenu{STRING_FOR_ATTACK, STRING_FOR_ENV1_ATTACK_MENU_TITLE, params::LOCAL_ENV_0_ATTACK};
envelope::Segment env1DecayMenu{STRING_FOR_DECAY, STRING_FOR_ENV1_DECAY_MENU_TITLE, params::LOCAL_ENV_0_DECAY};
envelope::Segment env1SustainMenu{STRING_FOR_SUSTAIN, STRING_FOR_ENV1_SUSTAIN_MENU_TITLE, params::LOCAL_ENV_0_SUSTAIN};
envelope::Segment env1ReleaseMenu{STRING_FOR_RELEASE, STRING_FOR_ENV1_RELEASE_MENU_TITLE, params::LOCAL_ENV_0_RELEASE};

HorizontalMenu env1Menu{
    STRING_FOR_ENVELOPE_1, {&env1AttackMenu, &env1DecayMenu, &env1SustainMenu, &env1ReleaseMenu}, 0};

// Envelope 2 menu ---------------------------------------------------------------------------------
envelope::Segment env2AttackMenu{STRING_FOR_ATTACK, STRING_FOR_ENV2_ATTACK_MENU_TITLE, params::LOCAL_ENV_0_ATTACK};
envelope::Segment env2DecayMenu{STRING_FOR_DECAY, STRING_FOR_ENV2_DECAY_MENU_TITLE, params::LOCAL_ENV_0_DECAY};
envelope::Segment env2SustainMenu{STRING_FOR_SUSTAIN, STRING_FOR_ENV2_SUSTAIN_MENU_TITLE, params::LOCAL_ENV_0_SUSTAIN};
envelope::Segment env2ReleaseMenu{STRING_FOR_RELEASE, STRING_FOR_ENV2_RELEASE_MENU_TITLE, params::LOCAL_ENV_0_RELEASE};

HorizontalMenu env2Menu{
    STRING_FOR_ENVELOPE_2, {&env2AttackMenu, &env2DecayMenu, &env2SustainMenu, &env2ReleaseMenu}, 1};

// Envelope 3 menu ---------------------------------------------------------------------------------
envelope::Segment env3AttackMenu{STRING_FOR_ATTACK, STRING_FOR_ENV3_ATTACK_MENU_TITLE, params::LOCAL_ENV_0_ATTACK};
envelope::Segment env3DecayMenu{STRING_FOR_DECAY, STRING_FOR_ENV3_DECAY_MENU_TITLE, params::LOCAL_ENV_0_DECAY};
envelope::Segment env3SustainMenu{STRING_FOR_SUSTAIN, STRING_FOR_ENV3_SUSTAIN_MENU_TITLE, params::LOCAL_ENV_0_SUSTAIN};
envelope::Segment env3ReleaseMenu{STRING_FOR_RELEASE, STRING_FOR_ENV3_RELEASE_MENU_TITLE, params::LOCAL_ENV_0_RELEASE};

HorizontalMenu env3Menu{
    STRING_FOR_ENVELOPE_3, {&env3AttackMenu, &env3DecayMenu, &env3SustainMenu, &env3ReleaseMenu}, 2};

// Envelope 4 menu ---------------------------------------------------------------------------------
envelope::Segment env4AttackMenu{STRING_FOR_ATTACK, STRING_FOR_ENV4_ATTACK_MENU_TITLE, params::LOCAL_ENV_0_ATTACK};
envelope::Segment env4DecayMenu{STRING_FOR_DECAY, STRING_FOR_ENV4_DECAY_MENU_TITLE, params::LOCAL_ENV_0_DECAY};
envelope::Segment env4SustainMenu{STRING_FOR_SUSTAIN, STRING_FOR_ENV4_SUSTAIN_MENU_TITLE, params::LOCAL_ENV_0_SUSTAIN};
envelope::Segment env4ReleaseMenu{STRING_FOR_RELEASE, STRING_FOR_ENV4_RELEASE_MENU_TITLE, params::LOCAL_ENV_0_RELEASE};

HorizontalMenu env4Menu{
    STRING_FOR_ENVELOPE_4, {&env4AttackMenu, &env4DecayMenu, &env4SustainMenu, &env4ReleaseMenu}, 3};

// LFO1 menu ---------------------------------------------------------------------------------

lfo::Type lfo1TypeMenu{STRING_FOR_SHAPE, STRING_FOR_LFO1_TYPE, LFO1_ID};
lfo::Sync lfo1SyncMenu{STRING_FOR_SYNC, STRING_FOR_LFO1_SYNC, LFO1_ID};
patched_param::Integer lfo1RateMenu{STRING_FOR_RATE, STRING_FOR_LFO1_RATE, params::GLOBAL_LFO_FREQ_1};

HorizontalMenu lfo1Menu{STRING_FOR_LFO1, {&lfo1TypeMenu, &lfo1SyncMenu, &lfo1RateMenu}};

// LFO2 menu ---------------------------------------------------------------------------------
lfo::Type lfo2TypeMenu{STRING_FOR_SHAPE, STRING_FOR_LFO2_TYPE, LFO2_ID};
lfo::Sync lfo2SyncMenu{STRING_FOR_SYNC, STRING_FOR_LFO2_SYNC, LFO2_ID};
patched_param::Integer lfo2RateMenu{STRING_FOR_RATE, STRING_FOR_LFO2_RATE, params::LOCAL_LFO_LOCAL_FREQ_1};

HorizontalMenu lfo2Menu{STRING_FOR_LFO2, {&lfo2TypeMenu, &lfo2SyncMenu, &lfo2RateMenu}};

// LFO3 menu ---------------------------------------------------------------------------------

lfo::Type lfo3TypeMenu{STRING_FOR_SHAPE, STRING_FOR_LFO3_TYPE, LFO3_ID};
lfo::Sync lfo3SyncMenu{STRING_FOR_SYNC, STRING_FOR_LFO3_SYNC, LFO3_ID};
patched_param::Integer lfo3RateMenu{STRING_FOR_RATE, STRING_FOR_LFO3_RATE, params::GLOBAL_LFO_FREQ_2};

HorizontalMenu lfo3Menu{STRING_FOR_LFO3, {&lfo3TypeMenu, &lfo3SyncMenu, &lfo3RateMenu}};

// LFO4 menu ---------------------------------------------------------------------------------
lfo::Type lfo4TypeMenu{STRING_FOR_SHAPE, STRING_FOR_LFO4_TYPE, LFO4_ID};
lfo::Sync lfo4SyncMenu{STRING_FOR_SYNC, STRING_FOR_LFO4_SYNC, LFO4_ID};
patched_param::Integer lfo4RateMenu{STRING_FOR_RATE, STRING_FOR_LFO4_RATE, params::LOCAL_LFO_LOCAL_FREQ_2};

HorizontalMenu lfo4Menu{STRING_FOR_LFO4, {&lfo4TypeMenu, &lfo4SyncMenu, &lfo4RateMenu}};

// Mod FX ----------------------------------------------------------------------------------
mod_fx::Type modFXTypeMenu{STRING_FOR_TYPE, STRING_FOR_MODFX_TYPE};
mod_fx::Rate modFXRateMenu{STRING_FOR_RATE, STRING_FOR_MODFX_RATE, params::GLOBAL_MOD_FX_RATE};
mod_fx::Feedback modFXFeedbackMenu{STRING_FOR_FEEDBACK, STRING_FOR_MODFX_FEEDBACK, params::UNPATCHED_MOD_FX_FEEDBACK};
mod_fx::Depth_Patched modFXDepthMenu{STRING_FOR_DEPTH, STRING_FOR_MODFX_DEPTH, params::GLOBAL_MOD_FX_DEPTH};
mod_fx::Offset modFXOffsetMenu{STRING_FOR_OFFSET, STRING_FOR_MODFX_OFFSET, params::UNPATCHED_MOD_FX_OFFSET};

submenu::ModFxHorizontalMenu modFXMenu{
    STRING_FOR_MOD_FX,
    {
        &modFXTypeMenu,
        &modFXRateMenu,
        &modFXDepthMenu,
        &modFXFeedbackMenu,
        &modFXOffsetMenu,
    },
};

// EQ -------------------------------------------------------------------------------------
eq::EqUnpatchedParam bassMenu{STRING_FOR_BASS, params::UNPATCHED_BASS};
eq::EqUnpatchedParam trebleMenu{STRING_FOR_TREBLE, params::UNPATCHED_TREBLE};
eq::EqUnpatchedParam bassFreqMenu{STRING_FOR_BASS_FREQUENCY, STRING_FOR_BASS_FREQUENCY_SHORT,
                                  params::UNPATCHED_BASS_FREQ};
eq::EqUnpatchedParam trebleFreqMenu{STRING_FOR_TREBLE_FREQUENCY, STRING_FOR_TREBLE_FREQUENCY_SHORT,
                                    params::UNPATCHED_TREBLE_FREQ};

HorizontalMenu eqMenu{
    STRING_FOR_EQ,
    {
        &bassMenu,
        &trebleMenu,
        &bassFreqMenu,
        &trebleFreqMenu,
    },
};

// Delay ---------------------------------------------------------------------------------
patched_param::Integer delayFeedbackMenu{STRING_FOR_AMOUNT, STRING_FOR_DELAY_AMOUNT, params::GLOBAL_DELAY_FEEDBACK};
patched_param::Integer delayRateMenu{STRING_FOR_RATE, STRING_FOR_DELAY_RATE, params::GLOBAL_DELAY_RATE};
delay::PingPong delayPingPongMenu{STRING_FOR_PINGPONG, STRING_FOR_DELAY_PINGPONG};
delay::Analog delayAnalogMenu{STRING_FOR_TYPE, STRING_FOR_DELAY_TYPE};
delay::Sync delaySyncMenu{STRING_FOR_SYNC, STRING_FOR_DELAY_SYNC};

HorizontalMenu delayMenu{
    STRING_FOR_DELAY,
    {
        &delayPingPongMenu,
        &delayFeedbackMenu,
        &delaySyncMenu,
        &delayRateMenu,
        &delayAnalogMenu,
    },
};

// Stutter ----------------------------------------------------------------------------------
stutter::StutterDirection stutterDirectionMenu{STRING_FOR_DIRECTION, STRING_FOR_DIRECTION};
stutter::QuantizedStutter stutterQuantizedMenu{STRING_FOR_QUANTIZE, STRING_FOR_QUANTIZE};
stutter::Rate stutterRateMenu{STRING_FOR_RATE, STRING_FOR_STUTTER_RATE};

HorizontalMenu stutterMenu{STRING_FOR_STUTTER,
                           {&stutterRateMenu, &stutterDirectionMenu, &stutterQuantizedMenu},
                           HorizontalMenu::Layout::FIXED};

// Bend Ranges -------------------------------------------------------------------------------

bend_range::Main mainBendRangeMenu{STRING_FOR_NORMAL};
bend_range::PerFinger perFingerBendRangeMenu{STRING_FOR_POLY_FINGER_MPE};

submenu::Bend bendMenu{
    STRING_FOR_BEND_RANGE,
    {
        &mainBendRangeMenu,
        &perFingerBendRangeMenu,
    },
};

// Sidechain-----------------------------------------------------------------------

sidechain::Send sidechainSendMenu{STRING_FOR_SEND_TO_SIDECHAIN, STRING_FOR_SEND_TO_SIDECH_MENU_TITLE};
sidechain::VolumeShortcut sidechainVolumeShortcutMenu{STRING_FOR_VOLUME_DUCKING, params::GLOBAL_VOLUME_POST_REVERB_SEND,
                                                      PatchSource::SIDECHAIN};
reverb::sidechain::Volume reverbSidechainVolumeMenu{STRING_FOR_VOLUME_DUCKING};
sidechain::Sync sidechainSyncMenu{STRING_FOR_SYNC, STRING_FOR_SIDECHAIN_SYNC};
sidechain::Attack sidechainAttackMenu{STRING_FOR_ATTACK, STRING_FOR_SIDECH_ATTACK_MENU_TITLE};
sidechain::Release sidechainReleaseMenu{STRING_FOR_RELEASE, STRING_FOR_SIDECH_RELEASE_MENU_TITLE};
unpatched_param::UpdatingReverbParams sidechainShapeMenu{STRING_FOR_SHAPE, STRING_FOR_SIDECH_SHAPE_MENU_TITLE,
                                                         params::UNPATCHED_SIDECHAIN_SHAPE};
reverb::sidechain::Shape reverbSidechainShapeMenu{STRING_FOR_SHAPE, STRING_FOR_SIDECH_SHAPE_MENU_TITLE};

submenu::Sidechain sidechainMenu{STRING_FOR_SIDECHAIN,
                                 STRING_FOR_SIDECHAIN,
                                 {
                                     &sidechainSendMenu,
                                     &sidechainVolumeShortcutMenu,
                                     &sidechainSyncMenu,
                                     &sidechainAttackMenu,
                                     &sidechainReleaseMenu,
                                     &sidechainShapeMenu,
                                 }};

submenu::ReverbSidechain reverbSidechainMenu{STRING_FOR_REVERB_SIDECHAIN,
                                             STRING_FOR_REVERB_SIDECH_MENU_TITLE,
                                             {
                                                 &reverbSidechainVolumeMenu,
                                                 &sidechainSyncMenu,
                                                 &sidechainAttackMenu,
                                                 &sidechainReleaseMenu,
                                                 &reverbSidechainShapeMenu,
                                             }};

// Reverb ----------------------------------------------------------------------------------
patched_param::Integer reverbAmountMenu{STRING_FOR_AMOUNT, STRING_FOR_REVERB_AMOUNT, params::GLOBAL_REVERB_AMOUNT};
reverb::RoomSize reverbRoomSizeMenu{STRING_FOR_ROOM_SIZE};
reverb::Damping reverbDampingMenu{STRING_FOR_DAMPING};
reverb::Width reverbWidthMenu{STRING_FOR_WIDTH, STRING_FOR_REVERB_WIDTH};
reverb::Pan reverbPanMenu{STRING_FOR_PAN, STRING_FOR_REVERB_PAN};
reverb::Model reverbModelMenu{STRING_FOR_MODEL};
reverb::HPF reverbHPFMenu{STRING_FOR_HPF};
reverb::LPF reverbLPFMenu{STRING_FOR_LPF};

HorizontalMenu reverbMenu{
    STRING_FOR_REVERB,
    {
        &reverbModelMenu,
        &reverbRoomSizeMenu,
        &reverbDampingMenu,
        &reverbWidthMenu,
        &reverbAmountMenu,
        &reverbPanMenu,
        &reverbHPFMenu,
        &reverbLPFMenu,
        &reverbSidechainMenu,
    },
};

// FX ----------------------------------------------------------------------------------------

fx::Clipping clippingMenu{STRING_FOR_SATURATION};
UnpatchedParam srrMenu{STRING_FOR_DECIMATION, params::UNPATCHED_SAMPLE_RATE_REDUCTION};
UnpatchedParam bitcrushMenu{STRING_FOR_BITCRUSH, params::UNPATCHED_BITCRUSHING};
patched_param::Integer foldMenu{STRING_FOR_WAVEFOLD, STRING_FOR_WAVEFOLD, params::LOCAL_FOLD};

HorizontalMenu soundDistortionMenu{
    STRING_FOR_DISTORTION,
    {
        &clippingMenu,
        &bitcrushMenu,
        &srrMenu,
        &foldMenu,
    },
};

// Output MIDI for sound drums --------------------------------------------------------------
midi::sound::OutputMidiChannel outputMidiChannelMenu{STRING_FOR_CHANNEL, STRING_FOR_CHANNEL};
midi::sound::OutputMidiNoteForDrum outputMidiNoteForDrumMenu{STRING_FOR_NOTE, STRING_FOR_NOTE};
Submenu outputMidiSubmenu{STRING_FOR_MIDI, {&outputMidiChannelMenu, &outputMidiNoteForDrumMenu}};

// MIDIInstrument menu ----------------------------------------------------------------------
midi::device_definition::Linked midiDeviceLinkedMenu{STRING_FOR_MIDI_DEVICE_DEFINITION_LINKED,
                                                     STRING_FOR_MIDI_DEVICE_DEFINITION_LINKED};

midi::device_definition::DeviceDefinitionSubmenu midiDeviceDefinitionMenu{
    STRING_FOR_MIDI_DEVICE_DEFINITION,
    {
        &midiDeviceLinkedMenu,
    },
};

midi::Bank midiBankMenu{STRING_FOR_BANK, STRING_FOR_MIDI_BANK};
midi::Sub midiSubMenu{STRING_FOR_SUB_BANK_SHORT, STRING_FOR_MIDI_SUB_BANK};
midi::PGM midiPGMMenu{STRING_FOR_PGM, STRING_FOR_MIDI_PGM_NUMB_MENU_TITLE};
midi::MPEYToModWheel mpeyToModWheelMenu{STRING_FOR_Y_AXIS_CONVERSION, STRING_FOR_Y_AXIS_CONVERSION};
cv::DualCVSelection cv2SourceMenu{STRING_FOR_CV2_SOURCE};
midi::AftertouchToMono midiAftertouchCollapseMenu{STRING_FOR_PATCH_SOURCE_AFTERTOUCH,
                                                  STRING_FOR_PATCH_SOURCE_AFTERTOUCH};
midi::MPEToMono midiMPECollapseMenu{STRING_FOR_MPE, STRING_FOR_MPE};
submenu::PolyMonoConversion midiMPEMenu{STRING_FOR_MPE_MONO, {&midiAftertouchCollapseMenu, &midiMPECollapseMenu}};
// Clip-level stuff --------------------------------------------------------------------------

sequence::Direction sequenceDirectionMenu{STRING_FOR_PLAY_DIRECTION};
// Tempo ratio menu instances - using empty strings to rely on getName() methods
sequence::TempoRatioGlobal tempoRatioGlobalMenu{EMPTY_STRING};           // Will use getName() -> "Global"
sequence::TempoRatioHalf tempoRatioHalfMenu{EMPTY_STRING};               // Will use getName() -> "1/2 Half"
sequence::TempoRatioDouble tempoRatioDoubleMenu{EMPTY_STRING};           // Will use getName() -> "2/1 Double"
sequence::TempoRatioThreeFour tempoRatioThreeFourMenu{EMPTY_STRING};     // Will use getName() -> "3/4"
sequence::TempoRatioFourThree tempoRatioFourThreeMenu{EMPTY_STRING};     // Will use getName() -> "4/3"
sequence::TempoRatioNumerator tempoRatioNumeratorMenu{EMPTY_STRING};     // Will use getName() -> "Numerator"
sequence::TempoRatioDenominator tempoRatioDenominatorMenu{EMPTY_STRING}; // Will use getName() -> "Denominator"

// Custom ratio submenu
menu_item::Submenu tempoRatioCustomMenu{
    STRING_FOR_CUSTOM,
    {
        &tempoRatioNumeratorMenu,
        &tempoRatioDenominatorMenu,
    },
};

// Main tempo ratio submenu
sequence::TempoRatio sequenceTempoMenu{
    STRING_FOR_TEMPO,
    {
        &tempoRatioGlobalMenu,
        &tempoRatioHalfMenu,
        &tempoRatioDoubleMenu,
        &tempoRatioThreeFourMenu,
        &tempoRatioFourThreeMenu,
        &tempoRatioCustomMenu,
    },
};

// Global FX Menu

// Volume
UnpatchedParam globalLevelMenu{STRING_FOR_VOLUME_LEVEL, params::UNPATCHED_VOLUME};

// Pitch
UnpatchedParam globalPitchMenu{STRING_FOR_PITCH, params::UNPATCHED_PITCH_ADJUST};

// Pan
unpatched_param::Pan globalPanMenu{STRING_FOR_PAN, params::UNPATCHED_PAN};

Submenu songMasterMenu{
    STRING_FOR_MASTER,
    {
        &globalLevelMenu,
        &globalPanMenu,
    },
};

Submenu kitClipMasterMenu{
    STRING_FOR_MASTER,
    {
        &globalLevelMenu,
        &globalPitchMenu,
        &globalPanMenu,
    },
};

// LPF Menu
filter::UnpatchedFilterParam globalLPFFreqMenu{STRING_FOR_FREQUENCY, STRING_FOR_LPF_FREQUENCY,
                                               params::UNPATCHED_LPF_FREQ, filter::FilterSlot::LPF,
                                               filter::FilterParamType::FREQUENCY};
filter::UnpatchedFilterParam globalLPFResMenu{STRING_FOR_RESONANCE, STRING_FOR_LPF_RESONANCE, params::UNPATCHED_LPF_RES,
                                              filter::FilterSlot::LPF, filter::FilterParamType::RESONANCE};
filter::UnpatchedFilterParam globalLPFMorphMenu{STRING_FOR_MORPH, STRING_FOR_LPF_MORPH, params::UNPATCHED_LPF_MORPH,
                                                filter::FilterSlot::LPF, filter::FilterParamType::MORPH};
HorizontalMenu globalLPFMenu{STRING_FOR_LPF,
                             {
                                 &globalLPFFreqMenu,
                                 &globalLPFResMenu,
                                 &globalLPFMorphMenu,
                                 &lpfModeMenu,
                             },
                             HorizontalMenu::Layout::FIXED};

// HPF Menu
filter::UnpatchedFilterParam globalHPFFreqMenu{STRING_FOR_FREQUENCY, STRING_FOR_HPF_FREQUENCY,
                                               params::UNPATCHED_HPF_FREQ, filter::FilterSlot::HPF,
                                               filter::FilterParamType::FREQUENCY};
filter::UnpatchedFilterParam globalHPFResMenu{STRING_FOR_RESONANCE, STRING_FOR_HPF_RESONANCE, params::UNPATCHED_HPF_RES,
                                              filter::FilterSlot::HPF, filter::FilterParamType::RESONANCE};
filter::UnpatchedFilterParam globalHPFMorphMenu{STRING_FOR_MORPH, STRING_FOR_HPF_MORPH, params::UNPATCHED_HPF_MORPH,
                                                filter::FilterSlot::HPF, filter::FilterParamType::MORPH};

HorizontalMenu globalHPFMenu{STRING_FOR_HPF,
                             {
                                 &globalHPFFreqMenu,
                                 &globalHPFResMenu,
                                 &globalHPFMorphMenu,
                                 &hpfModeMenu,
                             },
                             HorizontalMenu::Layout::FIXED};

Submenu globalFiltersMenu{
    STRING_FOR_FILTERS,
    {
        &globalLPFMenu,
        &globalHPFMenu,
        &filterRoutingMenu,
    },
};

// EQ Menu

HorizontalMenu globalEQMenu{
    STRING_FOR_EQ,
    {
        &bassMenu,
        &trebleMenu,
        &bassFreqMenu,
        &trebleFreqMenu,
    },
};

// Delay Menu
UnpatchedParam globalDelayFeedbackMenu{STRING_FOR_AMOUNT, STRING_FOR_DELAY_AMOUNT, params::UNPATCHED_DELAY_AMOUNT};
UnpatchedParam globalDelayRateMenu{STRING_FOR_RATE, STRING_FOR_DELAY_RATE, params::UNPATCHED_DELAY_RATE};

HorizontalMenu globalDelayMenu{
    STRING_FOR_DELAY,
    {
        &delayPingPongMenu,
        &globalDelayFeedbackMenu,
        &delaySyncMenu,
        &globalDelayRateMenu,
        &delayAnalogMenu,
    },
};

// Reverb Menu

UnpatchedParam globalReverbSendAmountMenu{
    STRING_FOR_AMOUNT,
    STRING_FOR_REVERB_AMOUNT,
    params::UNPATCHED_REVERB_SEND_AMOUNT,
};

HorizontalMenu globalReverbMenu{
    STRING_FOR_REVERB,
    {
        &reverbModelMenu,
        &reverbRoomSizeMenu,
        &reverbDampingMenu,
        &reverbWidthMenu,
        &globalReverbSendAmountMenu,
        &reverbPanMenu,
        &reverbHPFMenu,
        &reverbLPFMenu,
        &reverbSidechainMenu,
    },
};

// Mod FX Menu

mod_fx::Depth_Unpatched globalModFXDepthMenu{STRING_FOR_DEPTH, STRING_FOR_MOD_FX_DEPTH, params::UNPATCHED_MOD_FX_DEPTH};
mod_fx::Rate_Unpatched globalModFXRateMenu{STRING_FOR_RATE, STRING_FOR_MOD_FX_RATE, params::UNPATCHED_MOD_FX_RATE};

submenu::ModFxHorizontalMenu globalModFXMenu{
    STRING_FOR_MOD_FX,
    {
        &modFXTypeMenu,
        &globalModFXRateMenu,
        &globalModFXDepthMenu,
        &modFXFeedbackMenu,
        &modFXOffsetMenu,
    },
};

HorizontalMenu globalDistortionMenu{
    STRING_FOR_DISTORTION,
    {
        &srrMenu,
        &bitcrushMenu,
    },
};

Submenu globalFXMenu{
    STRING_FOR_FX,
    {
        &globalEQMenu,
        &globalDelayMenu,
        &globalReverbMenu,
        &stutterMenu,
        &globalModFXMenu,
        &globalDistortionMenu,
    },
};

// Sidechain menu
unpatched_param::UpdatingReverbParams globalSidechainVolumeMenu{STRING_FOR_VOLUME_DUCKING,
                                                                params::UNPATCHED_SIDECHAIN_VOLUME};

Submenu globalSidechainMenu{
    STRING_FOR_SIDECHAIN,
    {
        &globalSidechainVolumeMenu,
        &sidechainSyncMenu,
        &sidechainAttackMenu,
        &sidechainReleaseMenu,
        &sidechainShapeMenu,
    },
};

// AudioClip stuff ---------------------------------------------------------------------------

audio_clip::SetClipLengthEqualToSampleLength setClipLengthMenu{STRING_FOR_SET_CLIP_LENGTH_EQUAL_TO_SAMPLE_LENGTH};

Submenu audioClipActionsMenu{
    STRING_FOR_ACTIONS,
    {
        &setClipLengthMenu,
    },
};

audio_clip::AudioSourceSelector audioSourceSelectorMenu{STRING_FOR_AUDIO_SOURCE};
audio_clip::SpecificSourceOutputSelector specificOutputSelectorMenu{STRING_FOR_TRACK};
audio_clip::Transpose audioClipTransposeMenu{STRING_FOR_TRANSPOSE};

Submenu audioClipMasterMenu{
    STRING_FOR_MASTER,
    {
        &globalLevelMenu,
        &audioClipTransposeMenu,
        &globalPanMenu,
    },
};

HorizontalMenu audioClipDistortionMenu{
    STRING_FOR_DISTORTION,
    {
        &clippingMenu,
        &bitcrushMenu,
        &srrMenu,
    },
};

Submenu audioClipFXMenu{
    STRING_FOR_FX,
    {
        &eqMenu,
        &globalDelayMenu,
        &globalReverbMenu,
        &stutterMenu,
        &globalModFXMenu,
        &audioClipDistortionMenu,
    },
};

// Sample Menu
audio_clip::Reverse audioClipReverseMenu{STRING_FOR_REVERSE};
audio_clip::SampleMarkerEditor audioClipSampleMarkerEditorMenuStart{EMPTY_STRING, MarkerType::START};
audio_clip::SampleMarkerEditor audioClipSampleMarkerEditorMenuEnd{STRING_FOR_WAVEFORM, MarkerType::END};
AudioInterpolation audioClipInterpolationMenu{STRING_FOR_INTERPOLATION, STRING_FOR_AUDIO_INTERPOLATION};

Submenu audioClipSampleMenu{
    STRING_FOR_SAMPLE,
    {
        &fileSelectorMenu,
        &audioClipReverseMenu,
        &samplePitchSpeedMenu,
        &audioClipSampleMarkerEditorMenuEnd,
        &audioClipInterpolationMenu,
    },
};

audio_clip::Attack audioClipAttackMenu{STRING_FOR_ATTACK};

menu_item::EditName nameEditMenu{STRING_FOR_NAME};

PLACE_SDRAM_DATA const MenuItem* midiOrCVParamShortcuts[kDisplayHeight] = {
    &arpRateMenuMIDIOrCV,
    &arpSyncMenu,
    &arpGateMenuMIDIOrCV,
    &arpOctavesMenu,
    &arpPresetModeMenu,
    &nameEditMenu,
    nullptr,
    nullptr,
};

PLACE_SDRAM_DATA const MenuItem* gateDrumParamShortcuts[8] = {
    &arpRateMenuMIDIOrCV,
    &arpSyncMenu,
    &arpGateMenuMIDIOrCV,
    &arpRhythmMenuMIDIOrCV,
    &arpModeMenu,
    nullptr,
    nullptr,
    nullptr,
};

// Gate stuff
gate::Mode gateModeMenu;
gate::OffTime gateOffTimeMenu{EMPTY_STRING, STRING_FOR_MINIMUM_OFF_TIME};

// Root menu

// CV Menu
cv::Volts cvVoltsMenu{STRING_FOR_VOLTS_PER_OCTAVE, STRING_FOR_CV_V_PER_OCTAVE_MENU_TITLE};
cv::Transpose cvTransposeMenu{STRING_FOR_TRANSPOSE, STRING_FOR_CV_TRANSPOSE_MENU_TITLE};

cv::Submenu cvSubmenu{STRING_FOR_CV_OUTPUT_N, {&cvVoltsMenu, &cvTransposeMenu}};

cv::Selection cvSelectionMenu{STRING_FOR_CV, STRING_FOR_CV_OUTPUTS};
gate::Selection gateSelectionMenu{STRING_FOR_GATE, STRING_FOR_GATE_OUTPUTS};

swing::Interval swingIntervalMenu{STRING_FOR_SWING_INTERVAL};

// Pads menu
shortcuts::Version shortcutsVersionMenu{STRING_FOR_SHORTCUTS_VERSION, STRING_FOR_SHORTCUTS_VER_MENU_TITLE};
menu_item::keyboard::Layout keyboardLayoutMenu{STRING_FOR_KEYBOARD_FOR_TEXT, STRING_FOR_KEY_LAYOUT};

// Colours submenu
Submenu coloursSubmenu{
    STRING_FOR_COLOURS,
    {
        &activeColourMenu,
        &stoppedColourMenu,
        &mutedColourMenu,
        &soloColourMenu,
        &fillColourMenu,
        &onceColourMenu,
    },
};

Submenu padsSubmenu{
    STRING_FOR_PADS,
    {
        &shortcutsVersionMenu,
        &keyboardLayoutMenu,
        &coloursSubmenu,
    },
};

// Record submenu
record::Quantize recordQuantizeMenu{STRING_FOR_QUANTIZATION};
ToggleBool recordMarginsMenu{STRING_FOR_LOOP_MARGINS, STRING_FOR_LOOP_MARGINS, FlashStorage::audioClipRecordMargins};
record::CountIn recordCountInMenu{STRING_FOR_COUNT_IN, STRING_FOR_REC_COUNT_IN};
monitor::Mode monitorModeMenu{STRING_FOR_SAMPLING_MONITORING, STRING_FOR_MONITORING};

record::ThresholdMode defaultThresholdRecordingModeMenu{STRING_FOR_MODE, record::ThresholdMode::DEFAULT};

Submenu defaultThresholdRecordingSubmenu{
    STRING_FOR_THRESHOLD_RECORDING,
    {
        &defaultThresholdRecordingModeMenu,
    },
};

record::LoopCommand defaultLoopCommandMenu{STRING_FOR_LOOP_COMMAND, STRING_FOR_LOOP_COMMAND};

Submenu recordSubmenu{
    STRING_FOR_RECORDING,
    {
        &recordCountInMenu,
        &recordQuantizeMenu,
        &recordMarginsMenu,
        &monitorModeMenu,
        &defaultThresholdRecordingSubmenu,
        &defaultLoopCommandMenu,
    },
};

sample::browser_preview::Mode sampleBrowserPreviewModeMenu{STRING_FOR_SAMPLE_PREVIEW};

flash::Status flashStatusMenu{STRING_FOR_PLAY_CURSOR};

firmware::Version firmwareVersionMenu{STRING_FOR_FIRMWARE_VERSION, STRING_FOR_FIRMWARE_VER_MENU_TITLE};

runtime_feature::Settings runtimeFeatureSettingsMenu{STRING_FOR_COMMUNITY_FTS, STRING_FOR_COMMUNITY_FTS_MENU_TITLE};

// CV menu

// MIDI
// MIDI thru
ToggleBool midiThruMenu{STRING_FOR_MIDI_THRU, STRING_FOR_MIDI_THRU, midiEngine.midiThru};

// MIDI Takeover
midi::Takeover midiTakeoverMenu{STRING_FOR_TAKEOVER};

// MIDI Follow
midi::FollowChannel midiFollowChannelAMenu{STRING_FOR_FOLLOW_CHANNEL_A, STRING_FOR_FOLLOW_CHANNEL_A,
                                           MIDIFollowChannelType::A};
midi::FollowChannel midiFollowChannelBMenu{STRING_FOR_FOLLOW_CHANNEL_B, STRING_FOR_FOLLOW_CHANNEL_B,
                                           MIDIFollowChannelType::B};
midi::FollowChannel midiFollowChannelCMenu{STRING_FOR_FOLLOW_CHANNEL_C, STRING_FOR_FOLLOW_CHANNEL_C,
                                           MIDIFollowChannelType::C};
midi::FollowKitRootNote midiFollowKitRootNoteMenu{STRING_FOR_FOLLOW_KIT_ROOT_NOTE};
ToggleBool midiFollowDisplayParamMenu{STRING_FOR_FOLLOW_DISPLAY_PARAM, STRING_FOR_FOLLOW_DISPLAY_PARAM,
                                      midiEngine.midiFollowDisplayParam};
midi::FollowFeedbackChannelType midiFollowFeedbackChannelMenu{STRING_FOR_CHANNEL};
midi::FollowFeedbackAutomation midiFollowFeedbackAutomationMenu{STRING_FOR_FOLLOW_FEEDBACK_AUTOMATION};
ToggleBool midiFollowFeedbackFilterMenu{STRING_FOR_FOLLOW_FEEDBACK_FILTER, STRING_FOR_FOLLOW_FEEDBACK_FILTER,
                                        midiEngine.midiFollowFeedbackFilter};

Submenu midiFollowChannelSubmenu{
    STRING_FOR_CHANNEL,
    STRING_FOR_CHANNEL,
    {
        &midiFollowChannelAMenu,
        &midiFollowChannelBMenu,
        &midiFollowChannelCMenu,
    },
};

Submenu midiFollowFeedbackSubmenu{
    STRING_FOR_FOLLOW_FEEDBACK,
    STRING_FOR_FOLLOW_FEEDBACK,
    {
        &midiFollowFeedbackChannelMenu,
        &midiFollowFeedbackAutomationMenu,
        &midiFollowFeedbackFilterMenu,
    },
};

Submenu midiFollowSubmenu{
    STRING_FOR_FOLLOW_TITLE,
    STRING_FOR_FOLLOW_TITLE,
    {
        &midiFollowChannelSubmenu,
        &midiFollowKitRootNoteMenu,
        &midiFollowFeedbackSubmenu,
        &midiFollowDisplayParamMenu,
    },
};

// MIDI select kit row
ToggleBool midiSelectKitRowMenu{STRING_FOR_SELECT_KIT_ROW, STRING_FOR_SELECT_KIT_ROW, midiEngine.midiSelectKitRow};

// MIDI transpose menu

midi::Transpose midiTransposeMenu{STRING_FOR_TRANSPOSE};

Submenu midiTransposeSubmenu{
    STRING_FOR_TRANSPOSE,
    STRING_FOR_TRANSPOSE,
    {
        &midiTransposeMenu,
    },
};

// MIDI commands submenu
midi::Command playbackRestartMidiCommand{STRING_FOR_RESTART, GlobalMIDICommand::PLAYBACK_RESTART};
midi::Command playMidiCommand{STRING_FOR_PLAY, GlobalMIDICommand::PLAY};
midi::Command recordMidiCommand{STRING_FOR_RECORD, GlobalMIDICommand::RECORD};
midi::Command tapMidiCommand{STRING_FOR_TAP_TEMPO, GlobalMIDICommand::TAP};
midi::Command undoMidiCommand{STRING_FOR_UNDO, GlobalMIDICommand::UNDO};
midi::Command redoMidiCommand{STRING_FOR_REDO, GlobalMIDICommand::REDO};
midi::Command loopMidiCommand{STRING_FOR_LOOP, GlobalMIDICommand::LOOP};
midi::Command loopContinuousLayeringMidiCommand{STRING_FOR_LAYERING_LOOP, GlobalMIDICommand::LOOP_CONTINUOUS_LAYERING};
midi::Command fillMidiCommand{STRING_FOR_FILL, GlobalMIDICommand::FILL};
midi::Command transposeMidiCommand{STRING_FOR_TRANSPOSE, GlobalMIDICommand::TRANSPOSE};
midi::Command nextSongMidiCommand{STRING_FOR_SONG_LOAD_NEXT, GlobalMIDICommand::NEXT_SONG};

Submenu midiCommandsMenu{
    STRING_FOR_COMMANDS,
    STRING_FOR_MIDI_COMMANDS,
    {&playMidiCommand, &playbackRestartMidiCommand, &recordMidiCommand, &tapMidiCommand, &undoMidiCommand,
     &redoMidiCommand, &loopMidiCommand, &loopContinuousLayeringMidiCommand, &fillMidiCommand, &transposeMidiCommand,
     &nextSongMidiCommand},
};

// MIDI device submenu - for after we've selected which device we want it for

midi::DefaultVelocityToLevel defaultVelocityToLevelMenu{STRING_FOR_VELOCITY};
midi::SendClock sendClockMenu{STRING_FOR_CLOCK};
midi::Device midiDeviceMenu{
    EMPTY_STRING,
    {
        &mpe::directionSelectorMenu,
        &defaultVelocityToLevelMenu,
        &sendClockMenu,
    },
};

// MIDI input differentiation menu
ToggleBool midiInputDifferentiationMenu{STRING_FOR_DIFFERENTIATE_INPUTS, STRING_FOR_DIFFERENTIATE_INPUTS,
                                        MIDIDeviceManager::differentiatingInputsByDevice};

// MIDI clock menu
ToggleBool midiClockOutStatusMenu{STRING_FOR_OUTPUT, STRING_FOR_MIDI_CLOCK_OUT, playbackHandler.midiOutClockEnabled};
ToggleBool midiClockInStatusMenu{STRING_FOR_INPUT, STRING_FOR_MIDI_CLOCK_IN, playbackHandler.midiInClockEnabled};
ToggleBool tempoMagnitudeMatchingMenu{STRING_FOR_TEMPO_MAGNITUDE_MATCHING, STRING_FOR_TEMPO_MAGNITUDE_MATCHING,
                                      playbackHandler.tempoMagnitudeMatchingEnabled};

// Midi devices menu
midi::Devices midi::devicesMenu{STRING_FOR_DEVICES, STRING_FOR_MIDI_DEVICES};
mpe::DirectionSelector mpe::directionSelectorMenu{STRING_FOR_MPE};

// MIDI menu
Submenu midiClockMenu{
    STRING_FOR_CLOCK,
    STRING_FOR_MIDI_CLOCK,
    {
        &midiClockInStatusMenu,
        &midiClockOutStatusMenu,
        &tempoMagnitudeMatchingMenu,
    },
};
Submenu midiMenu{
    STRING_FOR_MIDI,
    {
        &midiClockMenu,
        &midiFollowSubmenu,
        &midiSelectKitRowMenu,
        &midiThruMenu,
        &midiTransposeMenu,
        &midiTakeoverMenu,
        &midiCommandsMenu,
        &midiInputDifferentiationMenu,
        &midi::devicesMenu,
    },
};

// Clock menu
// Trigger clock in menu
trigger::in::PPQN triggerInPPQNMenu{STRING_FOR_PPQN, STRING_FOR_INPUT_PPQN};
ToggleBool triggerInAutoStartMenu{STRING_FOR_AUTO_START, STRING_FOR_AUTO_START,
                                  playbackHandler.analogClockInputAutoStart};
Submenu triggerClockInMenu{
    STRING_FOR_INPUT,
    STRING_FOR_T_CLOCK_INPUT_MENU_TITLE,
    {
        &triggerInPPQNMenu,
        &triggerInAutoStartMenu,
    },
};

// Trigger clock out menu
trigger::out::PPQN triggerOutPPQNMenu{STRING_FOR_PPQN, STRING_FOR_OUTPUT_PPQN};
Submenu triggerClockOutMenu{
    STRING_FOR_OUTPUT,
    STRING_FOR_T_CLOCK_OUT_MENU_TITLE,
    {
        &triggerOutPPQNMenu,
    },
};

// Trigger clock menu
Submenu triggerClockMenu{
    STRING_FOR_TRIGGER_CLOCK,
    {
        &triggerClockInMenu,
        &triggerClockOutMenu,
    },
};

// Defaults menu
defaults::KeyboardLayout defaultKeyboardLayoutMenu{STRING_FOR_DEFAULT_UI_LAYOUT, STRING_FOR_DEFAULT_UI_LAYOUT};

defaults::DefaultFavouritesLayout defaultFavouritesLayout{STRING_FOR_DEFAULT_UI_FAVOURITES,
                                                          STRING_FOR_DEFAULT_UI_FAVOURITES};

InvertedToggleBool defaultUIKeyboardFunctionsVelocityGlide{STRING_FOR_DEFAULT_UI_KB_CONTROLS_VELOCITY_MOMENTARY,
                                                           STRING_FOR_DEFAULT_UI_KB_CONTROLS_VELOCITY_MOMENTARY,
                                                           // This control is inverted, as the default value is true
                                                           // (Enabled) Glide mode is the opposite to Momentary mode
                                                           FlashStorage::keyboardFunctionsVelocityGlide};
InvertedToggleBool defaultUIKeyboardFunctionsModwheelGlide{STRING_FOR_DEFAULT_UI_KB_CONTROLS_MODWHEEL_MOMENTARY,
                                                           STRING_FOR_DEFAULT_UI_KB_CONTROLS_MODWHEEL_MOMENTARY,
                                                           // This control is inverted, as the default value is true
                                                           // (Enabled) Glide mode is the opposite to Momentary mode
                                                           FlashStorage::keyboardFunctionsModwheelGlide};
Submenu defaultKeyboardFunctionsMenu{
    STRING_FOR_DEFAULT_UI_KB_CONTROLS,
    {&defaultUIKeyboardFunctionsVelocityGlide, &defaultUIKeyboardFunctionsModwheelGlide},
};

Submenu defaultUIKeyboard{
    STRING_FOR_DEFAULT_UI_KEYBOARD,
    {&defaultKeyboardLayoutMenu, &defaultKeyboardFunctionsMenu, &defaultFavouritesLayout},
};

ToggleBool defaultgridEmptyPadsUnarm{STRING_FOR_DEFAULT_UI_DEFAULT_GRID_EMPTY_PADS_UNARM,
                                     STRING_FOR_DEFAULT_UI_DEFAULT_GRID_EMPTY_PADS_UNARM,
                                     FlashStorage::gridEmptyPadsUnarm};
ToggleBool defaultGridEmptyPadsCreateRec{STRING_FOR_DEFAULT_UI_DEFAULT_GRID_EMPTY_PADS_CREATE_REC,
                                         STRING_FOR_DEFAULT_UI_DEFAULT_GRID_EMPTY_PADS_CREATE_REC,
                                         FlashStorage::gridEmptyPadsCreateRec};
Submenu defaultEmptyPadMenu{
    STRING_FOR_DEFAULT_UI_DEFAULT_GRID_EMPTY_PADS,
    {&defaultgridEmptyPadsUnarm, &defaultGridEmptyPadsCreateRec},
};

defaults::DefaultGridDefaultActiveMode defaultGridDefaultActiveMode{STRING_FOR_DEFAULT_UI_DEFAULT_GRID_ACTIVE_MODE,
                                                                    STRING_FOR_DEFAULT_UI_DEFAULT_GRID_ACTIVE_MODE};
ToggleBool defaultGridAllowGreenSelection{STRING_FOR_DEFAULT_UI_DEFAULT_GRID_ALLOW_GREEN_SELECTION,
                                          STRING_FOR_DEFAULT_UI_DEFAULT_GRID_ALLOW_GREEN_SELECTION,
                                          FlashStorage::gridAllowGreenSelection};
Submenu defaultSessionGridMenu{
    STRING_FOR_DEFAULT_UI_GRID,
    {&defaultGridDefaultActiveMode, &defaultGridAllowGreenSelection, &defaultEmptyPadMenu},
};

defaults::SessionLayout defaultSessionLayoutMenu{STRING_FOR_DEFAULT_UI_LAYOUT, STRING_FOR_DEFAULT_UI_LAYOUT};
Submenu defaultUISession{
    STRING_FOR_DEFAULT_UI_SONG,
    {&defaultSessionLayoutMenu, &defaultSessionGridMenu},
};

ToggleBool defaultAccessibilityShortcuts{STRING_FOR_DEFAULT_ACCESSIBILITY_SHORTCUTS,
                                         STRING_FOR_DEFAULT_ACCESSIBILITY_SHORTCUTS,
                                         FlashStorage::accessibilityShortcuts};
ToggleBool defaultAccessibilityMenuHighlighting{STRING_FOR_DEFAULT_ACCESSIBILITY_MENU_HIGHLIGHTING,
                                                STRING_FOR_DEFAULT_ACCESSIBILITY_MENU_HIGHLIGHTING,
                                                FlashStorage::accessibilityMenuHighlighting};

Submenu defaultAccessibilityMenu{STRING_FOR_DEFAULT_ACCESSIBILITY,
                                 {
                                     &defaultAccessibilityShortcuts,
                                     &defaultAccessibilityMenuHighlighting,
                                 }};

defaults::ui::clip_type::DefaultNewClipType defaultNewClipTypeMenu{STRING_FOR_DEFAULT_NEW_CLIP_TYPE,
                                                                   STRING_FOR_DEFAULT_NEW_CLIP_TYPE};
ToggleBool defaultUseLastClipTypeMenu{STRING_FOR_DEFAULT_USE_LAST_CLIP_TYPE, STRING_FOR_DEFAULT_USE_LAST_CLIP_TYPE,
                                      FlashStorage::defaultUseLastClipType};

Submenu defaultClipTypeMenu{STRING_FOR_DEFAULT_CLIP_TYPE,
                            {
                                &defaultNewClipTypeMenu,
                                &defaultUseLastClipTypeMenu,
                            }};

Submenu defaultUI{
    STRING_FOR_DEFAULT_UI,
    {&defaultAccessibilityMenu, &defaultUISession, &defaultUIKeyboard, &defaultClipTypeMenu},
};

ToggleBool defaultAutomationInterpolateMenu{STRING_FOR_DEFAULT_AUTOMATION_INTERPOLATION,
                                            STRING_FOR_DEFAULT_AUTOMATION_INTERPOLATION,
                                            FlashStorage::automationInterpolate};

ToggleBool defaultAutomationClearMenu{STRING_FOR_DEFAULT_AUTOMATION_CLEAR, STRING_FOR_DEFAULT_AUTOMATION_CLEAR,
                                      FlashStorage::automationClear};

ToggleBool defaultAutomationShiftMenu{STRING_FOR_DEFAULT_AUTOMATION_SHIFT, STRING_FOR_DEFAULT_AUTOMATION_SHIFT,
                                      FlashStorage::automationShift};

ToggleBool defaultAutomationNudgeNoteMenu{STRING_FOR_DEFAULT_AUTOMATION_NUDGE_NOTE,
                                          STRING_FOR_DEFAULT_AUTOMATION_NUDGE_NOTE, FlashStorage::automationNudgeNote};

ToggleBool defaultAutomationDisableAuditionPadShortcutsMenu{
    STRING_FOR_DEFAULT_AUTOMATION_DISABLE_AUDITION_PAD_SHORTCUTS,
    STRING_FOR_DEFAULT_AUTOMATION_DISABLE_AUDITION_PAD_SHORTCUTS, FlashStorage::automationDisableAuditionPadShortcuts};

Submenu defaultAutomationMenu{
    STRING_FOR_AUTOMATION,
    {
        &defaultAutomationInterpolateMenu,
        &defaultAutomationClearMenu,
        &defaultAutomationShiftMenu,
        &defaultAutomationNudgeNoteMenu,
        &defaultAutomationDisableAuditionPadShortcutsMenu,
    },
};

IntegerRange defaultTempoMenu{STRING_FOR_TEMPO, STRING_FOR_DEFAULT_TEMPO, 60, 240};
IntegerRange defaultSwingAmountMenu{STRING_FOR_SWING_AMOUNT, STRING_FOR_DEFAULT_SWING, 1, 99};
defaults::SwingInterval defaultSwingIntervalMenu{STRING_FOR_SWING_INTERVAL, STRING_FOR_DEFAULT_SWING};
KeyRange defaultKeyMenu{STRING_FOR_KEY, STRING_FOR_DEFAULT_KEY};
defaults::DefaultScale defaultScaleMenu{STRING_FOR_INIT_SCALE};
defaults::Velocity defaultVelocityMenu{STRING_FOR_VELOCITY, STRING_FOR_DEFAULT_VELOC_MENU_TITLE};
defaults::Magnitude defaultMagnitudeMenu{STRING_FOR_RESOLUTION, STRING_FOR_DEFAULT_RESOL_MENU_TITLE};
defaults::BendRange defaultBendRangeMenu{STRING_FOR_BEND_RANGE, STRING_FOR_DEFAULT_BEND_R};
defaults::MetronomeVolume defaultMetronomeVolumeMenu{STRING_FOR_METRONOME, STRING_FOR_DEFAULT_METRO_MENU_TITLE};
defaults::StartupSongModeMenu defaultStartupSongMenu{STRING_FOR_DEFAULT_UI_DEFAULT_STARTUP_SONG_MODE,
                                                     STRING_FOR_DEFAULT_UI_DEFAULT_STARTUP_SONG_MODE};
defaults::PadBrightness defaultPadBrightness{STRING_FOR_DEFAULT_PAD_BRIGHTNESS,
                                             STRING_FOR_DEFAULT_PAD_BRIGHTNESS_MENU_TITLE};
defaults::SliceMode defaultSliceMode{STRING_FOR_DEFAULT_SLICE_MODE, STRING_FOR_DEFAULT_SLICE_MODE_MENU_TITLE};
ToggleBool defaultHighCPUUsageIndicatorMode{STRING_FOR_DEFAULT_HIGH_CPU_USAGE_INDICATOR,
                                            STRING_FOR_DEFAULT_HIGH_CPU_USAGE_INDICATOR,
                                            FlashStorage::highCPUUsageIndicator};
defaults::HoldTime defaultHoldTimeMenu{STRING_FOR_HOLD_TIME, STRING_FOR_HOLD_TIME};

ActiveScaleMenu defaultActiveScaleMenu{STRING_FOR_ACTIVE_SCALES, ActiveScaleMenu::DEFAULT};

Submenu defaultScalesSubmenu{STRING_FOR_SCALE,
                             {
                                 &defaultScaleMenu,
                                 &defaultActiveScaleMenu,
                             }};

Submenu defaultsSubmenu{
    STRING_FOR_DEFAULTS,
    {
        &defaultUI,
        &defaultAutomationMenu,
        &defaultTempoMenu,
        &defaultSwingAmountMenu,
        &defaultSwingIntervalMenu,
        &defaultKeyMenu,
        &defaultScalesSubmenu,
        &defaultVelocityMenu,
        &defaultMagnitudeMenu,
        &defaultBendRangeMenu,
        &defaultMetronomeVolumeMenu,
        &defaultStartupSongMenu,
        &defaultPadBrightness,
        &defaultSliceMode,
        &defaultHighCPUUsageIndicatorMode,
        &defaultHoldTimeMenu,
    },
};

// Sound editor menu -----------------------------------------------------------------------------

// FM only
std::array<MenuItem*, 5> modulatorMenuItems = {
    &modulatorVolume, &modulatorTransposeMenu, &modulatorFeedbackMenu, &modulatorDestMenu, &modulatorPhaseMenu,
};

submenu::Modulator modulator0Menu{STRING_FOR_FM_MODULATOR_1, modulatorMenuItems, 0};
submenu::Modulator modulator1Menu{STRING_FOR_FM_MODULATOR_2, modulatorMenuItems, 1};

std::array<MenuItem*, 3> dxMenuItems = {
    &dxBrowseMenu,
    &dxGlobalParams,
    &dxEngineSelect,
};
menu_item::Submenu dxMenu{STRING_FOR_DX_1, dxMenuItems};

// Not FM
patched_param::IntegerNonFM noiseMenu{STRING_FOR_NOISE_LEVEL, params::LOCAL_NOISE_VOLUME};

MasterTranspose masterTransposeMenu{STRING_FOR_MASTER_TRANSPOSE, STRING_FOR_MASTER_TRAN_MENU_TITLE};

patch_cable_strength::Fixed vibratoMenu{STRING_FOR_VIBRATO, params::LOCAL_PITCH_ADJUST, PatchSource::LFO_GLOBAL_1};

// Synth only
menu_item::SynthModeSelection synthModeMenu{STRING_FOR_SYNTH_MODE};
bend_range::PerFinger drumBendRangeMenu{STRING_FOR_BEND_RANGE}; // The single option available for Drums
patched_param::Integer volumeMenu{STRING_FOR_VOLUME_LEVEL, STRING_FOR_MASTER_LEVEL, params::GLOBAL_VOLUME_POST_FX};
patched_param::Pan panMenu{STRING_FOR_PAN, params::LOCAL_PAN};

PatchCables patchCablesMenu{STRING_FOR_MOD_MATRIX};

Submenu soundMasterMenu{
    STRING_FOR_MASTER,
    {
        &volumeMenu,
        &masterTransposeMenu,
        &vibratoMenu,
        &panMenu,
        &synthModeMenu,
        &nameEditMenu,
    },
};

Submenu soundFXMenu{
    STRING_FOR_FX,
    {
        &eqMenu,
        &delayMenu,
        &reverbMenu,
        &stutterMenu,
        &modFXMenu,
        &soundDistortionMenu,
        &noiseMenu,
    },
};

menu_item::Submenu soundEditorRootMenu{
    STRING_FOR_SOUND,
    {
        &soundMasterMenu,   &arpMenu,           &globalRandomizerMenu,
        &audioCompMenu,     &soundFiltersMenu,  &soundFXMenu,
        &sidechainMenu,     &source0Menu,       &source1Menu,
        &modulator0Menu,    &modulator1Menu,    &env1Menu,
        &env2Menu,          &env3Menu,          &env4Menu,
        &lfo1Menu,          &lfo2Menu,          &lfo3Menu,
        &lfo4Menu,          &voiceMenu,         &bendMenu,
        &drumBendRangeMenu, &patchCablesMenu,   &sequenceDirectionMenu,
        &sequenceTempoMenu, &outputMidiSubmenu,
    },
};

menu_item::note::IteranceDivisor noteCustomIteranceDivisor{STRING_FOR_ITERANCE_DIVISOR};
menu_item::note::IteranceStepToggle noteCustomIteranceStep1{STRING_FOR_ITERATION_STEP_1, STRING_FOR_ITERATION_STEP_1,
                                                            0};
menu_item::note::IteranceStepToggle noteCustomIteranceStep2{STRING_FOR_ITERATION_STEP_2, STRING_FOR_ITERATION_STEP_2,
                                                            1};
menu_item::note::IteranceStepToggle noteCustomIteranceStep3{STRING_FOR_ITERATION_STEP_3, STRING_FOR_ITERATION_STEP_3,
                                                            2};
menu_item::note::IteranceStepToggle noteCustomIteranceStep4{STRING_FOR_ITERATION_STEP_4, STRING_FOR_ITERATION_STEP_4,
                                                            3};
menu_item::note::IteranceStepToggle noteCustomIteranceStep5{STRING_FOR_ITERATION_STEP_5, STRING_FOR_ITERATION_STEP_5,
                                                            4};
menu_item::note::IteranceStepToggle noteCustomIteranceStep6{STRING_FOR_ITERATION_STEP_6, STRING_FOR_ITERATION_STEP_6,
                                                            5};
menu_item::note::IteranceStepToggle noteCustomIteranceStep7{STRING_FOR_ITERATION_STEP_7, STRING_FOR_ITERATION_STEP_7,
                                                            6};
menu_item::note::IteranceStepToggle noteCustomIteranceStep8{STRING_FOR_ITERATION_STEP_8, STRING_FOR_ITERATION_STEP_8,
                                                            7};

// Root menu for note custom iterance
menu_item::Submenu noteCustomIteranceRootMenu{
    STRING_FOR_CUSTOM,
    {
        &noteCustomIteranceDivisor,
        &noteCustomIteranceStep1,
        &noteCustomIteranceStep2,
        &noteCustomIteranceStep3,
        &noteCustomIteranceStep4,
        &noteCustomIteranceStep5,
        &noteCustomIteranceStep6,
        &noteCustomIteranceStep7,
        &noteCustomIteranceStep8,
    },
};

menu_item::note::Velocity noteVelocityMenu{STRING_FOR_NOTE_EDITOR_VELOCITY};
menu_item::note::Probability noteProbabilityMenu{STRING_FOR_NOTE_EDITOR_PROBABILITY};
menu_item::note::IterancePreset noteIteranceMenu{STRING_FOR_NOTE_EDITOR_ITERANCE};
menu_item::note::Fill noteFillMenu{STRING_FOR_NOTE_EDITOR_FILL};

// Root menu for Note Editor
menu_item::Submenu noteEditorRootMenu{
    STRING_FOR_NOTE_EDITOR,
    {
        &noteVelocityMenu,
        &noteProbabilityMenu,
        &noteIteranceMenu,
        &noteFillMenu,
    },
};

menu_item::note_row::IteranceDivisor noteRowCustomIteranceDivisor{STRING_FOR_ITERANCE_DIVISOR};
menu_item::note_row::IteranceStepToggle noteRowCustomIteranceStep1{STRING_FOR_ITERATION_STEP_1,
                                                                   STRING_FOR_ITERATION_STEP_1, 0};
menu_item::note_row::IteranceStepToggle noteRowCustomIteranceStep2{STRING_FOR_ITERATION_STEP_2,
                                                                   STRING_FOR_ITERATION_STEP_2, 1};
menu_item::note_row::IteranceStepToggle noteRowCustomIteranceStep3{STRING_FOR_ITERATION_STEP_3,
                                                                   STRING_FOR_ITERATION_STEP_3, 2};
menu_item::note_row::IteranceStepToggle noteRowCustomIteranceStep4{STRING_FOR_ITERATION_STEP_4,
                                                                   STRING_FOR_ITERATION_STEP_4, 3};
menu_item::note_row::IteranceStepToggle noteRowCustomIteranceStep5{STRING_FOR_ITERATION_STEP_5,
                                                                   STRING_FOR_ITERATION_STEP_5, 4};
menu_item::note_row::IteranceStepToggle noteRowCustomIteranceStep6{STRING_FOR_ITERATION_STEP_6,
                                                                   STRING_FOR_ITERATION_STEP_6, 5};
menu_item::note_row::IteranceStepToggle noteRowCustomIteranceStep7{STRING_FOR_ITERATION_STEP_7,
                                                                   STRING_FOR_ITERATION_STEP_7, 6};
menu_item::note_row::IteranceStepToggle noteRowCustomIteranceStep8{STRING_FOR_ITERATION_STEP_8,
                                                                   STRING_FOR_ITERATION_STEP_8, 7};

// Root menu for note row custom iterance
menu_item::Submenu noteRowCustomIteranceRootMenu{
    STRING_FOR_CUSTOM,
    {
        &noteRowCustomIteranceDivisor,
        &noteRowCustomIteranceStep1,
        &noteRowCustomIteranceStep2,
        &noteRowCustomIteranceStep3,
        &noteRowCustomIteranceStep4,
        &noteRowCustomIteranceStep5,
        &noteRowCustomIteranceStep6,
        &noteRowCustomIteranceStep7,
        &noteRowCustomIteranceStep8,
    },
};

menu_item::note_row::Probability noteRowProbabilityMenu{STRING_FOR_NOTE_ROW_EDITOR_PROBABILITY};
menu_item::note_row::IterancePreset noteRowIteranceMenu{STRING_FOR_NOTE_ROW_EDITOR_ITERANCE};
menu_item::note_row::Fill noteRowFillMenu{STRING_FOR_NOTE_ROW_EDITOR_FILL};

// Root menu for Note Row Editor
menu_item::Submenu noteRowEditorRootMenu{
    STRING_FOR_NOTE_ROW_EDITOR,
    {
        &noteRowProbabilityMenu,
        &noteRowIteranceMenu,
        &noteRowFillMenu,
        &sequenceDirectionMenu,
    },
};

menu_item::midi::ProgramSubMenu midiProgramMenu{STRING_FOR_MIDI_PROGRAM_MENU_TITLE,
                                                {
                                                    &midiBankMenu,
                                                    &midiSubMenu,
                                                    &midiPGMMenu,
                                                },
                                                HorizontalMenu::Layout::FIXED,
                                                2};

// Root menu for MIDI / CV
menu_item::Submenu soundEditorRootMenuMIDIOrCV{
    STRING_FOR_MIDI_INST_MENU_TITLE,
    {
        &midiDeviceDefinitionMenu,
        &midiProgramMenu,
        &arpMenuMIDIOrCV,
        &globalRandomizerMenu,
        &bendMenu,
        &cv2SourceMenu,
        &mpeyToModWheelMenu,
        &midiMPEMenu,
        &sequenceDirectionMenu,
        &sequenceTempoMenu,
    },
};

// Root menu for NonAudioDrums (MIDI and Gate drums)
menu_item::Submenu soundEditorRootMenuMidiDrum{
    STRING_FOR_MIDI,
    {
        &arpMenuMIDIOrCV,
        &globalRandomizerMenu,
    },
};
menu_item::Submenu soundEditorRootMenuGateDrum{
    STRING_FOR_GATE,
    {
        &arpMenuMIDIOrCV,
        &globalRandomizerMenu,
    },
};

// Root menu for AudioClips
menu_item::Submenu soundEditorRootMenuAudioClip{
    STRING_FOR_AUDIO_CLIP,
    {
        &audioClipActionsMenu,
        &audioSourceSelectorMenu,
        &specificOutputSelectorMenu,
        &audioClipMasterMenu,
        &audioCompMenu,
        &globalFiltersMenu,
        &audioClipFXMenu,
        &globalSidechainMenu,
        &audioClipSampleMenu,
        &audioClipAttackMenu,
        &priorityMenu,
        &sequenceDirectionMenu,
        &sequenceTempoMenu,
    },
};

// Menu for Performance View Editing Mode
menu_item::performance_session_view::EditingMode performEditorMenu{STRING_FOR_PERFORM_EDITOR};

// Root menu for Performance View
menu_item::Submenu soundEditorRootMenuPerformanceView{
    STRING_FOR_PERFORM_FX,
    {
        &performEditorMenu,
        &globalFiltersMenu,
        &globalFXMenu,
    },
};

// Sub menu for Stem Export
menu_item::stem_export::Start startStemExportMenu{STRING_FOR_START_EXPORT};

ToggleBool configureNormalizationMenu{STRING_FOR_CONFIGURE_EXPORT_STEMS_NORMALIZATION,
                                      STRING_FOR_CONFIGURE_EXPORT_STEMS_NORMALIZATION, stemExport.allowNormalization};
ToggleBool configureSilenceMenu{STRING_FOR_CONFIGURE_EXPORT_STEMS_SILENCE, STRING_FOR_CONFIGURE_EXPORT_STEMS_SILENCE,
                                stemExport.exportToSilence};
ToggleBool configureSongFXMenu{STRING_FOR_CONFIGURE_EXPORT_STEMS_SONGFX, STRING_FOR_CONFIGURE_EXPORT_STEMS_SONGFX,
                               stemExport.includeSongFX};
ToggleBool configureOfflineRenderingMenu{STRING_FOR_CONFIGURE_EXPORT_STEMS_OFFLINE_RENDERING,
                                         STRING_FOR_CONFIGURE_EXPORT_STEMS_OFFLINE_RENDERING, stemExport.renderOffline};
ToggleBool configureMixdownMenu{STRING_FOR_CONFIGURE_EXPORT_STEMS_MIXDOWN, STRING_FOR_CONFIGURE_EXPORT_STEMS_MIXDOWN,
                                stemExport.exportMixdown};
menu_item::Submenu configureStemExportMenu{STRING_FOR_CONFIGURE_EXPORT_STEMS,
                                           {
                                               &configureNormalizationMenu,
                                               &configureSilenceMenu,
                                               &configureSongFXMenu,
                                               &configureOfflineRenderingMenu,
                                               &configureMixdownMenu,
                                           }};

menu_item::Submenu stemExportMenu{
    STRING_FOR_EXPORT_AUDIO,
    {
        &startStemExportMenu,
        &configureStemExportMenu,
    },
};

ActiveScaleMenu activeScaleMenu{STRING_FOR_ACTIVE_SCALES, ActiveScaleMenu::SONG};
record::ThresholdMode songThresholdRecordingModeMenu{STRING_FOR_MODE, record::ThresholdMode::SONG};

Submenu songThresholdRecordingSubmenu{
    STRING_FOR_THRESHOLD_RECORDING,
    {
        &songThresholdRecordingModeMenu,
    },
};

song::ConfigureMacros configureSongMacrosMenu{STRING_FOR_CONFIGURE_SONG_MACROS};
song::MidiLearn midiLearnMenu{STRING_FOR_MIDI_LEARN};

// Root menu for Song View
menu_item::Submenu soundEditorRootMenuSongView{
    STRING_FOR_SONG,
    {
        &songMasterMenu,
        &globalFiltersMenu,
        &globalFXMenu,
        &swingIntervalMenu,
        &activeScaleMenu,
        &songThresholdRecordingSubmenu,
        &configureSongMacrosMenu,
        &midiLearnMenu,
        &stemExportMenu,
    },
};

// Root menu for Kit Global FX
menu_item::Submenu soundEditorRootMenuKitGlobalFX{
    STRING_FOR_KIT_GLOBAL_FX,
    {
        &kitClipMasterMenu,
        &kitArpMenu,
        &audioCompMenu,
        &globalFiltersMenu,
        &globalFXMenu,
        &globalSidechainMenu,
    },
};

// Root Menu
Submenu settingsRootMenu{
    STRING_FOR_SETTINGS,
    {
        &cvSelectionMenu,
        &gateSelectionMenu,
        &triggerClockMenu,
        &midiMenu,
        &defaultsSubmenu,
        &padsSubmenu,
        &sampleBrowserPreviewModeMenu,
        &flashStatusMenu,
        &recordSubmenu,
        &runtimeFeatureSettingsMenu,
        &firmwareVersionMenu,
    },
};

#define comingSoonMenu reinterpret_cast<MenuItem*>(0xFFFFFFFF)

// clang-format off
PLACE_SDRAM_DATA MenuItem* paramShortcutsForSounds[][kDisplayHeight] = {
  // Post V3
    {&sampleRepeatMenu,       &sampleReverseMenu,      &timeStretchMenu,               &samplePitchSpeedMenu,          &audioRecorderMenu,   &fileSelectorMenu,      &interpolationMenu,       &sampleStartMenu                   },
    {&sampleRepeatMenu,       &sampleReverseMenu,      &timeStretchMenu,               &samplePitchSpeedMenu,          &audioRecorderMenu,   &fileSelectorMenu,      &interpolationMenu,       &sampleStartMenu                   },
    {&sourceVolumeMenu,       &sourceTransposeMenu,    &oscTypeMenu,                   &pulseWidthMenu,                &oscPhaseMenu,        &sourceFeedbackMenu,    &sourceWaveIndexMenu,     &noiseMenu                         },
    {&sourceVolumeMenu,       &sourceTransposeMenu,    &oscTypeMenu,                   &pulseWidthMenu,                &oscPhaseMenu,        &sourceFeedbackMenu,    &sourceWaveIndexMenu,     &oscSyncMenu                       },
    {&modulatorVolume,        &modulatorTransposeMenu, comingSoonMenu,                 comingSoonMenu,                 &modulatorPhaseMenu,  &modulatorFeedbackMenu, comingSoonMenu,           &sequenceDirectionMenu             },
    {&modulatorVolume,        &modulatorTransposeMenu, comingSoonMenu,                 comingSoonMenu,                 &modulatorPhaseMenu,  &modulatorFeedbackMenu, &modulatorDestMenu,       &stutterRateMenu                   },
    {&volumeMenu,             &masterTransposeMenu,    &vibratoMenu,                   &panMenu,                       &synthModeMenu,       &srrMenu,               &bitcrushMenu,            &clippingMenu                      },
    {&portaMenu,              &polyphonyMenu,          &priorityMenu,                  &unisonDetuneMenu,              &numUnisonMenu,       &threshold,             nullptr,                  &foldMenu                          },
    {&env1ReleaseMenu,        &env1SustainMenu,        &env1DecayMenu,                 &env1AttackMenu,                &lpfMorphMenu,        &lpfModeMenu,           &lpfResMenu,              &lpfFreqMenu                       },
    {&env2ReleaseMenu,        &env2SustainMenu,        &env2DecayMenu,                 &env2AttackMenu,                &hpfMorphMenu,        &hpfModeMenu,           &hpfResMenu,              &hpfFreqMenu                       },
    {&sidechainReleaseMenu,   &sidechainSyncMenu,      &sidechainVolumeShortcutMenu,   &sidechainAttackMenu,           &sidechainShapeMenu,  &sidechainSendMenu,     &bassMenu,                &bassFreqMenu                      },
    {&arpRateMenu,            &arpSyncMenu,            &arpGateMenu,                   &arpOctavesMenu,                &arpPresetModeMenu,   &nameEditMenu,          &trebleMenu,              &trebleFreqMenu                    },
    {&lfo1RateMenu,           &lfo1SyncMenu,           &lfo1TypeMenu,                  &modFXTypeMenu,                 &modFXOffsetMenu,     &modFXFeedbackMenu,     &modFXDepthMenu,          &modFXRateMenu                     },
    {&lfo2RateMenu,           &lfo2SyncMenu,           &lfo2TypeMenu,                  &reverbAmountMenu,              &reverbPanMenu,       &reverbWidthMenu,       &reverbDampingMenu,       &reverbRoomSizeMenu                },
    {&delayRateMenu,          &delaySyncMenu,          &delayAnalogMenu,               &delayFeedbackMenu,             &delayPingPongMenu,   nullptr,                nullptr,                  nullptr                            },
    {nullptr,          	      &arpSpreadVelocityMenu,  nullptr,                        &arpNoteProbabilityMenu,        nullptr,              nullptr,                nullptr,                  nullptr                            },
};

PLACE_SDRAM_DATA Submenu* parentsForSoundShortcuts[][kDisplayHeight] = {
    {nullptr,                 nullptr,                 nullptr,                        nullptr,                        nullptr,              nullptr,                nullptr,                  nullptr,                           },
    {nullptr,                 nullptr,                 nullptr,                        nullptr,                        nullptr,              nullptr,                nullptr,                  nullptr,                           },
    {nullptr,                 nullptr,                 nullptr,                        nullptr,                        nullptr,              nullptr,                nullptr,                  nullptr,                           },
    {nullptr,                 nullptr,                 nullptr,                        nullptr,                        nullptr,              nullptr,                nullptr,                  nullptr,                           },
    {nullptr,                 nullptr,                 nullptr,                        nullptr,                        nullptr,              nullptr,                nullptr,                  nullptr,                           },
    {nullptr,                 nullptr,                 nullptr,                        nullptr,                        nullptr,              nullptr,                nullptr,                  &stutterMenu                       },
    {nullptr,                 nullptr,                 nullptr,                        nullptr,                        nullptr,              &soundDistortionMenu,   &soundDistortionMenu,     &soundDistortionMenu,              },
    {&voiceMenu,              &voiceMenu,              &voiceMenu,                     &unisonMenu,                    &unisonMenu,          &audioCompMenu,         nullptr,                  &soundDistortionMenu,              },
    {&env1Menu,               &env1Menu,               &env1Menu,                      &env1Menu,                      &lpfMenu,             &lpfMenu,               &lpfMenu,                 &lpfMenu,                          },
    {&env2Menu,               &env2Menu,               &env2Menu,                      &env2Menu,                      &hpfMenu,             &hpfMenu,               &hpfMenu,                 &hpfMenu,                          },
    {nullptr,                 nullptr,                 nullptr,                        nullptr,                        nullptr,              nullptr,                &eqMenu,                  &eqMenu,                           },
    {&arpBasicMenu,           &arpBasicMenu,           &arpBasicMenu,                  &arpPatternMenu,                &arpPresetAndRandomizerMenu, nullptr,         &eqMenu,                  &eqMenu,                           },
    {&lfo1Menu,               &lfo1Menu,               &lfo1Menu,                      &modFXMenu,                     &modFXMenu,           &modFXMenu,             &modFXMenu,               &modFXMenu,                        },
    {&lfo2Menu,               &lfo2Menu,               &lfo2Menu,                      &reverbMenu,              	   &reverbMenu,    	 	 &reverbMenu,      		 &reverbMenu,        	   &reverbMenu,                 	  },
    {&delayMenu,              &delayMenu,              &delayMenu,                     &delayMenu,                     &delayMenu,           nullptr,                nullptr,                  nullptr,                           },
    {nullptr,                 &globalRandomizerMenu,   nullptr,                        &globalRandomizerMenu,          nullptr,              nullptr,                nullptr,                  nullptr,                           },
};

PLACE_SDRAM_DATA MenuItem* paramShortcutsForSoundsSecondLayer[][kDisplayHeight] = {
    {nullptr,	        nullptr,	            nullptr,	        nullptr,	        nullptr,	                nullptr,	nullptr,	nullptr              },
    {nullptr,	        nullptr,	            nullptr,	        nullptr,	        nullptr,	                nullptr,	nullptr,	nullptr              },
    {nullptr,	        nullptr,	            nullptr,	        nullptr,	        nullptr,	                nullptr,	nullptr,	nullptr              },
    {nullptr,	        nullptr,	            nullptr,	        nullptr,	        nullptr,	                nullptr,	nullptr,	nullptr              },
    {nullptr,	        nullptr,	            nullptr,	        nullptr,	        nullptr,	                nullptr,	nullptr,	nullptr              },
    {nullptr,	        nullptr,	            nullptr,	        nullptr,	        nullptr,	                nullptr,	nullptr,	&stutterDirectionMenu},
    {nullptr,	        nullptr,	            nullptr,	        nullptr,	        nullptr,	                nullptr,	nullptr,	nullptr              },
    {nullptr,	        &voice::polyphonicVoiceCountMenu, nullptr,  nullptr,	        &unison::stereoSpreadMenu,  &compRatio,	nullptr,	nullptr              },
    {&env3ReleaseMenu,	&env3SustainMenu,	    &env3DecayMenu,	    &env3AttackMenu,	nullptr,	                nullptr,	nullptr,	nullptr              },
    {&env4ReleaseMenu,	&env4SustainMenu,	    &env4DecayMenu,	    &env4AttackMenu,	nullptr,	                nullptr,	nullptr,	nullptr              },
    {nullptr,	        nullptr,	            nullptr,	        nullptr,	        nullptr,	                nullptr,	nullptr,	nullptr              },
    {nullptr,	        nullptr,	            nullptr,	        nullptr,	        &arpRandomizerLockMenu,	    nullptr,	nullptr,	nullptr              },
    {&lfo3RateMenu,	    &lfo3SyncMenu,	        &lfo3TypeMenu,	    nullptr,	        nullptr,	                nullptr,	nullptr,	nullptr              },
    {&lfo4RateMenu,	    &lfo4SyncMenu,	        &lfo4TypeMenu,	    nullptr,	        nullptr,	                nullptr,	nullptr,	nullptr              },
    {nullptr,	        nullptr,	            nullptr,	        nullptr,	        nullptr,                    nullptr,	nullptr,	nullptr              },
    {nullptr,	        nullptr,	            nullptr,	        nullptr,	        nullptr,                    nullptr,	nullptr,	nullptr              },
};

PLACE_SDRAM_DATA Submenu* parentsForSoundShortcutsSecondLayer[][kDisplayHeight] = {
    {nullptr,	nullptr,	nullptr,	nullptr,	nullptr,	            nullptr,	    nullptr,	nullptr     },
    {nullptr,	nullptr,	nullptr,	nullptr,	nullptr,	            nullptr,	    nullptr,	nullptr     },
    {nullptr,	nullptr,	nullptr,	nullptr,	nullptr,	            nullptr,	    nullptr,	nullptr     },
    {nullptr,	nullptr,	nullptr,	nullptr,	nullptr,	            nullptr,	    nullptr,	nullptr     },
    {nullptr,	nullptr,	nullptr,	nullptr,	nullptr,	            nullptr,	    nullptr,	nullptr     },
    {nullptr,	nullptr,	nullptr,	nullptr,	nullptr,	            nullptr,	    nullptr,	&stutterMenu},
    {nullptr,	nullptr,	nullptr,	nullptr,	nullptr,	            nullptr,	    nullptr,	nullptr     },
    {nullptr,	&voiceMenu,	nullptr,	nullptr,	&unisonMenu,            &audioCompMenu,	nullptr,	nullptr     },
    {&env3Menu,	&env3Menu,	&env3Menu,	&env3Menu,	nullptr,	            nullptr,	    nullptr,	nullptr     },
    {&env4Menu,	&env4Menu,	&env4Menu,	&env4Menu,	nullptr,	            nullptr,	    nullptr,	nullptr     },
    {nullptr,	nullptr,	nullptr,	nullptr,	nullptr,	            nullptr,	    nullptr,	nullptr     },
    {nullptr,	nullptr,	nullptr,	nullptr,	&arpRandomizerMenu,	    nullptr,	    nullptr,	nullptr     },
    {&lfo3Menu,	&lfo3Menu,	&lfo3Menu,	nullptr,	nullptr,	            nullptr,	    nullptr,	nullptr     },
    {&lfo4Menu,	&lfo4Menu,	&lfo4Menu,	nullptr,	nullptr,	            nullptr,	    nullptr,	nullptr     },
    {nullptr,	nullptr,	nullptr,	nullptr,	nullptr,	            nullptr,	    nullptr,	nullptr     },
    {nullptr,	nullptr,	nullptr,	nullptr,	nullptr,	            nullptr,	    nullptr,	nullptr     },
};

PLACE_SDRAM_DATA MenuItem* paramShortcutsForAudioClips[kDisplayWidth][kDisplayHeight] = {
    {nullptr,                 &audioClipReverseMenu,   nullptr,                    &samplePitchSpeedMenu,       nullptr,              &fileSelectorMenu,  &audioClipInterpolationMenu,&audioClipSampleMarkerEditorMenuEnd},
    {nullptr,                 &audioClipReverseMenu,   nullptr,                    &samplePitchSpeedMenu,       nullptr,              &fileSelectorMenu,  &audioClipInterpolationMenu,&audioClipSampleMarkerEditorMenuEnd},
    {nullptr,                 nullptr,                 nullptr,                    nullptr,                     nullptr,              nullptr,            nullptr,                  nullptr                            },
    {nullptr,                 nullptr,                 nullptr,                    nullptr,                     nullptr,              nullptr,            nullptr,                  nullptr                            },
    {nullptr,                 nullptr,                 nullptr,                    nullptr,                     nullptr,              nullptr,            nullptr,                  nullptr                            },
    {nullptr,                 nullptr,                 nullptr,                    nullptr,                     nullptr,              nullptr,            nullptr,                  &stutterRateMenu                   },
    {&globalLevelMenu,        &audioClipTransposeMenu, nullptr,                    &globalPanMenu,              nullptr,              &srrMenu,           &bitcrushMenu,            &clippingMenu                      },
    {nullptr,                 nullptr,                 &priorityMenu,              nullptr,                     nullptr,              &threshold,         nullptr,                  comingSoonMenu                     },
    {nullptr,                 nullptr,                 nullptr,                    &audioClipAttackMenu,        &globalLPFMorphMenu,  &lpfModeMenu,       &globalLPFResMenu,        &globalLPFFreqMenu                 },
    {nullptr,                 nullptr,                 nullptr,                    &audioClipAttackMenu,        &globalHPFMorphMenu,  &hpfModeMenu,       &globalHPFResMenu,        &globalHPFFreqMenu                 },
    {&sidechainReleaseMenu,   &sidechainSyncMenu,      &globalSidechainVolumeMenu, &sidechainAttackMenu,        &sidechainShapeMenu,  nullptr,            &bassMenu,                &bassFreqMenu                      },
    {nullptr,                 nullptr,                 nullptr,                    nullptr,                     nullptr,              &nameEditMenu,      &trebleMenu,              &trebleFreqMenu                    },
    {nullptr,                 nullptr,                 nullptr,                    &modFXTypeMenu,              &modFXOffsetMenu,     &modFXFeedbackMenu, &globalModFXDepthMenu,    &globalModFXRateMenu               },
    {nullptr,                 nullptr,                 nullptr,                    &globalReverbSendAmountMenu, &reverbPanMenu,       &reverbWidthMenu,   &reverbDampingMenu,       &reverbRoomSizeMenu                },
    {&globalDelayRateMenu,    &delaySyncMenu,          &delayAnalogMenu,           &globalDelayFeedbackMenu,    &delayPingPongMenu,   nullptr,            nullptr,                  nullptr                            },
    {nullptr,                 nullptr,                 nullptr,                    nullptr,                     nullptr,              nullptr,            nullptr,                  nullptr                            },
};
PLACE_SDRAM_DATA Submenu* parentsForAudioShortcuts[][kDisplayHeight] = {
    {nullptr,                 nullptr,                 nullptr,                        nullptr,                        nullptr,              	nullptr,                 nullptr,                  nullptr,                        },
    {nullptr,                 nullptr,                 nullptr,                        nullptr,                        nullptr,              	nullptr,                 nullptr,                  nullptr,                        },
    {nullptr,                 nullptr,                 nullptr,                        nullptr,                        nullptr,              	nullptr,                 nullptr,                  nullptr,                        },
    {nullptr,                 nullptr,                 nullptr,                        nullptr,                        nullptr,              	nullptr,                 nullptr,                  nullptr,                        },
    {nullptr,                 nullptr,                 nullptr,                        nullptr,                        nullptr,              	nullptr,                 nullptr,                  nullptr,                        },
    {nullptr,                 nullptr,                 nullptr,                        nullptr,                        nullptr,              	nullptr,                 nullptr,                  &stutterMenu,                   },
    {nullptr,                 nullptr,                 nullptr,                        nullptr,                        nullptr,					&audioClipDistortionMenu,&audioClipDistortionMenu, &audioClipDistortionMenu,       },
    {nullptr,                 nullptr,                 nullptr,                        nullptr,                        nullptr,              	&audioCompMenu,          nullptr,                  nullptr,                        },
    {nullptr,                 nullptr,                 nullptr,                        nullptr,                        &globalLPFMenu,       	&globalLPFMenu,          &globalLPFMenu,           &globalLPFMenu,                 },
    {nullptr,                 nullptr,                 nullptr,                        nullptr,                        &globalHPFMenu,       	&globalHPFMenu,          &globalHPFMenu,           &globalHPFMenu,                 },
    {nullptr,                 nullptr,                 nullptr,                        nullptr,                        nullptr,              	nullptr,                 &eqMenu,                  &eqMenu,                        },
    {nullptr,                 nullptr,                 nullptr,                        nullptr,                        nullptr,              	nullptr,                 &eqMenu,                  &eqMenu,                        },
    {nullptr,                 nullptr,                 nullptr,                        &globalModFXMenu,               &globalModFXMenu,        &globalModFXMenu,        &globalModFXMenu,         &globalModFXMenu,               },
    {nullptr,                 nullptr,                 nullptr,                        &globalReverbMenu,              &globalReverbMenu,       &globalReverbMenu,       &globalReverbMenu,        &globalReverbMenu,              },
    {&globalDelayMenu,        &globalDelayMenu,        &globalDelayMenu,               &globalDelayMenu,               &globalDelayMenu,        nullptr,                 nullptr,                  nullptr,                        },
    {nullptr,                 nullptr,                 nullptr,                        nullptr,                        nullptr,              	nullptr,                 nullptr,                  nullptr,                        },
};

PLACE_SDRAM_DATA MenuItem* paramShortcutsForSongView[][kDisplayHeight] = {
    {nullptr,                 nullptr,                 nullptr,                        nullptr,                        nullptr,              nullptr,                nullptr,                  nullptr                            },
    {nullptr,                 nullptr,                 nullptr,                        nullptr,                        nullptr,              nullptr,                nullptr,                  nullptr                            },
    {nullptr,                 nullptr,                 nullptr,                        nullptr,                        nullptr,              nullptr,                nullptr,                  nullptr                            },
    {nullptr,                 nullptr,                 nullptr,                        nullptr,                        nullptr,              nullptr,                nullptr,                  nullptr                            },
    {nullptr,                 nullptr,                 nullptr,                        nullptr,                        nullptr,              nullptr,                nullptr,                  nullptr                            },
    {nullptr,                 nullptr,                 nullptr,                        nullptr,                        nullptr,              nullptr,                nullptr,                  &stutterRateMenu                   },
    {&globalLevelMenu,        nullptr,                 nullptr,                        &globalPanMenu,                 nullptr,              &srrMenu,               &bitcrushMenu,            nullptr                            },
    {nullptr,                 nullptr,                 nullptr,                        nullptr,                        nullptr,              &threshold,             nullptr,                  nullptr                            },
    {nullptr,                 nullptr,                 nullptr,                        nullptr,                        &globalLPFMorphMenu,  &lpfModeMenu,           &globalLPFResMenu,        &globalLPFFreqMenu                 },
    {nullptr,                 nullptr,                 nullptr,                        nullptr,                        &globalHPFMorphMenu,  &hpfModeMenu,           &globalHPFResMenu,        &globalHPFFreqMenu                 },
    {nullptr,                 nullptr,                 nullptr,                        nullptr,                        nullptr,              nullptr,                &bassMenu,                &bassFreqMenu                      },
    {nullptr,                 nullptr,                 nullptr,                        nullptr,                        nullptr,              nullptr,                &trebleMenu,              &trebleFreqMenu                    },
    {nullptr,                 nullptr,                 nullptr,                        &modFXTypeMenu,                 &modFXOffsetMenu,     &modFXFeedbackMenu,     &globalModFXDepthMenu,    &globalModFXRateMenu               },
    {nullptr,                 nullptr,                 nullptr,                        &globalReverbSendAmountMenu,    &reverbPanMenu,       &reverbWidthMenu,       &reverbDampingMenu,       &reverbRoomSizeMenu                },
    {&globalDelayRateMenu,    &delaySyncMenu,          &delayAnalogMenu,               &globalDelayFeedbackMenu,       &delayPingPongMenu,   nullptr,                nullptr,                  nullptr                            },
    {nullptr,          	      nullptr,                 nullptr,                        nullptr,                        nullptr,              nullptr,                nullptr,                  nullptr                            },
};
PLACE_SDRAM_DATA Submenu* parentsForSongShortcuts[][kDisplayHeight] = {
    {nullptr,                 nullptr,                 nullptr,                        nullptr,                        nullptr,              nullptr,                nullptr,                  nullptr,                           },
    {nullptr,                 nullptr,                 nullptr,                        nullptr,                        nullptr,              nullptr,                nullptr,                  nullptr,                           },
    {nullptr,                 nullptr,                 nullptr,                        nullptr,                        nullptr,              nullptr,                nullptr,                  nullptr,                           },
    {nullptr,                 nullptr,                 nullptr,                        nullptr,                        nullptr,              nullptr,                nullptr,                  nullptr,                           },
    {nullptr,                 nullptr,                 nullptr,                        nullptr,                        nullptr,              nullptr,                nullptr,                  nullptr,                           },
    {nullptr,                 nullptr,                 nullptr,                        nullptr,                        nullptr,              nullptr,                nullptr,                  &stutterMenu,                      },
    {nullptr,                 nullptr,                 nullptr,                        nullptr,                        nullptr,              &globalDistortionMenu,  &globalDistortionMenu,    &globalDistortionMenu,             },
    {nullptr,                 nullptr,                 nullptr,                        nullptr,                        nullptr,              &audioCompMenu,         nullptr,                  nullptr,                           },
    {nullptr,                 nullptr,                 nullptr,                        nullptr,                        &globalLPFMenu,       &globalLPFMenu,         &globalLPFMenu,           &globalLPFMenu,                    },
    {nullptr,                 nullptr,                 nullptr,                        nullptr,                        &globalHPFMenu,       &globalHPFMenu,         &globalHPFMenu,           &globalHPFMenu,                    },
    {nullptr,                 nullptr,                 nullptr,                        nullptr,                        nullptr,              nullptr,                &globalEQMenu,            &globalEQMenu,                     },
    {nullptr,                 nullptr,                 nullptr,                        nullptr,                        nullptr,              nullptr,                &globalEQMenu,            &globalEQMenu,                     },
    {nullptr,                 nullptr,                 nullptr,                        &globalModFXMenu,               &globalModFXMenu,     &globalModFXMenu,       &globalModFXMenu,         &globalModFXMenu,                  },
    {nullptr,                 nullptr,                 nullptr,                        &globalReverbMenu,              &globalReverbMenu,    &globalReverbMenu,      &globalReverbMenu,        &globalReverbMenu,                 },
    {&globalDelayMenu,        &globalDelayMenu,        &globalDelayMenu,               &globalDelayMenu,               &globalDelayMenu,     nullptr,                nullptr,                  nullptr,                           },
    {nullptr,                 nullptr,                 nullptr,                        nullptr,                        nullptr,              nullptr,                nullptr,                  nullptr,                           },
};

PLACE_SDRAM_DATA MenuItem* paramShortcutsForKitGlobalFX[][kDisplayHeight] = {
    {nullptr,                 nullptr,                 nullptr,                        nullptr,                        nullptr,                     nullptr,                nullptr,                  nullptr                            },
    {nullptr,                 nullptr,                 nullptr,                        nullptr,                        nullptr,                     nullptr,                nullptr,                  nullptr                            },
    {nullptr,                 nullptr,                 nullptr,                        nullptr,                        nullptr,                     nullptr,                nullptr,                  nullptr                            },
    {nullptr,                 nullptr,                 nullptr,                        nullptr,                        nullptr,                     nullptr,                nullptr,                  nullptr                            },
    {nullptr,                 nullptr,                 nullptr,                        nullptr,                        nullptr,                     nullptr,                nullptr,                  nullptr                            },
    {nullptr,                 nullptr,                 nullptr,                        nullptr,                        nullptr,                     nullptr,                nullptr,                  nullptr             				 },
    {&globalLevelMenu,        &globalPitchMenu,        nullptr,                        &globalPanMenu,                 nullptr,                     &srrMenu,               &bitcrushMenu,            nullptr                            },
    {nullptr,              	  nullptr,                 nullptr,                        nullptr,                        nullptr,                     &threshold,             nullptr,                  nullptr                            },
    {nullptr,                 nullptr,                 nullptr,                        nullptr,                        &globalLPFMorphMenu,         &lpfModeMenu,           &globalLPFResMenu,        &globalLPFFreqMenu                 },
    {nullptr,                 nullptr,                 nullptr,                        nullptr,                        &globalHPFMorphMenu,         &hpfModeMenu,           &globalHPFResMenu,        &globalHPFFreqMenu                 },
    {&sidechainReleaseMenu,   &sidechainSyncMenu,      &globalSidechainVolumeMenu,     &sidechainAttackMenu,           &sidechainShapeMenu,         nullptr,                &bassMenu,                &bassFreqMenu                      },
    {&arpRateMenu,            &arpSyncMenu,            &arpGateMenu,                   &arpOctavesMenu,                &arpPresetModeMenu,          &nameEditMenu,          &trebleMenu,              &trebleFreqMenu                    },
    {nullptr,                 nullptr,                 nullptr,                        &modFXTypeMenu,                 &modFXOffsetMenu,            &modFXFeedbackMenu,     &globalModFXDepthMenu,    &globalModFXRateMenu               },
    {nullptr,                 nullptr,                 nullptr,                        &globalReverbSendAmountMenu,    &reverbPanMenu,              &reverbWidthMenu,       &reverbDampingMenu,       &reverbRoomSizeMenu                },
    {&globalDelayRateMenu,    &delaySyncMenu,          &delayAnalogMenu,               &globalDelayFeedbackMenu,       &delayPingPongMenu,          nullptr,                nullptr,                  nullptr                            },
    {nullptr,          	      &arpSpreadVelocityMenu,  nullptr,                        &arpNoteProbabilityMenu,        nullptr,                     nullptr,                nullptr,                  nullptr                            },
};
PLACE_SDRAM_DATA Submenu* parentsForKitGlobalFXShortcuts[][kDisplayHeight] = {
    {nullptr,                 nullptr,                 nullptr,                        nullptr,                        nullptr,                     nullptr,                nullptr,                  nullptr,                           },
    {nullptr,                 nullptr,                 nullptr,                        nullptr,                        nullptr,                     nullptr,                nullptr,                  nullptr,                           },
    {nullptr,                 nullptr,                 nullptr,                        nullptr,                        nullptr,                     nullptr,                nullptr,                  nullptr,                           },
    {nullptr,                 nullptr,                 nullptr,                        nullptr,                        nullptr,                     nullptr,                nullptr,                  nullptr,                           },
    {nullptr,                 nullptr,                 nullptr,                        nullptr,                        nullptr,                     nullptr,                nullptr,                  nullptr,                           },
    {nullptr,                 nullptr,                 nullptr,                        nullptr,                        nullptr,                     nullptr,                nullptr,                  nullptr,                           },
    {nullptr,                 nullptr,                 nullptr,                        nullptr,                        nullptr,                     &globalDistortionMenu,  &globalDistortionMenu,    nullptr,                           },
    {nullptr,                 nullptr,                 nullptr,                        nullptr,                        nullptr,                     &audioCompMenu,         nullptr,                  nullptr,                           },
    {nullptr,                 nullptr,                 nullptr,                        nullptr,                        &globalLPFMenu,              &globalLPFMenu,         &globalLPFMenu,           &globalLPFMenu,                    },
    {nullptr,                 nullptr,                 nullptr,                        nullptr,                        &globalHPFMenu,              &globalHPFMenu,         &globalHPFMenu,           &globalHPFMenu,                    },
    {nullptr,                 nullptr,                 nullptr,                        nullptr,                        nullptr,                     nullptr,                &globalEQMenu,            &globalEQMenu,                     },
    {&arpBasicMenuKit,        &arpBasicMenuKit,        &arpBasicMenuKit,               &arpPatternMenu,                &arpPresetAndRandomizerMenu, nullptr,                &globalEQMenu,            &globalEQMenu,                     },
    {nullptr,                 nullptr,                 nullptr,                        &globalModFXMenu,               &globalModFXMenu,            &globalModFXMenu,       &globalModFXMenu,         &globalModFXMenu,                  },
    {nullptr,                 nullptr,                 nullptr,                        &globalReverbMenu,              &globalReverbMenu,           &globalReverbMenu,      &globalReverbMenu,        &globalReverbMenu,                 },
    {&globalDelayMenu,        &globalDelayMenu,        &globalDelayMenu,               &globalDelayMenu,               &globalDelayMenu,            nullptr,                nullptr,                  nullptr,                           },
    {nullptr,                 &globalRandomizerMenu,   nullptr,                        &globalRandomizerMenu,          nullptr,                     nullptr,                nullptr,                  nullptr,                           },
};
PLACE_SDRAM_DATA Submenu* parentsForMidiOrCVParamShortcuts[][kDisplayHeight] = {
    {nullptr,                 nullptr,                 nullptr,                        nullptr,                        nullptr,                     nullptr,                nullptr,                  nullptr,                           },
    {nullptr,                 nullptr,                 nullptr,                        nullptr,                        nullptr,                     nullptr,                nullptr,                  nullptr,                           },
    {nullptr,                 nullptr,                 nullptr,                        nullptr,                        nullptr,                     nullptr,                nullptr,                  nullptr,                           },
    {nullptr,                 nullptr,                 nullptr,                        nullptr,                        nullptr,                     nullptr,                nullptr,                  nullptr,                           },
    {nullptr,                 nullptr,                 nullptr,                        nullptr,                        nullptr,                     nullptr,                nullptr,                  nullptr,                           },
    {nullptr,                 nullptr,                 nullptr,                        nullptr,                        nullptr,                     nullptr,                nullptr,                  nullptr,                           },
    {nullptr,                 nullptr,                 nullptr,                        nullptr,                        nullptr,                     nullptr,                nullptr,                  nullptr,                           },
    {nullptr,                 nullptr,                 nullptr,                        nullptr,                        nullptr,                     nullptr,                nullptr,                  nullptr,                           },
    {nullptr,                 nullptr,                 nullptr,                        nullptr,                        nullptr,                     nullptr,                nullptr,                  nullptr,                           },
    {nullptr,                 nullptr,                 nullptr,                        nullptr,                        nullptr,                     nullptr,                nullptr,                  nullptr,                           },
    {nullptr,                 nullptr,                 nullptr,                        nullptr,                        nullptr,                     nullptr,                nullptr,                  nullptr,                           },
    {&arpBasicMenuMIDIOrCV,   &arpBasicMenuMIDIOrCV,   &arpBasicMenuMIDIOrCV,          &arpPatternMenu,                &arpPresetAndRandomizerMenu, nullptr,                nullptr,                  nullptr,                           },
    {nullptr,                 nullptr,                 nullptr,                        nullptr,                        nullptr,                     nullptr,                nullptr,                  nullptr,                           },
    {nullptr,                 nullptr,                 nullptr,                        nullptr,                        nullptr,                     nullptr,                nullptr,                  nullptr,                           },
    {nullptr,                 nullptr,                 nullptr,                        nullptr,                        nullptr,                     nullptr,                nullptr,                  nullptr,                           },
    {nullptr,                 &globalRandomizerMenu,   nullptr,                        &globalRandomizerMenu,          nullptr,                     nullptr,                nullptr,                  nullptr,                           },
};

//clang-format on

void setOscillatorNumberForTitles(int32_t num) {
	num += 1;
	oscTypeMenu.format(num);
	sourceVolumeMenu.format(num);
	sourceWaveIndexMenu.format(num);
	sourceTransposeMenu.format(num);
	pulseWidthMenu.format(num);
	oscPhaseMenu.format(num);

	sourceFeedbackMenu.format(num);

	sampleReverseMenu.format(num);
	sampleRepeatMenu.format(num);
	timeStretchMenu.format(num);
	interpolationMenu.format(num);
}

void setModulatorNumberForTitles(int32_t num) {
	num += 1;
	modulatorTransposeMenu.format(num);
	modulatorVolume.format(num);
	modulatorFeedbackMenu.format(num);
	modulatorPhaseMenu.format(num);
}

void setCvNumberForTitle(int32_t num) {
	num += 1;
	cvSubmenu.format(num);
	cvVoltsMenu.format(num);
	cvTransposeMenu.format(num);
}
