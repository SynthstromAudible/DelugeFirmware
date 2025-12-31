#include "../menu_item/randomizer/midi_cv/note_probability.h"
#include "gui/l10n/strings.h"
#include "gui/menu_item/active_scales.h"
#include "gui/menu_item/arpeggiator/arp_unpatched_param.h"
#include "gui/menu_item/arpeggiator/chord_type.h"
#include "gui/menu_item/arpeggiator/include_in_kit_arp.h"
#include "gui/menu_item/arpeggiator/midi_cv/gate.h"
#include "gui/menu_item/arpeggiator/midi_cv/rate.h"
#include "gui/menu_item/arpeggiator/midi_cv/rhythm.h"
#include "gui/menu_item/arpeggiator/midi_cv/sequence_length.h"
#include "gui/menu_item/arpeggiator/mode.h"
#include "gui/menu_item/arpeggiator/mpe_velocity.h"
#include "gui/menu_item/arpeggiator/note_mode.h"
#include "gui/menu_item/arpeggiator/note_mode_for_drums.h"
#include "gui/menu_item/arpeggiator/octave_mode.h"
#include "gui/menu_item/arpeggiator/octaves.h"
#include "gui/menu_item/arpeggiator/preset_mode.h"
#include "gui/menu_item/arpeggiator/rate.h"
#include "gui/menu_item/arpeggiator/rhythm.h"
#include "gui/menu_item/arpeggiator/sequence_length.h"
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
#include "gui/menu_item/defaults/accessibility_menu_highlighting.h"
#include "gui/menu_item/defaults/bend_range.h"
#include "gui/menu_item/defaults/favourites_layout.h"
#include "gui/menu_item/defaults/grid_default_active_mode.h"
#include "gui/menu_item/defaults/hold_time.h"
#include "gui/menu_item/defaults/keyboard_layout.h"
#include "gui/menu_item/defaults/magnitude.h"
#include "gui/menu_item/defaults/metronome_volume.h"
#include "gui/menu_item/defaults/pad_brightness.h"
#include "gui/menu_item/defaults/patch_cable_polarity.h"
#include "gui/menu_item/defaults/scale.h"
#include "gui/menu_item/defaults/session_layout.h"
#include "gui/menu_item/defaults/slice_mode.h"
#include "gui/menu_item/defaults/startup_song_mode.h"
#include "gui/menu_item/defaults/swing_interval.h"
#include "gui/menu_item/defaults/ui/clip_type/default_new_clip_type.h"
#include "gui/menu_item/defaults/velocity.h"
#include "gui/menu_item/delay/amount.h"
#include "gui/menu_item/delay/amount_unpatched.h"
#include "gui/menu_item/delay/analog.h"
#include "gui/menu_item/delay/ping_pong.h"
#include "gui/menu_item/delay/sync.h"
#include "gui/menu_item/dx/browse.h"
#include "gui/menu_item/dx/engine_select.h"
#include "gui/menu_item/dx/global_params.h"
#include "gui/menu_item/edit_name.h"
#include "gui/menu_item/envelope/envelope_menu.h"
#include "gui/menu_item/envelope/segment.h"
#include "gui/menu_item/eq/eq_menu.h"
#include "gui/menu_item/eq/eq_unpatched_param.h"
#include "gui/menu_item/file_selector.h"
#include "gui/menu_item/filter/filter_container.h"
#include "gui/menu_item/filter/mode.h"
#include "gui/menu_item/filter/param.h"
#include "gui/menu_item/filter_route.h"
#include "gui/menu_item/firmware/version.h"
#include "gui/menu_item/flash/status.h"
#include "gui/menu_item/fx/clipping.h"
#include "gui/menu_item/gate/mode.h"
#include "gui/menu_item/gate/off_time.h"
#include "gui/menu_item/gate/selection.h"
#include "gui/menu_item/horizontal_menu.h"
#include "gui/menu_item/horizontal_menu_container.h"
#include "gui/menu_item/horizontal_menu_group.h"
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
#include "gui/menu_item/midi/device_receive_clock.h"
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
#include "gui/menu_item/randomizer/midi_cv/bass_probability.h"
#include "gui/menu_item/randomizer/midi_cv/chord_polyphony.h"
#include "gui/menu_item/randomizer/midi_cv/chord_probability.h"
#include "gui/menu_item/randomizer/midi_cv/glide_probability.h"
#include "gui/menu_item/randomizer/midi_cv/ratchet_amount.h"
#include "gui/menu_item/randomizer/midi_cv/ratchet_probability.h"
#include "gui/menu_item/randomizer/midi_cv/spread_gate.h"
#include "gui/menu_item/randomizer/midi_cv/spread_octave.h"
#include "gui/menu_item/randomizer/midi_cv/spread_velocity.h"
#include "gui/menu_item/randomizer/midi_cv/swap_probability.h"
#include "gui/menu_item/randomizer/randomizer_lock.h"
#include "gui/menu_item/randomizer/randomizer_unpatched_param.h"
#include "gui/menu_item/record/countin.h"
#include "gui/menu_item/record/loop_command.h"
#include "gui/menu_item/record/quantize.h"
#include "gui/menu_item/record/threshold_mode.h"
#include "gui/menu_item/reverb/amount.h"
#include "gui/menu_item/reverb/amount_unpatched.h"
#include "gui/menu_item/reverb/damping.h"
#include "gui/menu_item/reverb/hpf.h"
#include "gui/menu_item/reverb/lpf.h"
#include "gui/menu_item/reverb/model.h"
#include "gui/menu_item/reverb/pan.h"
#include "gui/menu_item/reverb/room_size.h"
#include "gui/menu_item/reverb/sidechain/shape.h"
#include "gui/menu_item/reverb/sidechain/volume.h"
#include "gui/menu_item/reverb/width.h"
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
#include "gui/menu_item/shortcuts/version.h"
#include "gui/menu_item/sidechain/attack.h"
#include "gui/menu_item/sidechain/release.h"
#include "gui/menu_item/sidechain/send.h"
#include "gui/menu_item/sidechain/shape.h"
#include "gui/menu_item/sidechain/sync.h"
#include "gui/menu_item/sidechain/volume.h"
#include "gui/menu_item/sidechain/volume_global.h"
#include "gui/menu_item/song/configure_macros.h"
#include "gui/menu_item/song/midi_learn.h"
#include "gui/menu_item/source/patched_param/modulator_feedback.h"
#include "gui/menu_item/source/patched_param/modulator_level.h"
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
#include "gui/menu_item/swing/interval.h"
#include "gui/menu_item/synth_mode.h"
#include "gui/menu_item/trigger/in/ppqn.h"
#include "gui/menu_item/trigger/out/ppqn.h"
#include "gui/menu_item/unison/count.h"
#include "gui/menu_item/unison/detune.h"
#include "gui/menu_item/unison/stereoSpread.h"
#include "gui/menu_item/unpatched_param.h"
#include "gui/menu_item/unpatched_param/pan.h"
#include "gui/menu_item/voice/polyphony.h"
#include "gui/menu_item/voice/portamento.h"
#include "gui/menu_item/voice/priority.h"
#include "io/midi/midi_device_manager.h"
#include "io/midi/midi_engine.h"
#include "modulation/params/param.h"
#include "playback/playback_handler.h"
#include "storage/flash_storage.h"

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
PLACE_SDRAM_BSS arpeggiator::PresetMode arpPresetModeMenu{STRING_FOR_PRESET, STRING_FOR_ARP_PRESET_MENU_TITLE};
// Rate
PLACE_SDRAM_BSS arpeggiator::Mode arpModeMenu{STRING_FOR_ENABLED, STRING_FOR_ARP_MODE_MENU_TITLE};
PLACE_SDRAM_BSS arpeggiator::Sync arpSyncMenu{STRING_FOR_SYNC, STRING_FOR_ARP_SYNC_MENU_TITLE};
PLACE_SDRAM_BSS arpeggiator::Rate arpRateMenu{STRING_FOR_RATE, STRING_FOR_ARP_RATE_MENU_TITLE, params::GLOBAL_ARP_RATE};
PLACE_SDRAM_BSS arpeggiator::KitRate arpKitRateMenu{STRING_FOR_RATE, STRING_FOR_ARP_RATE_MENU_TITLE,
                                                    params::UNPATCHED_ARP_RATE};
PLACE_SDRAM_BSS arpeggiator::midi_cv::Rate arpRateMenuMIDIOrCV{STRING_FOR_RATE, STRING_FOR_ARP_RATE_MENU_TITLE};
// Pattern
PLACE_SDRAM_BSS arpeggiator::Octaves arpOctavesMenu{STRING_FOR_NUMBER_OF_OCTAVES, STRING_FOR_ARP_OCTAVES_MENU_TITLE};
PLACE_SDRAM_BSS arpeggiator::OctaveMode arpOctaveModeMenu{STRING_FOR_OCTAVE_MODE,
                                                          STRING_FOR_ARP_OCTAVE_MODE_MENU_TITLE};
PLACE_SDRAM_BSS arpeggiator::OctaveModeToNoteMode arpeggiator::arpOctaveModeToNoteModeMenu{
    STRING_FOR_OCTAVE_MODE, STRING_FOR_ARP_OCTAVE_MODE_MENU_TITLE};
PLACE_SDRAM_BSS arpeggiator::OctaveModeToNoteModeForDrums arpeggiator::arpOctaveModeToNoteModeMenuForDrums{
    STRING_FOR_OCTAVE_MODE, STRING_FOR_ARP_OCTAVE_MODE_MENU_TITLE};
PLACE_SDRAM_BSS arpeggiator::NoteMode arpNoteModeMenu{STRING_FOR_NOTE_MODE, STRING_FOR_ARP_NOTE_MODE_MENU_TITLE};
PLACE_SDRAM_BSS arpeggiator::NoteModeForDrums arpNoteModeMenuForDrums{STRING_FOR_NOTE_MODE,
                                                                      STRING_FOR_ARP_NOTE_MODE_MENU_TITLE};
PLACE_SDRAM_BSS arpeggiator::NoteModeFromOctaveMode arpeggiator::arpNoteModeFromOctaveModeMenu{
    STRING_FOR_NOTE_MODE, STRING_FOR_ARP_NOTE_MODE_MENU_TITLE};
PLACE_SDRAM_BSS arpeggiator::NoteModeFromOctaveModeForDrums arpeggiator::arpNoteModeFromOctaveModeMenuForDrums{
    STRING_FOR_NOTE_MODE, STRING_FOR_ARP_NOTE_MODE_MENU_TITLE};
PLACE_SDRAM_BSS arpeggiator::ChordType arpChordSimulatorMenuKit{STRING_FOR_CHORD_SIMULATOR,
                                                                STRING_FOR_ARP_CHORD_SIMULATOR_MENU_TITLE};
PLACE_SDRAM_BSS arpeggiator::StepRepeat arpStepRepeatMenu{STRING_FOR_STEP_REPEAT,
                                                          STRING_FOR_ARP_STEP_REPEAT_MENU_TITLE};
// Note and rhythm settings
PLACE_SDRAM_BSS arpeggiator::ArpUnpatchedParam arpGateMenu{STRING_FOR_GATE, STRING_FOR_ARP_GATE_MENU_TITLE,
                                                           params::UNPATCHED_ARP_GATE, RenderingStyle::LENGTH_SLIDER};
PLACE_SDRAM_BSS arpeggiator::midi_cv::Gate arpGateMenuMIDIOrCV{STRING_FOR_GATE, STRING_FOR_ARP_GATE_MENU_TITLE};
PLACE_SDRAM_BSS arpeggiator::Rhythm arpRhythmMenu{STRING_FOR_RHYTHM, STRING_FOR_ARP_RHYTHM_MENU_TITLE,
                                                  params::UNPATCHED_ARP_RHYTHM};
PLACE_SDRAM_BSS arpeggiator::midi_cv::Rhythm arpRhythmMenuMIDIOrCV{STRING_FOR_RHYTHM, STRING_FOR_ARP_RHYTHM_MENU_TITLE};

PLACE_SDRAM_BSS arpeggiator::SequenceLength arpSequenceLengthMenu{
    STRING_FOR_SEQUENCE_LENGTH, STRING_FOR_ARP_SEQUENCE_LENGTH_MENU_TITLE, params::UNPATCHED_ARP_SEQUENCE_LENGTH};
PLACE_SDRAM_BSS arpeggiator::midi_cv::SequenceLength arpSequenceLengthMenuMIDIOrCV{
    STRING_FOR_SEQUENCE_LENGTH, STRING_FOR_ARP_SEQUENCE_LENGTH_MENU_TITLE};

PLACE_SDRAM_BSS arpeggiator::IncludeInKitArp arpIncludeInKitArpMenu{STRING_FOR_INCLUDE_IN_KIT_ARP,
                                                                    STRING_FOR_INCLUDE_IN_KIT_ARP};

// Randomizer ---------------------------------
PLACE_SDRAM_BSS randomizer::RandomizerLock randomizerLockMenu{STRING_FOR_RANDOMIZER_LOCK,
                                                              STRING_FOR_ARP_RANDOMIZER_LOCK_TITLE};
PLACE_SDRAM_BSS randomizer::RandomizerUnpatchedParam spreadGateMenu{
    STRING_FOR_SPREAD_GATE, STRING_FOR_ARP_SPREAD_GATE_MENU_TITLE, params::UNPATCHED_ARP_SPREAD_GATE, BAR};
PLACE_SDRAM_BSS randomizer::midi_cv::SpreadGate spreadGateMenuMIDIOrCV{STRING_FOR_SPREAD_GATE,
                                                                       STRING_FOR_ARP_SPREAD_GATE_MENU_TITLE};
PLACE_SDRAM_BSS randomizer::RandomizerSoundOnlyUnpatchedParam spreadOctaveMenu{
    STRING_FOR_SPREAD_OCTAVE, STRING_FOR_ARP_SPREAD_OCTAVE_MENU_TITLE, params::UNPATCHED_ARP_SPREAD_OCTAVE, BAR};
PLACE_SDRAM_BSS randomizer::midi_cv::SpreadOctave spreadOctaveMenuMIDIOrCV{STRING_FOR_SPREAD_OCTAVE,
                                                                           STRING_FOR_ARP_SPREAD_OCTAVE_MENU_TITLE};
PLACE_SDRAM_BSS randomizer::RandomizerUnpatchedParam spreadVelocityMenu{
    STRING_FOR_SPREAD_VELOCITY, STRING_FOR_SPREAD_VELOCITY_MENU_TITLE, params::UNPATCHED_SPREAD_VELOCITY, BAR};
PLACE_SDRAM_BSS randomizer::midi_cv::SpreadVelocity spreadVelocityMenuMIDIOrCV{STRING_FOR_SPREAD_VELOCITY,
                                                                               STRING_FOR_SPREAD_VELOCITY_MENU_TITLE};
PLACE_SDRAM_BSS randomizer::RandomizerUnpatchedParam ratchetAmountMenu{
    STRING_FOR_NUMBER_OF_RATCHETS, STRING_FOR_ARP_RATCHETS_MENU_TITLE, params::UNPATCHED_ARP_RATCHET_AMOUNT, BAR};
PLACE_SDRAM_BSS randomizer::midi_cv::RatchetAmount ratchetAmountMenuMIDIOrCV{STRING_FOR_NUMBER_OF_RATCHETS,
                                                                             STRING_FOR_ARP_RATCHETS_MENU_TITLE};
PLACE_SDRAM_BSS randomizer::RandomizerUnpatchedParam ratchetProbabilityMenu{
    STRING_FOR_RATCHET_PROBABILITY, STRING_FOR_ARP_RATCHET_PROBABILITY_MENU_TITLE,
    params::UNPATCHED_ARP_RATCHET_PROBABILITY, PERCENT};
PLACE_SDRAM_BSS randomizer::midi_cv::RatchetProbability ratchetProbabilityMenuMIDIOrCV{
    STRING_FOR_RATCHET_PROBABILITY, STRING_FOR_ARP_RATCHET_PROBABILITY_MENU_TITLE};
PLACE_SDRAM_BSS randomizer::RandomizerNonKitSoundUnpatchedParam chordPolyphonyMenu{
    STRING_FOR_CHORD_POLYPHONY, STRING_FOR_ARP_CHORD_POLYPHONY_MENU_TITLE, params::UNPATCHED_ARP_CHORD_POLYPHONY, BAR};
PLACE_SDRAM_BSS randomizer::midi_cv::ChordPolyphony chordPolyphonyMenuMIDIOrCV{
    STRING_FOR_CHORD_POLYPHONY, STRING_FOR_ARP_CHORD_POLYPHONY_MENU_TITLE};
PLACE_SDRAM_BSS randomizer::RandomizerNonKitSoundUnpatchedParam chordProbabilityMenu{
    STRING_FOR_CHORD_PROBABILITY, STRING_FOR_ARP_CHORD_PROBABILITY_MENU_TITLE, params::UNPATCHED_ARP_CHORD_PROBABILITY,
    PERCENT};
PLACE_SDRAM_BSS randomizer::midi_cv::ChordProbability chordProbabilityMenuMIDIOrCV{
    STRING_FOR_CHORD_PROBABILITY, STRING_FOR_ARP_CHORD_PROBABILITY_MENU_TITLE};
PLACE_SDRAM_BSS randomizer::RandomizerUnpatchedParam randomizerNoteProbabilityMenu{
    STRING_FOR_NOTE_PROBABILITY, STRING_FOR_NOTE_PROBABILITY_MENU_TITLE, params::UNPATCHED_NOTE_PROBABILITY, PERCENT};
PLACE_SDRAM_BSS randomizer::midi_cv::NoteProbability randomizerNoteProbabilityMenuMIDIOrCV{
    STRING_FOR_NOTE_PROBABILITY, STRING_FOR_NOTE_PROBABILITY_MENU_TITLE};
PLACE_SDRAM_BSS randomizer::RandomizerUnpatchedParam swapProbabilityMenu{
    STRING_FOR_SWAP_PROBABILITY, STRING_FOR_ARP_SWAP_PROBABILITY_MENU_TITLE, params::UNPATCHED_ARP_SWAP_PROBABILITY,
    PERCENT};
PLACE_SDRAM_BSS randomizer::midi_cv::SwapProbability swapProbabilityMenuMIDIOrCV{
    STRING_FOR_SWAP_PROBABILITY, STRING_FOR_ARP_SWAP_PROBABILITY_MENU_TITLE};
PLACE_SDRAM_BSS randomizer::RandomizerUnpatchedParam bassProbabilityMenu{
    STRING_FOR_BASS_PROBABILITY, STRING_FOR_ARP_BASS_PROBABILITY_MENU_TITLE, params::UNPATCHED_ARP_BASS_PROBABILITY,
    PERCENT};
PLACE_SDRAM_BSS randomizer::midi_cv::BassProbability bassProbabilityMenuMIDIOrCV{
    STRING_FOR_BASS_PROBABILITY, STRING_FOR_ARP_BASS_PROBABILITY_MENU_TITLE};

PLACE_SDRAM_BSS randomizer::RandomizerUnpatchedParam glideProbabilityMenu{
    STRING_FOR_GLIDE_PROBABILITY, STRING_FOR_ARP_GLIDE_PROBABILITY_MENU_TITLE, params::UNPATCHED_ARP_GLIDE_PROBABILITY,
    PERCENT};
PLACE_SDRAM_BSS randomizer::midi_cv::GlideProbability glideProbabilityMenuMIDIOrCV{
    STRING_FOR_GLIDE_PROBABILITY, STRING_FOR_ARP_GLIDE_PROBABILITY_MENU_TITLE};
PLACE_SDRAM_BSS randomizer::RandomizerUnpatchedParam reverseProbabilityMenu{
    STRING_FOR_REVERSE_PROBABILITY, STRING_FOR_REVERSE_PROBABILITY_MENU_TITLE, params::UNPATCHED_REVERSE_PROBABILITY,
    PERCENT};

PLACE_SDRAM_BSS HorizontalMenu randomizerMenu{STRING_FOR_RANDOMIZER,
                                              {// Lock
                                               &randomizerLockMenu,
                                               // Spreads
                                               &spreadGateMenu, &spreadGateMenuMIDIOrCV, &spreadOctaveMenu,
                                               &spreadOctaveMenuMIDIOrCV, &spreadVelocityMenu,
                                               &spreadVelocityMenuMIDIOrCV,
                                               // Ratchets: Amount
                                               &ratchetAmountMenu, &ratchetAmountMenuMIDIOrCV,
                                               // Ratchets: Probability
                                               &ratchetProbabilityMenu, &ratchetProbabilityMenuMIDIOrCV,
                                               // Chords: Polyphony
                                               &chordPolyphonyMenu, &chordPolyphonyMenuMIDIOrCV,
                                               // Chords: Probability
                                               &chordProbabilityMenu, &chordProbabilityMenuMIDIOrCV,
                                               // Note
                                               &randomizerNoteProbabilityMenu, &randomizerNoteProbabilityMenuMIDIOrCV,
                                               // Swap
                                               &swapProbabilityMenu, &swapProbabilityMenuMIDIOrCV,
                                               // Bass
                                               &bassProbabilityMenu, &bassProbabilityMenuMIDIOrCV,
                                               // Glide
                                               &glideProbabilityMenu, &glideProbabilityMenuMIDIOrCV,
                                               // Reverse
                                               &reverseProbabilityMenu}};

// Arp: Basic
PLACE_SDRAM_BSS HorizontalMenu arpBasicMenu{
    STRING_FOR_BASIC, STRING_FOR_ARP_BASIC_MENU_TITLE, {&arpPresetModeMenu, &arpGateMenu, &arpSyncMenu, &arpRateMenu}};
PLACE_SDRAM_BSS HorizontalMenu arpBasicMenuKit{STRING_FOR_BASIC,
                                               STRING_FOR_ARP_BASIC_MENU_TITLE,
                                               {&arpPresetModeMenu, &arpGateMenu, &arpSyncMenu, &arpKitRateMenu}};
PLACE_SDRAM_BSS HorizontalMenu arpBasicMenuMIDIOrCV{
    STRING_FOR_BASIC,
    STRING_FOR_ARP_BASIC_MENU_TITLE,
    {&arpPresetModeMenu, &arpGateMenuMIDIOrCV, &arpSyncMenu, &arpRateMenuMIDIOrCV}};

// Arp: Pattern
PLACE_SDRAM_BSS HorizontalMenu arpPatternMenu{STRING_FOR_PATTERN,
                                              STRING_FOR_ARP_PATTERN_MENU_TITLE,
                                              {// Pattern
                                               &arpOctavesMenu, &arpStepRepeatMenu, &arpOctaveModeMenu,
                                               &arpNoteModeMenu, &arpNoteModeMenuForDrums, &arpChordSimulatorMenuKit,
                                               // Note and rhythm settings
                                               &arpRhythmMenu, &arpRhythmMenuMIDIOrCV, &arpSequenceLengthMenu,
                                               &arpSequenceLengthMenuMIDIOrCV}};

PLACE_SDRAM_BSS HorizontalMenuGroup arpMenuGroup{{&arpBasicMenu, &arpPatternMenu}};
PLACE_SDRAM_BSS HorizontalMenuGroup arpMenuGroupKit{{&arpBasicMenuKit, &arpPatternMenu}};
PLACE_SDRAM_BSS HorizontalMenuGroup arpMenuGroupMIDIOrCV{{&arpBasicMenuMIDIOrCV, &arpPatternMenu}};

// Arp: MPE
PLACE_SDRAM_BSS arpeggiator::ArpMpeVelocity arpMpeVelocityMenu{STRING_FOR_VELOCITY, STRING_FOR_VELOCITY};
PLACE_SDRAM_BSS submenu::ArpMpeSubmenu arpMpeMenu{STRING_FOR_MPE, {&arpMpeVelocityMenu}};

PLACE_SDRAM_BSS Submenu arpMenu{
    STRING_FOR_ARPEGGIATOR,
    {
        // Mode
        &arpModeMenu,
        // Basic
        &arpBasicMenu,
        // Pattern
        &arpPatternMenu,
        // MPE
        &arpMpeMenu,
        // Include in kit arp
        &arpIncludeInKitArpMenu,
    },
};

PLACE_SDRAM_BSS Submenu arpMenuMIDIOrCV{
    STRING_FOR_ARPEGGIATOR,
    {
        // Mode
        &arpModeMenu,
        // Basic
        &arpBasicMenuMIDIOrCV,
        // Pattern
        &arpPatternMenu,
        // MPE
        &arpMpeMenu,
        // Include in kit arp
        &arpIncludeInKitArpMenu,
    },
};

PLACE_SDRAM_BSS Submenu kitArpMenu{
    STRING_FOR_KIT_ARPEGGIATOR,
    {
        // Mode
        &arpModeMenu,
        // Basic
        &arpBasicMenuKit,
        // Pattern
        &arpPatternMenu,
        // Randomizer
        &randomizerMenu,
    },
};

// Voice menu ----------------------------------------------------------------------------------------------------

PLACE_SDRAM_BSS voice::PolyphonyType polyphonyMenu{STRING_FOR_POLYPHONY};
PLACE_SDRAM_BSS voice::VoiceCount voice::polyphonicVoiceCountMenu{STRING_FOR_MAX_VOICES};
PLACE_SDRAM_BSS voice::Portamento portaMenu{STRING_FOR_PORTAMENTO};
PLACE_SDRAM_BSS voice::Priority priorityMenu{STRING_FOR_PRIORITY};

PLACE_SDRAM_BSS HorizontalMenu voiceMenu{
    STRING_FOR_VOICE,
    {&priorityMenu, &polyphonyMenu, &voice::polyphonicVoiceCountMenu, &portaMenu, &unisonMenu},
    HorizontalMenu::Layout::FIXED};
PLACE_SDRAM_BSS HorizontalMenu voiceMenuWithoutUnison{
    STRING_FOR_VOICE,
    {&priorityMenu, &polyphonyMenu, &voice::polyphonicVoiceCountMenu, &portaMenu},
    HorizontalMenu::Layout::FIXED};
HorizontalMenuGroup voiceMenuGroup{{&unisonMenu, &voiceMenuWithoutUnison}};

// Envelope 1-4 menu -----------------------------------------------------------------------------
PLACE_SDRAM_BSS HorizontalMenuGroup envMenuGroup{{&env1Menu, &env2Menu, &env3Menu, &env4Menu}};

// LFO 1-4 menu -----------------------------------------------------------------------------
PLACE_SDRAM_BSS HorizontalMenuGroup lfoMenuGroup{{&lfo1Menu, &lfo2Menu, &lfo3Menu, &lfo4Menu}};

// Mod FX ----------------------------------------------------------------------------------
PLACE_SDRAM_BSS mod_fx::Type modFXTypeMenu{STRING_FOR_TYPE, STRING_FOR_MODFX_TYPE};
PLACE_SDRAM_BSS mod_fx::Rate modFXRateMenu{STRING_FOR_RATE, STRING_FOR_MODFX_RATE, params::GLOBAL_MOD_FX_RATE};
PLACE_SDRAM_BSS mod_fx::Feedback modFXFeedbackMenu{STRING_FOR_FEEDBACK, STRING_FOR_MODFX_FEEDBACK,
                                                   params::UNPATCHED_MOD_FX_FEEDBACK};
PLACE_SDRAM_BSS mod_fx::Depth_Patched modFXDepthMenu{STRING_FOR_DEPTH, STRING_FOR_MODFX_DEPTH,
                                                     params::GLOBAL_MOD_FX_DEPTH};
PLACE_SDRAM_BSS mod_fx::Offset modFXOffsetMenu{STRING_FOR_OFFSET, STRING_FOR_MODFX_OFFSET,
                                               params::UNPATCHED_MOD_FX_OFFSET};

PLACE_SDRAM_BSS submenu::ModFxHorizontalMenu modFXMenu{
    STRING_FOR_MOD_FX,
    {
        &modFXTypeMenu,
        &modFXDepthMenu,
        &modFXRateMenu,
        &modFXFeedbackMenu,
        &modFXOffsetMenu,
    },
};

// EQ -------------------------------------------------------------------------------------
PLACE_SDRAM_BSS eq::EqUnpatchedParam bassMenu{STRING_FOR_BASS, params::UNPATCHED_BASS};
PLACE_SDRAM_BSS eq::EqUnpatchedParam trebleMenu{STRING_FOR_TREBLE, params::UNPATCHED_TREBLE};
PLACE_SDRAM_BSS eq::EqUnpatchedParam bassFreqMenu{STRING_FOR_BASS_FREQUENCY, STRING_FOR_BASS_FREQUENCY_SHORT,
                                                  params::UNPATCHED_BASS_FREQ};
PLACE_SDRAM_BSS eq::EqUnpatchedParam trebleFreqMenu{STRING_FOR_TREBLE_FREQUENCY, STRING_FOR_TREBLE_FREQUENCY_SHORT,
                                                    params::UNPATCHED_TREBLE_FREQ};

PLACE_SDRAM_BSS eq::EqMenu eqMenu{
    STRING_FOR_EQ,
    {
        &bassMenu,
        &trebleMenu,
        &bassFreqMenu,
        &trebleFreqMenu,
    },
};

// Delay ---------------------------------------------------------------------------------
PLACE_SDRAM_BSS delay::Amount delayFeedbackMenu{STRING_FOR_AMOUNT, STRING_FOR_DELAY_AMOUNT,
                                                params::GLOBAL_DELAY_FEEDBACK};
PLACE_SDRAM_BSS patched_param::Integer delayRateMenu{STRING_FOR_RATE, STRING_FOR_DELAY_RATE, params::GLOBAL_DELAY_RATE};
PLACE_SDRAM_BSS delay::PingPong delayPingPongMenu{STRING_FOR_PINGPONG, STRING_FOR_DELAY_PINGPONG};
PLACE_SDRAM_BSS delay::Analog delayAnalogMenu{STRING_FOR_TYPE, STRING_FOR_DELAY_TYPE};
PLACE_SDRAM_BSS delay::Sync delaySyncMenu{STRING_FOR_SYNC, STRING_FOR_DELAY_SYNC};

PLACE_SDRAM_BSS HorizontalMenu delayMenu{
    STRING_FOR_DELAY,
    {
        &delayFeedbackMenu,
        &delayPingPongMenu,
        &delaySyncMenu,
        &delayRateMenu,
        &delayAnalogMenu,
    },
};

// Stutter ----------------------------------------------------------------------------------
PLACE_SDRAM_BSS stutter::StutterDirection stutterDirectionMenu{STRING_FOR_DIRECTION, STRING_FOR_DIRECTION};
PLACE_SDRAM_BSS stutter::QuantizedStutter stutterQuantizedMenu{STRING_FOR_QUANTIZE, STRING_FOR_QUANTIZE};
PLACE_SDRAM_BSS stutter::Rate stutterRateMenu{STRING_FOR_RATE, STRING_FOR_STUTTER_RATE};

PLACE_SDRAM_BSS HorizontalMenu stutterMenu{STRING_FOR_STUTTER,
                                           {&stutterRateMenu, &stutterDirectionMenu, &stutterQuantizedMenu},
                                           HorizontalMenu::Layout::FIXED};

// Bend Ranges -------------------------------------------------------------------------------

PLACE_SDRAM_BSS bend_range::Main mainBendRangeMenu{STRING_FOR_NORMAL};
PLACE_SDRAM_BSS bend_range::PerFinger perFingerBendRangeMenu{STRING_FOR_POLY_FINGER_MPE};

PLACE_SDRAM_BSS submenu::Bend bendMenu{
    STRING_FOR_BEND_RANGE,
    {
        &mainBendRangeMenu,
        &perFingerBendRangeMenu,
    },
};

// Sidechain-----------------------------------------------------------------------

PLACE_SDRAM_BSS sidechain::Send sidechainSendMenu{STRING_FOR_SEND_TO_SIDECHAIN, STRING_FOR_SEND_TO_SIDECH_MENU_TITLE};
PLACE_SDRAM_BSS sidechain::VolumeShortcut sidechainVolumeShortcutMenu{
    STRING_FOR_VOLUME_DUCKING, params::GLOBAL_VOLUME_POST_REVERB_SEND, PatchSource::SIDECHAIN};
PLACE_SDRAM_BSS sidechain::Sync sidechainSyncMenu{STRING_FOR_SYNC, STRING_FOR_SIDECHAIN_SYNC, false};
PLACE_SDRAM_BSS sidechain::Attack sidechainAttackMenu{STRING_FOR_ATTACK, STRING_FOR_SIDECH_ATTACK_MENU_TITLE};
PLACE_SDRAM_BSS sidechain::Release sidechainReleaseMenu{STRING_FOR_RELEASE, STRING_FOR_SIDECH_RELEASE_MENU_TITLE};
PLACE_SDRAM_BSS sidechain::Shape sidechainShapeMenu{STRING_FOR_SHAPE, STRING_FOR_SIDECH_SHAPE_MENU_TITLE,
                                                    params::UNPATCHED_SIDECHAIN_SHAPE};

PLACE_SDRAM_BSS HorizontalMenu sidechainMenu{STRING_FOR_SIDECHAIN,
                                             STRING_FOR_SIDECHAIN,
                                             {
                                                 &sidechainVolumeShortcutMenu,
                                                 &sidechainSyncMenu,
                                                 &sidechainShapeMenu,
                                                 &sidechainSendMenu,
                                                 &sidechainAttackMenu,
                                                 &sidechainReleaseMenu,
                                             }};

// Reverb sidechain -----------------------------------------------------------------------

PLACE_SDRAM_BSS reverb::sidechain::Volume reverbSidechainVolumeMenu{STRING_FOR_VOLUME_DUCKING};
PLACE_SDRAM_BSS sidechain::Sync reverbSidechainSyncMenu{STRING_FOR_SYNC, STRING_FOR_SIDECHAIN_SYNC, true};
PLACE_SDRAM_BSS sidechain::Attack reverbSidechainAttackMenu{STRING_FOR_ATTACK, STRING_FOR_SIDECH_ATTACK_MENU_TITLE,
                                                            true};
PLACE_SDRAM_BSS sidechain::Release reverbSidechainReleaseMenu{STRING_FOR_RELEASE, STRING_FOR_SIDECH_RELEASE_MENU_TITLE,
                                                              true};
PLACE_SDRAM_BSS reverb::sidechain::Shape reverbSidechainShapeMenu{STRING_FOR_SHAPE, STRING_FOR_SIDECH_SHAPE_MENU_TITLE};

PLACE_SDRAM_BSS HorizontalMenu reverbSidechainMenu{STRING_FOR_REVERB_SIDECHAIN,
                                                   STRING_FOR_REVERB_SIDECH_MENU_TITLE,
                                                   {
                                                       &reverbSidechainVolumeMenu,
                                                       &reverbSidechainShapeMenu,
                                                       &reverbSidechainAttackMenu,
                                                       &reverbSidechainReleaseMenu,
                                                       &reverbSidechainSyncMenu,
                                                   },
                                                   HorizontalMenu::FIXED};

// Reverb ----------------------------------------------------------------------------------
PLACE_SDRAM_BSS reverb::Amount reverbAmountMenu{STRING_FOR_AMOUNT, STRING_FOR_REVERB_AMOUNT,
                                                params::GLOBAL_REVERB_AMOUNT};
PLACE_SDRAM_BSS reverb::RoomSize reverbRoomSizeMenu{STRING_FOR_ROOM_SIZE};
PLACE_SDRAM_BSS reverb::Damping reverbDampingMenu{STRING_FOR_DAMPING};
PLACE_SDRAM_BSS reverb::Width reverbWidthMenu{STRING_FOR_WIDTH, STRING_FOR_REVERB_WIDTH};
PLACE_SDRAM_BSS reverb::Pan reverbPanMenu{STRING_FOR_PAN, STRING_FOR_REVERB_PAN};
PLACE_SDRAM_BSS reverb::Model reverbModelMenu{STRING_FOR_MODEL};
PLACE_SDRAM_BSS reverb::HPF reverbHPFMenu{STRING_FOR_HPF};
PLACE_SDRAM_BSS reverb::LPF reverbLPFMenu{STRING_FOR_LPF};

PLACE_SDRAM_BSS HorizontalMenu reverbMenu{
    STRING_FOR_REVERB,
    {
        &reverbAmountMenu,
        &reverbRoomSizeMenu,
        &reverbDampingMenu,
        &reverbWidthMenu,
        &reverbModelMenu,
        &reverbPanMenu,
        &reverbHPFMenu,
        &reverbLPFMenu,
        &reverbSidechainMenu,
    },
};

PLACE_SDRAM_BSS HorizontalMenu reverbMenuWithoutSidechain{
    STRING_FOR_REVERB,
    {&reverbAmountMenu, &reverbRoomSizeMenu, &reverbDampingMenu, &reverbWidthMenu, &reverbModelMenu, &reverbPanMenu,
     &reverbHPFMenu, &reverbLPFMenu},
};

PLACE_SDRAM_BSS HorizontalMenuGroup reverbMenuGroup{{&reverbMenuWithoutSidechain, &reverbSidechainMenu}};

// Filters ------------------------------------------------------------------------------------
PLACE_SDRAM_BSS HorizontalMenu routingHorizontal{STRING_FOR_FILTER_ROUTE, {&filterRoutingMenu}};
PLACE_SDRAM_BSS HorizontalMenuGroup filtersMenuGroup{{&lpfMenu, &hpfMenu, &routingHorizontal}};

// FX ----------------------------------------------------------------------------------------
PLACE_SDRAM_BSS fx::Clipping clippingMenu{STRING_FOR_SATURATION};
PLACE_SDRAM_BSS UnpatchedParam srrMenu{STRING_FOR_DECIMATION, params::UNPATCHED_SAMPLE_RATE_REDUCTION,
                                       RenderingStyle::BAR};
PLACE_SDRAM_BSS UnpatchedParam bitcrushMenu{STRING_FOR_BITCRUSH, params::UNPATCHED_BITCRUSHING, RenderingStyle::BAR};
PLACE_SDRAM_BSS patched_param::Integer foldMenu{STRING_FOR_WAVEFOLD, STRING_FOR_WAVEFOLD, params::LOCAL_FOLD,
                                                RenderingStyle::BAR};

PLACE_SDRAM_BSS HorizontalMenu soundDistortionMenu{
    STRING_FOR_DISTORTION,
    {
        &clippingMenu,
        &bitcrushMenu,
        &srrMenu,
        &foldMenu,
    },
};

// Output MIDI for sound drums --------------------------------------------------------------
PLACE_SDRAM_BSS midi::sound::OutputMidiChannel outputMidiChannelMenu{STRING_FOR_CHANNEL, STRING_FOR_CHANNEL};
PLACE_SDRAM_BSS midi::sound::OutputMidiNoteForDrum outputMidiNoteForDrumMenu{STRING_FOR_NOTE, STRING_FOR_NOTE};
PLACE_SDRAM_BSS Submenu outputMidiSubmenu{STRING_FOR_MIDI, {&outputMidiChannelMenu, &outputMidiNoteForDrumMenu}};

// MIDIInstrument menu ----------------------------------------------------------------------
PLACE_SDRAM_BSS midi::device_definition::Linked midiDeviceLinkedMenu{STRING_FOR_MIDI_DEVICE_DEFINITION_LINKED,
                                                                     STRING_FOR_MIDI_DEVICE_DEFINITION_LINKED};

PLACE_SDRAM_BSS midi::device_definition::DeviceDefinitionSubmenu midiDeviceDefinitionMenu{
    STRING_FOR_MIDI_DEVICE_DEFINITION,
    {
        &midiDeviceLinkedMenu,
    },
};

PLACE_SDRAM_BSS midi::Bank midiBankMenu{STRING_FOR_BANK, STRING_FOR_MIDI_BANK};
PLACE_SDRAM_BSS midi::Sub midiSubMenu{STRING_FOR_SUB_BANK_SHORT, STRING_FOR_MIDI_SUB_BANK};
PLACE_SDRAM_BSS midi::PGM midiPGMMenu{STRING_FOR_PGM, STRING_FOR_MIDI_PGM_NUMB_MENU_TITLE};
PLACE_SDRAM_BSS midi::MPEYToModWheel mpeyToModWheelMenu{STRING_FOR_Y_AXIS_CONVERSION, STRING_FOR_Y_AXIS_CONVERSION};
PLACE_SDRAM_BSS cv::DualCVSelection cv2SourceMenu{STRING_FOR_CV2_SOURCE};
PLACE_SDRAM_BSS midi::AftertouchToMono midiAftertouchCollapseMenu{STRING_FOR_PATCH_SOURCE_AFTERTOUCH,
                                                                  STRING_FOR_PATCH_SOURCE_AFTERTOUCH};
PLACE_SDRAM_BSS midi::MPEToMono midiMPECollapseMenu{STRING_FOR_MPE, STRING_FOR_MPE};
PLACE_SDRAM_BSS submenu::PolyMonoConversion midiMPEMenu{STRING_FOR_MPE_MONO,
                                                        {&midiAftertouchCollapseMenu, &midiMPECollapseMenu}};

// Clip-level stuff --------------------------------------------------------------------------

PLACE_SDRAM_BSS sequence::Direction sequenceDirectionMenu{STRING_FOR_PLAY_DIRECTION};

// Global FX Menu

// Volume
PLACE_SDRAM_BSS UnpatchedParam globalLevelMenu{STRING_FOR_VOLUME_LEVEL, params::UNPATCHED_VOLUME, BAR};

// Pitch
PLACE_SDRAM_BSS UnpatchedParam globalPitchMenu{STRING_FOR_PITCH, params::UNPATCHED_PITCH_ADJUST};

// Pan
PLACE_SDRAM_BSS unpatched_param::Pan globalPanMenu{STRING_FOR_PAN, params::UNPATCHED_PAN};

PLACE_SDRAM_BSS HorizontalMenu songMasterMenu{
    STRING_FOR_MASTER,
    {
        &globalLevelMenu,
        &globalPanMenu,
    },
};

PLACE_SDRAM_BSS HorizontalMenu kitClipMasterMenu{
    STRING_FOR_MASTER,
    {
        &globalLevelMenu,
        &globalPanMenu,
        &globalPitchMenu,
    },
};

// LPF Menu
PLACE_SDRAM_BSS filter::UnpatchedFilterParam globalLPFFreqMenu{STRING_FOR_FREQUENCY, STRING_FOR_LPF_FREQUENCY,
                                                               params::UNPATCHED_LPF_FREQ, filter::FilterSlot::LPF,
                                                               filter::FilterParamType::FREQUENCY};
PLACE_SDRAM_BSS filter::UnpatchedFilterParam globalLPFResMenu{STRING_FOR_RESONANCE, STRING_FOR_LPF_RESONANCE,
                                                              params::UNPATCHED_LPF_RES, filter::FilterSlot::LPF,
                                                              filter::FilterParamType::RESONANCE};
PLACE_SDRAM_BSS filter::UnpatchedFilterParam globalLPFMorphMenu{STRING_FOR_MORPH, STRING_FOR_LPF_MORPH,
                                                                params::UNPATCHED_LPF_MORPH, filter::FilterSlot::LPF,
                                                                filter::FilterParamType::MORPH};
PLACE_SDRAM_BSS HorizontalMenu globalLPFMenu{
    STRING_FOR_LPF, {&lpfModeMenu, &globalLPFFreqMenu, &globalLPFResMenu, &globalLPFMorphMenu}};

// HPF Menu
PLACE_SDRAM_BSS filter::UnpatchedFilterParam globalHPFFreqMenu{STRING_FOR_FREQUENCY, STRING_FOR_HPF_FREQUENCY,
                                                               params::UNPATCHED_HPF_FREQ, filter::FilterSlot::HPF,
                                                               filter::FilterParamType::FREQUENCY};
PLACE_SDRAM_BSS filter::UnpatchedFilterParam globalHPFResMenu{STRING_FOR_RESONANCE, STRING_FOR_HPF_RESONANCE,
                                                              params::UNPATCHED_HPF_RES, filter::FilterSlot::HPF,
                                                              filter::FilterParamType::RESONANCE};
PLACE_SDRAM_BSS filter::UnpatchedFilterParam globalHPFMorphMenu{STRING_FOR_MORPH, STRING_FOR_HPF_MORPH,
                                                                params::UNPATCHED_HPF_MORPH, filter::FilterSlot::HPF,
                                                                filter::FilterParamType::MORPH};

PLACE_SDRAM_BSS HorizontalMenu globalHPFMenu{
    STRING_FOR_HPF, {&hpfModeMenu, &globalHPFFreqMenu, &globalHPFResMenu, &globalHPFMorphMenu}};

PLACE_SDRAM_BSS Submenu globalFiltersMenu{
    STRING_FOR_FILTERS,
    {
        &globalLPFMenu,
        &globalHPFMenu,
        &filterRoutingMenu,
    },
};

PLACE_SDRAM_BSS HorizontalMenuGroup globalFiltersMenuGroup{{&globalLPFMenu, &globalHPFMenu, &routingHorizontal}};

// EQ Menu

PLACE_SDRAM_BSS eq::EqMenu globalEQMenu{
    STRING_FOR_EQ,
    {
        &bassMenu,
        &trebleMenu,
        &bassFreqMenu,
        &trebleFreqMenu,
    },
};

// Delay Menu
PLACE_SDRAM_BSS delay::Amount_Unpatched globalDelayFeedbackMenu{STRING_FOR_AMOUNT, STRING_FOR_DELAY_AMOUNT,
                                                                params::UNPATCHED_DELAY_AMOUNT};
PLACE_SDRAM_BSS UnpatchedParam globalDelayRateMenu{STRING_FOR_RATE, STRING_FOR_DELAY_RATE,
                                                   params::UNPATCHED_DELAY_RATE};

PLACE_SDRAM_BSS HorizontalMenu globalDelayMenu{
    STRING_FOR_DELAY,
    {
        &globalDelayFeedbackMenu,
        &delayPingPongMenu,
        &delaySyncMenu,
        &globalDelayRateMenu,
        &delayAnalogMenu,
    },
};

// Reverb Menu

PLACE_SDRAM_BSS reverb::Amount_Unpatched globalReverbSendAmountMenu{
    STRING_FOR_AMOUNT,
    STRING_FOR_REVERB_AMOUNT,
    params::UNPATCHED_REVERB_SEND_AMOUNT,
};

PLACE_SDRAM_BSS HorizontalMenu globalReverbMenu{
    STRING_FOR_REVERB,
    {
        &globalReverbSendAmountMenu,
        &reverbRoomSizeMenu,
        &reverbDampingMenu,
        &reverbWidthMenu,
        &reverbModelMenu,
        &reverbPanMenu,
        &reverbHPFMenu,
        &reverbLPFMenu,
        &reverbSidechainMenu,
    },
};

PLACE_SDRAM_BSS HorizontalMenu globalReverbMenuWithoutSidechain{
    STRING_FOR_REVERB,
    {
        &globalReverbSendAmountMenu,
        &reverbRoomSizeMenu,
        &reverbDampingMenu,
        &reverbWidthMenu,
        &reverbModelMenu,
        &reverbPanMenu,
        &reverbHPFMenu,
        &reverbLPFMenu,
    },
};
PLACE_SDRAM_BSS HorizontalMenuGroup globalReverbMenuGroup{{&globalReverbMenuWithoutSidechain, &reverbSidechainMenu}};

// Mod FX Menu

PLACE_SDRAM_BSS mod_fx::Depth_Unpatched globalModFXDepthMenu{STRING_FOR_DEPTH, STRING_FOR_MOD_FX_DEPTH,
                                                             params::UNPATCHED_MOD_FX_DEPTH};
PLACE_SDRAM_BSS mod_fx::Rate_Unpatched globalModFXRateMenu{STRING_FOR_RATE, STRING_FOR_MOD_FX_RATE,
                                                           params::UNPATCHED_MOD_FX_RATE};

PLACE_SDRAM_BSS submenu::ModFxHorizontalMenu globalModFXMenu{
    STRING_FOR_MOD_FX,
    {
        &modFXTypeMenu,
        &globalModFXDepthMenu,
        &globalModFXRateMenu,
        &modFXFeedbackMenu,
        &modFXOffsetMenu,
    },
};

PLACE_SDRAM_BSS HorizontalMenu globalDistortionMenu{
    STRING_FOR_DISTORTION,
    {
        &srrMenu,
        &bitcrushMenu,
    },
};

PLACE_SDRAM_BSS Submenu globalFXMenu{
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
PLACE_SDRAM_BSS sidechain::GlobalVolume globalSidechainVolumeMenu{STRING_FOR_VOLUME_DUCKING,
                                                                  params::UNPATCHED_SIDECHAIN_VOLUME};

PLACE_SDRAM_BSS HorizontalMenu globalSidechainMenu{
    STRING_FOR_SIDECHAIN,
    {
        &globalSidechainVolumeMenu,
        &sidechainSyncMenu,
        &sidechainShapeMenu,
        &sidechainAttackMenu,
        &sidechainReleaseMenu,
    },
};

// AudioClip stuff ---------------------------------------------------------------------------

PLACE_SDRAM_BSS audio_clip::SetClipLengthEqualToSampleLength setClipLengthMenu{
    STRING_FOR_SET_CLIP_LENGTH_EQUAL_TO_SAMPLE_LENGTH};

PLACE_SDRAM_BSS Submenu audioClipActionsMenu{
    STRING_FOR_ACTIONS,
    {
        &setClipLengthMenu,
    },
};

PLACE_SDRAM_BSS audio_clip::AudioSourceSelector audioSourceSelectorMenu{STRING_FOR_AUDIO_SOURCE};
PLACE_SDRAM_BSS audio_clip::SpecificSourceOutputSelector specificOutputSelectorMenu{STRING_FOR_TRACK};
PLACE_SDRAM_BSS audio_clip::Transpose audioClipTransposeMenu{STRING_FOR_TRANSPOSE};

PLACE_SDRAM_BSS HorizontalMenu audioClipMasterMenu{
    STRING_FOR_MASTER,
    {&globalLevelMenu, &globalPanMenu},
};

PLACE_SDRAM_BSS HorizontalMenu audioClipDistortionMenu{
    STRING_FOR_DISTORTION,
    {
        &clippingMenu,
        &bitcrushMenu,
        &srrMenu,
    },
};

PLACE_SDRAM_BSS Submenu audioClipFXMenu{
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
PLACE_SDRAM_BSS audio_clip::Reverse audioClipReverseMenu{STRING_FOR_REVERSE};
PLACE_SDRAM_BSS audio_clip::SampleMarkerEditor audioClipSampleMarkerEditorMenuStart{EMPTY_STRING, MarkerType::START};
PLACE_SDRAM_BSS audio_clip::SampleMarkerEditor audioClipSampleMarkerEditorMenuEnd{STRING_FOR_WAVEFORM, MarkerType::END};
PLACE_SDRAM_BSS AudioInterpolation audioClipInterpolationMenu{STRING_FOR_INTERPOLATION, STRING_FOR_AUDIO_INTERPOLATION};

PLACE_SDRAM_BSS HorizontalMenu audioClipSampleMenu{
    STRING_FOR_SAMPLE,
    {
        &file0SelectorMenu,
        &audioClipTransposeMenu,
        &audioClipReverseMenu,
        &sample0PitchSpeedMenu,
        &audioClipSampleMarkerEditorMenuEnd,
        &audioClipInterpolationMenu,
    },
};

PLACE_SDRAM_BSS audio_clip::Attack audioClipAttackMenu{STRING_FOR_ATTACK};

PLACE_SDRAM_BSS menu_item::EditName nameEditMenu{STRING_FOR_RENAME_CLIP};

PLACE_SDRAM_RODATA const MenuItem* midiOrCVParamShortcuts[kDisplayHeight] = {
    &arpRateMenuMIDIOrCV,
    &arpSyncMenu,
    &arpGateMenuMIDIOrCV,
    &arpOctavesMenu,
    &arpPresetModeMenu,
    &nameEditMenu,
    nullptr,
    nullptr,
};

PLACE_SDRAM_RODATA const MenuItem* gateDrumParamShortcuts[8] = {
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
PLACE_SDRAM_BSS gate::Mode gateModeMenu;
PLACE_SDRAM_BSS gate::OffTime gateOffTimeMenu{EMPTY_STRING, STRING_FOR_MINIMUM_OFF_TIME};

// Root menu

// CV Menu
PLACE_SDRAM_BSS cv::Volts cvVoltsMenu{STRING_FOR_VOLTS_PER_OCTAVE, STRING_FOR_CV_V_PER_OCTAVE_MENU_TITLE};
PLACE_SDRAM_BSS cv::Transpose cvTransposeMenu{STRING_FOR_TRANSPOSE, STRING_FOR_CV_TRANSPOSE_MENU_TITLE};

PLACE_SDRAM_BSS cv::Submenu cvSubmenu{STRING_FOR_CV_OUTPUT_N, {&cvVoltsMenu, &cvTransposeMenu}};

PLACE_SDRAM_BSS cv::Selection cvSelectionMenu{STRING_FOR_CV, STRING_FOR_CV_OUTPUTS};
PLACE_SDRAM_BSS gate::Selection gateSelectionMenu{STRING_FOR_GATE, STRING_FOR_GATE_OUTPUTS};

PLACE_SDRAM_BSS swing::Interval swingIntervalMenu{STRING_FOR_SWING_INTERVAL};

// Pads menu
PLACE_SDRAM_BSS shortcuts::Version shortcutsVersionMenu{STRING_FOR_SHORTCUTS_VERSION,
                                                        STRING_FOR_SHORTCUTS_VER_MENU_TITLE};
PLACE_SDRAM_BSS menu_item::keyboard::Layout keyboardLayoutMenu{STRING_FOR_KEYBOARD_FOR_TEXT, STRING_FOR_KEY_LAYOUT};

// Colours submenu
PLACE_SDRAM_BSS Submenu coloursSubmenu{
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

PLACE_SDRAM_BSS Submenu padsSubmenu{
    STRING_FOR_PADS,
    {
        &shortcutsVersionMenu,
        &keyboardLayoutMenu,
        &coloursSubmenu,
    },
};

// Record submenu
PLACE_SDRAM_BSS record::Quantize recordQuantizeMenu{STRING_FOR_QUANTIZATION};
PLACE_SDRAM_BSS ToggleBool recordMarginsMenu{STRING_FOR_LOOP_MARGINS, STRING_FOR_LOOP_MARGINS,
                                             FlashStorage::audioClipRecordMargins};
PLACE_SDRAM_BSS record::CountIn recordCountInMenu{STRING_FOR_COUNT_IN, STRING_FOR_REC_COUNT_IN};
PLACE_SDRAM_BSS monitor::Mode monitorModeMenu{STRING_FOR_SAMPLING_MONITORING, STRING_FOR_MONITORING};

PLACE_SDRAM_BSS record::ThresholdMode defaultThresholdRecordingModeMenu{STRING_FOR_MODE,
                                                                        record::ThresholdMode::DEFAULT};

PLACE_SDRAM_BSS Submenu defaultThresholdRecordingSubmenu{
    STRING_FOR_THRESHOLD_RECORDING,
    {
        &defaultThresholdRecordingModeMenu,
    },
};

PLACE_SDRAM_BSS record::LoopCommand defaultLoopCommandMenu{STRING_FOR_LOOP_COMMAND, STRING_FOR_LOOP_COMMAND};

PLACE_SDRAM_BSS Submenu recordSubmenu{
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

PLACE_SDRAM_BSS sample::browser_preview::Mode sampleBrowserPreviewModeMenu{STRING_FOR_SAMPLE_PREVIEW};

PLACE_SDRAM_BSS flash::Status flashStatusMenu{STRING_FOR_PLAY_CURSOR};

PLACE_SDRAM_BSS firmware::Version firmwareVersionMenu{STRING_FOR_FIRMWARE_VERSION, STRING_FOR_FIRMWARE_VER_MENU_TITLE};

PLACE_SDRAM_BSS runtime_feature::Settings runtimeFeatureSettingsMenu{STRING_FOR_COMMUNITY_FTS,
                                                                     STRING_FOR_COMMUNITY_FTS_MENU_TITLE};

// CV menu

// MIDI
// MIDI thru
PLACE_SDRAM_BSS ToggleBool midiThruMenu{STRING_FOR_MIDI_THRU, STRING_FOR_MIDI_THRU, midiEngine.midiThru};

// MIDI Takeover
PLACE_SDRAM_BSS midi::Takeover midiTakeoverMenu{STRING_FOR_TAKEOVER};

// MIDI Follow
PLACE_SDRAM_BSS midi::FollowChannel midiFollowChannelAMenu{STRING_FOR_FOLLOW_CHANNEL_A, STRING_FOR_FOLLOW_CHANNEL_A,
                                                           MIDIFollowChannelType::A};
PLACE_SDRAM_BSS midi::FollowChannel midiFollowChannelBMenu{STRING_FOR_FOLLOW_CHANNEL_B, STRING_FOR_FOLLOW_CHANNEL_B,
                                                           MIDIFollowChannelType::B};
PLACE_SDRAM_BSS midi::FollowChannel midiFollowChannelCMenu{STRING_FOR_FOLLOW_CHANNEL_C, STRING_FOR_FOLLOW_CHANNEL_C,
                                                           MIDIFollowChannelType::C};
PLACE_SDRAM_BSS midi::FollowKitRootNote midiFollowKitRootNoteMenu{STRING_FOR_FOLLOW_KIT_ROOT_NOTE};
PLACE_SDRAM_BSS ToggleBool midiFollowDisplayParamMenu{STRING_FOR_FOLLOW_DISPLAY_PARAM, STRING_FOR_FOLLOW_DISPLAY_PARAM,
                                                      midiEngine.midiFollowDisplayParam};
PLACE_SDRAM_BSS midi::FollowFeedbackChannelType midiFollowFeedbackChannelMenu{STRING_FOR_CHANNEL};
PLACE_SDRAM_BSS midi::FollowFeedbackAutomation midiFollowFeedbackAutomationMenu{STRING_FOR_FOLLOW_FEEDBACK_AUTOMATION};
PLACE_SDRAM_BSS ToggleBool midiFollowFeedbackFilterMenu{
    STRING_FOR_FOLLOW_FEEDBACK_FILTER, STRING_FOR_FOLLOW_FEEDBACK_FILTER, midiEngine.midiFollowFeedbackFilter};

PLACE_SDRAM_BSS Submenu midiFollowChannelSubmenu{
    STRING_FOR_CHANNEL,
    STRING_FOR_CHANNEL,
    {
        &midiFollowChannelAMenu,
        &midiFollowChannelBMenu,
        &midiFollowChannelCMenu,
    },
};

PLACE_SDRAM_BSS Submenu midiFollowFeedbackSubmenu{
    STRING_FOR_FOLLOW_FEEDBACK,
    STRING_FOR_FOLLOW_FEEDBACK,
    {
        &midiFollowFeedbackChannelMenu,
        &midiFollowFeedbackAutomationMenu,
        &midiFollowFeedbackFilterMenu,
    },
};

PLACE_SDRAM_BSS Submenu midiFollowSubmenu{
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
PLACE_SDRAM_BSS ToggleBool midiSelectKitRowMenu{STRING_FOR_SELECT_KIT_ROW, STRING_FOR_SELECT_KIT_ROW,
                                                midiEngine.midiSelectKitRow};

// MIDI transpose menu

PLACE_SDRAM_BSS midi::Transpose midiTransposeMenu{STRING_FOR_TRANSPOSE};

PLACE_SDRAM_BSS Submenu midiTransposeSubmenu{
    STRING_FOR_TRANSPOSE,
    STRING_FOR_TRANSPOSE,
    {
        &midiTransposeMenu,
    },
};

// MIDI commands submenu
PLACE_SDRAM_BSS midi::Command playbackRestartMidiCommand{STRING_FOR_RESTART, GlobalMIDICommand::PLAYBACK_RESTART};
PLACE_SDRAM_BSS midi::Command playMidiCommand{STRING_FOR_PLAY, GlobalMIDICommand::PLAY};
PLACE_SDRAM_BSS midi::Command recordMidiCommand{STRING_FOR_RECORD, GlobalMIDICommand::RECORD};
PLACE_SDRAM_BSS midi::Command tapMidiCommand{STRING_FOR_TAP_TEMPO, GlobalMIDICommand::TAP};
PLACE_SDRAM_BSS midi::Command undoMidiCommand{STRING_FOR_UNDO, GlobalMIDICommand::UNDO};
PLACE_SDRAM_BSS midi::Command redoMidiCommand{STRING_FOR_REDO, GlobalMIDICommand::REDO};
PLACE_SDRAM_BSS midi::Command loopMidiCommand{STRING_FOR_LOOP, GlobalMIDICommand::LOOP};
PLACE_SDRAM_BSS midi::Command loopContinuousLayeringMidiCommand{STRING_FOR_LAYERING_LOOP,
                                                                GlobalMIDICommand::LOOP_CONTINUOUS_LAYERING};
PLACE_SDRAM_BSS midi::Command fillMidiCommand{STRING_FOR_FILL, GlobalMIDICommand::FILL};
PLACE_SDRAM_BSS midi::Command transposeMidiCommand{STRING_FOR_TRANSPOSE, GlobalMIDICommand::TRANSPOSE};
PLACE_SDRAM_BSS midi::Command nextSongMidiCommand{STRING_FOR_SONG_LOAD_NEXT, GlobalMIDICommand::NEXT_SONG};

PLACE_SDRAM_BSS Submenu midiCommandsMenu{
    STRING_FOR_COMMANDS,
    STRING_FOR_MIDI_COMMANDS,
    {&playMidiCommand, &playbackRestartMidiCommand, &recordMidiCommand, &tapMidiCommand, &undoMidiCommand,
     &redoMidiCommand, &loopMidiCommand, &loopContinuousLayeringMidiCommand, &fillMidiCommand, &transposeMidiCommand,
     &nextSongMidiCommand},
};

// MIDI device submenu - for after we've selected which device we want it for

PLACE_SDRAM_BSS midi::DefaultVelocityToLevel defaultVelocityToLevelMenu{STRING_FOR_VELOCITY};
PLACE_SDRAM_BSS midi::SendClock sendClockMenu{STRING_FOR_CLOCK_OUT};
PLACE_SDRAM_BSS midi::ReceiveClock receiveClockMenu{STRING_FOR_CLOCK_IN};
PLACE_SDRAM_BSS midi::Device midiDeviceMenu{
    EMPTY_STRING,
    {
        &mpe::directionSelectorMenu,
        &defaultVelocityToLevelMenu,
        &sendClockMenu,
        &receiveClockMenu,
    },
};

// MIDI input differentiation menu
PLACE_SDRAM_BSS ToggleBool midiInputDifferentiationMenu{
    STRING_FOR_DIFFERENTIATE_INPUTS, STRING_FOR_DIFFERENTIATE_INPUTS, MIDIDeviceManager::differentiatingInputsByDevice};

// MIDI clock menu
PLACE_SDRAM_BSS ToggleBool midiClockOutStatusMenu{STRING_FOR_OUTPUT, STRING_FOR_MIDI_CLOCK_OUT,
                                                  playbackHandler.midiOutClockEnabled};
PLACE_SDRAM_BSS ToggleBool midiClockInStatusMenu{STRING_FOR_INPUT, STRING_FOR_MIDI_CLOCK_IN,
                                                 playbackHandler.midiInClockEnabled};
PLACE_SDRAM_BSS ToggleBool tempoMagnitudeMatchingMenu{STRING_FOR_TEMPO_MAGNITUDE_MATCHING,
                                                      STRING_FOR_TEMPO_MAGNITUDE_MATCHING,
                                                      playbackHandler.tempoMagnitudeMatchingEnabled};

// Midi devices menu
PLACE_SDRAM_BSS midi::Devices midi::devicesMenu{STRING_FOR_DEVICES, STRING_FOR_MIDI_DEVICES};
PLACE_SDRAM_BSS mpe::DirectionSelector mpe::directionSelectorMenu{STRING_FOR_MPE};

// MIDI menu
PLACE_SDRAM_BSS Submenu midiClockMenu{
    STRING_FOR_CLOCK,
    STRING_FOR_MIDI_CLOCK,
    {
        &midiClockInStatusMenu,
        &midiClockOutStatusMenu,
        &tempoMagnitudeMatchingMenu,
    },
};

PLACE_SDRAM_BSS Submenu midiMenu{
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
PLACE_SDRAM_BSS trigger::in::PPQN triggerInPPQNMenu{STRING_FOR_PPQN, STRING_FOR_INPUT_PPQN};
PLACE_SDRAM_BSS ToggleBool triggerInAutoStartMenu{STRING_FOR_AUTO_START, STRING_FOR_AUTO_START,
                                                  playbackHandler.analogClockInputAutoStart};
PLACE_SDRAM_BSS Submenu triggerClockInMenu{
    STRING_FOR_INPUT,
    STRING_FOR_T_CLOCK_INPUT_MENU_TITLE,
    {
        &triggerInPPQNMenu,
        &triggerInAutoStartMenu,
    },
};

// Trigger clock out menu
PLACE_SDRAM_BSS trigger::out::PPQN triggerOutPPQNMenu{STRING_FOR_PPQN, STRING_FOR_OUTPUT_PPQN};
PLACE_SDRAM_BSS Submenu triggerClockOutMenu{
    STRING_FOR_OUTPUT,
    STRING_FOR_T_CLOCK_OUT_MENU_TITLE,
    {
        &triggerOutPPQNMenu,
    },
};

// Trigger clock menu
PLACE_SDRAM_BSS Submenu triggerClockMenu{
    STRING_FOR_TRIGGER_CLOCK,
    {
        &triggerClockInMenu,
        &triggerClockOutMenu,
    },
};

// Defaults menu
PLACE_SDRAM_BSS defaults::KeyboardLayout defaultKeyboardLayoutMenu{STRING_FOR_DEFAULT_UI_LAYOUT,
                                                                   STRING_FOR_DEFAULT_UI_LAYOUT};

PLACE_SDRAM_BSS defaults::DefaultFavouritesLayout defaultFavouritesLayout{STRING_FOR_DEFAULT_UI_FAVOURITES,
                                                                          STRING_FOR_DEFAULT_UI_FAVOURITES};

PLACE_SDRAM_BSS InvertedToggleBool defaultUIKeyboardFunctionsVelocityGlide{
    STRING_FOR_DEFAULT_UI_KB_CONTROLS_VELOCITY_MOMENTARY, STRING_FOR_DEFAULT_UI_KB_CONTROLS_VELOCITY_MOMENTARY,
    // This control is inverted, as the default value is true
    // (Enabled) Glide mode is the opposite to Momentary mode
    FlashStorage::keyboardFunctionsVelocityGlide};
PLACE_SDRAM_BSS InvertedToggleBool defaultUIKeyboardFunctionsModwheelGlide{
    STRING_FOR_DEFAULT_UI_KB_CONTROLS_MODWHEEL_MOMENTARY, STRING_FOR_DEFAULT_UI_KB_CONTROLS_MODWHEEL_MOMENTARY,
    // This control is inverted, as the default value is true
    // (Enabled) Glide mode is the opposite to Momentary mode
    FlashStorage::keyboardFunctionsModwheelGlide};
PLACE_SDRAM_BSS Submenu defaultKeyboardFunctionsMenu{
    STRING_FOR_DEFAULT_UI_KB_CONTROLS,
    {&defaultUIKeyboardFunctionsVelocityGlide, &defaultUIKeyboardFunctionsModwheelGlide},
};

PLACE_SDRAM_BSS Submenu defaultUIKeyboard{
    STRING_FOR_DEFAULT_UI_KEYBOARD,
    {&defaultKeyboardLayoutMenu, &defaultKeyboardFunctionsMenu, &defaultFavouritesLayout},
};

PLACE_SDRAM_BSS ToggleBool defaultgridEmptyPadsUnarm{STRING_FOR_DEFAULT_UI_DEFAULT_GRID_EMPTY_PADS_UNARM,
                                                     STRING_FOR_DEFAULT_UI_DEFAULT_GRID_EMPTY_PADS_UNARM,
                                                     FlashStorage::gridEmptyPadsUnarm};
PLACE_SDRAM_BSS ToggleBool defaultGridEmptyPadsCreateRec{STRING_FOR_DEFAULT_UI_DEFAULT_GRID_EMPTY_PADS_CREATE_REC,
                                                         STRING_FOR_DEFAULT_UI_DEFAULT_GRID_EMPTY_PADS_CREATE_REC,
                                                         FlashStorage::gridEmptyPadsCreateRec};
PLACE_SDRAM_BSS Submenu defaultEmptyPadMenu{
    STRING_FOR_DEFAULT_UI_DEFAULT_GRID_EMPTY_PADS,
    {&defaultgridEmptyPadsUnarm, &defaultGridEmptyPadsCreateRec},
};

PLACE_SDRAM_BSS defaults::DefaultGridDefaultActiveMode defaultGridDefaultActiveMode{
    STRING_FOR_DEFAULT_UI_DEFAULT_GRID_ACTIVE_MODE, STRING_FOR_DEFAULT_UI_DEFAULT_GRID_ACTIVE_MODE};
PLACE_SDRAM_BSS ToggleBool defaultGridAllowGreenSelection{STRING_FOR_DEFAULT_UI_DEFAULT_GRID_ALLOW_GREEN_SELECTION,
                                                          STRING_FOR_DEFAULT_UI_DEFAULT_GRID_ALLOW_GREEN_SELECTION,
                                                          FlashStorage::gridAllowGreenSelection};
PLACE_SDRAM_BSS Submenu defaultSessionGridMenu{
    STRING_FOR_DEFAULT_UI_GRID,
    {&defaultGridDefaultActiveMode, &defaultGridAllowGreenSelection, &defaultEmptyPadMenu},
};

PLACE_SDRAM_BSS defaults::SessionLayout defaultSessionLayoutMenu{STRING_FOR_DEFAULT_UI_LAYOUT,
                                                                 STRING_FOR_DEFAULT_UI_LAYOUT};

PLACE_SDRAM_BSS Submenu defaultUISession{
    STRING_FOR_DEFAULT_UI_SONG,
    {&defaultSessionLayoutMenu, &defaultSessionGridMenu},
};

PLACE_SDRAM_BSS ToggleBool defaultAccessibilityShortcuts{STRING_FOR_DEFAULT_ACCESSIBILITY_SHORTCUTS,
                                                         STRING_FOR_DEFAULT_ACCESSIBILITY_SHORTCUTS,
                                                         FlashStorage::accessibilityShortcuts};

PLACE_SDRAM_BSS defaults::AccessibilityMenuHighlighting defaultAccessibilityMenuHighlighting{
    STRING_FOR_DEFAULT_ACCESSIBILITY_MENU_HIGHLIGHTING, STRING_FOR_DEFAULT_ACCESSIBILITY_MENU_HIGHLIGHTING};

PLACE_SDRAM_BSS Submenu defaultAccessibilityMenu{STRING_FOR_DEFAULT_ACCESSIBILITY,
                                                 {
                                                     &defaultAccessibilityShortcuts,
                                                     &defaultAccessibilityMenuHighlighting,
                                                 }};

PLACE_SDRAM_BSS defaults::ui::clip_type::DefaultNewClipType defaultNewClipTypeMenu{STRING_FOR_DEFAULT_NEW_CLIP_TYPE,
                                                                                   STRING_FOR_DEFAULT_NEW_CLIP_TYPE};
PLACE_SDRAM_BSS ToggleBool defaultUseLastClipTypeMenu{
    STRING_FOR_DEFAULT_USE_LAST_CLIP_TYPE, STRING_FOR_DEFAULT_USE_LAST_CLIP_TYPE, FlashStorage::defaultUseLastClipType};

PLACE_SDRAM_BSS Submenu defaultClipTypeMenu{STRING_FOR_DEFAULT_CLIP_TYPE,
                                            {
                                                &defaultNewClipTypeMenu,
                                                &defaultUseLastClipTypeMenu,
                                            }};

PLACE_SDRAM_BSS ToggleBool defaultUseSharps{STRING_FOR_DEFAULT_UI_SHARPS, STRING_FOR_DEFAULT_UI_SHARPS,
                                            FlashStorage::defaultUseSharps};

PLACE_SDRAM_BSS Submenu defaultUI{
    STRING_FOR_DEFAULT_UI,
    {&defaultAccessibilityMenu, &defaultUISession, &defaultUIKeyboard, &defaultClipTypeMenu, &defaultUseSharps},
};

PLACE_SDRAM_BSS ToggleBool defaultAutomationInterpolateMenu{STRING_FOR_DEFAULT_AUTOMATION_INTERPOLATION,
                                                            STRING_FOR_DEFAULT_AUTOMATION_INTERPOLATION,
                                                            FlashStorage::automationInterpolate};

PLACE_SDRAM_BSS ToggleBool defaultAutomationClearMenu{
    STRING_FOR_DEFAULT_AUTOMATION_CLEAR, STRING_FOR_DEFAULT_AUTOMATION_CLEAR, FlashStorage::automationClear};

PLACE_SDRAM_BSS ToggleBool defaultAutomationShiftMenu{
    STRING_FOR_DEFAULT_AUTOMATION_SHIFT, STRING_FOR_DEFAULT_AUTOMATION_SHIFT, FlashStorage::automationShift};

PLACE_SDRAM_BSS ToggleBool defaultAutomationNudgeNoteMenu{STRING_FOR_DEFAULT_AUTOMATION_NUDGE_NOTE,
                                                          STRING_FOR_DEFAULT_AUTOMATION_NUDGE_NOTE,
                                                          FlashStorage::automationNudgeNote};

PLACE_SDRAM_BSS ToggleBool defaultAutomationDisableAuditionPadShortcutsMenu{
    STRING_FOR_DEFAULT_AUTOMATION_DISABLE_AUDITION_PAD_SHORTCUTS,
    STRING_FOR_DEFAULT_AUTOMATION_DISABLE_AUDITION_PAD_SHORTCUTS, FlashStorage::automationDisableAuditionPadShortcuts};

PLACE_SDRAM_BSS Submenu defaultAutomationMenu{
    STRING_FOR_AUTOMATION,
    {
        &defaultAutomationInterpolateMenu,
        &defaultAutomationClearMenu,
        &defaultAutomationShiftMenu,
        &defaultAutomationNudgeNoteMenu,
        &defaultAutomationDisableAuditionPadShortcutsMenu,
    },
};

PLACE_SDRAM_BSS IntegerRange defaultTempoMenu{STRING_FOR_TEMPO, STRING_FOR_DEFAULT_TEMPO, 60, 240};
PLACE_SDRAM_BSS IntegerRange defaultSwingAmountMenu{STRING_FOR_SWING_AMOUNT, STRING_FOR_DEFAULT_SWING, 1, 99};
PLACE_SDRAM_BSS defaults::SwingInterval defaultSwingIntervalMenu{STRING_FOR_SWING_INTERVAL, STRING_FOR_DEFAULT_SWING};
PLACE_SDRAM_BSS KeyRange defaultKeyMenu{STRING_FOR_KEY, STRING_FOR_DEFAULT_KEY};
PLACE_SDRAM_BSS defaults::DefaultScale defaultScaleMenu{STRING_FOR_INIT_SCALE};
PLACE_SDRAM_BSS defaults::Velocity defaultVelocityMenu{STRING_FOR_VELOCITY, STRING_FOR_DEFAULT_VELOC_MENU_TITLE};
PLACE_SDRAM_BSS defaults::Magnitude defaultMagnitudeMenu{STRING_FOR_RESOLUTION, STRING_FOR_DEFAULT_RESOL_MENU_TITLE};
PLACE_SDRAM_BSS defaults::BendRange defaultBendRangeMenu{STRING_FOR_BEND_RANGE, STRING_FOR_DEFAULT_BEND_R};
PLACE_SDRAM_BSS defaults::MetronomeVolume defaultMetronomeVolumeMenu{STRING_FOR_METRONOME,
                                                                     STRING_FOR_DEFAULT_METRO_MENU_TITLE};
PLACE_SDRAM_BSS defaults::PatchCablePolarity defaultPatchCablePolarityMenu{STRING_FOR_DEFAULT_POLARITY,
                                                                           STRING_FOR_DEFAULT_POLARITY};
PLACE_SDRAM_BSS defaults::StartupSongModeMenu defaultStartupSongMenu{STRING_FOR_DEFAULT_UI_DEFAULT_STARTUP_SONG_MODE,
                                                                     STRING_FOR_DEFAULT_UI_DEFAULT_STARTUP_SONG_MODE};
PLACE_SDRAM_BSS defaults::PadBrightness defaultPadBrightness{STRING_FOR_DEFAULT_PAD_BRIGHTNESS,
                                                             STRING_FOR_DEFAULT_PAD_BRIGHTNESS_MENU_TITLE};
PLACE_SDRAM_BSS defaults::SliceMode defaultSliceMode{STRING_FOR_DEFAULT_SLICE_MODE,
                                                     STRING_FOR_DEFAULT_SLICE_MODE_MENU_TITLE};
PLACE_SDRAM_BSS ToggleBool defaultHighCPUUsageIndicatorMode{STRING_FOR_DEFAULT_HIGH_CPU_USAGE_INDICATOR,
                                                            STRING_FOR_DEFAULT_HIGH_CPU_USAGE_INDICATOR,
                                                            FlashStorage::highCPUUsageIndicator};
PLACE_SDRAM_BSS defaults::HoldTime defaultHoldTimeMenu{STRING_FOR_HOLD_TIME, STRING_FOR_HOLD_TIME};

PLACE_SDRAM_BSS ActiveScaleMenu defaultActiveScaleMenu{STRING_FOR_ACTIVE_SCALES, ActiveScaleMenu::DEFAULT};

PLACE_SDRAM_BSS Submenu defaultScalesSubmenu{STRING_FOR_SCALE,
                                             {
                                                 &defaultScaleMenu,
                                                 &defaultActiveScaleMenu,
                                             }};

PLACE_SDRAM_BSS Submenu defaultsSubmenu{
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
        &defaultPatchCablePolarityMenu,
        &defaultStartupSongMenu,
        &defaultPadBrightness,
        &defaultSliceMode,
        &defaultHighCPUUsageIndicatorMode,
        &defaultHoldTimeMenu,
    },
};

// Sound editor menu -----------------------------------------------------------------------------

// FM only
PLACE_SDRAM_RODATA std::array<MenuItem*, 3> dxMenuItems = {
    &dxBrowseMenu,
    &dxGlobalParams,
    &dxEngineSelect,
};

PLACE_SDRAM_BSS menu_item::Submenu dxMenu{STRING_FOR_DX_1, dxMenuItems};

// Not FM
PLACE_SDRAM_BSS MasterTranspose masterTransposeMenu{STRING_FOR_MASTER_TRANSPOSE, STRING_FOR_MASTER_TRAN_MENU_TITLE};

PLACE_SDRAM_BSS patch_cable_strength::Fixed vibratoMenu{STRING_FOR_VIBRATO, params::LOCAL_PITCH_ADJUST,
                                                        PatchSource::LFO_GLOBAL_1};

// Synth only
PLACE_SDRAM_BSS SynthModeSelection synthModeMenu{STRING_FOR_SYNTH_MODE};
PLACE_SDRAM_BSS bend_range::PerFinger drumBendRangeMenu{STRING_FOR_BEND_RANGE}; // The single option available for Drums
PLACE_SDRAM_BSS patched_param::Integer volumeMenu{STRING_FOR_VOLUME_LEVEL, STRING_FOR_MASTER_LEVEL,
                                                  params::GLOBAL_VOLUME_POST_FX, BAR};
PLACE_SDRAM_BSS patched_param::Pan panMenu{STRING_FOR_PAN, params::LOCAL_PAN};

PLACE_SDRAM_BSS PatchCables patchCablesMenu{STRING_FOR_MOD_MATRIX};

PLACE_SDRAM_BSS HorizontalMenu soundMasterMenu{
    STRING_FOR_MASTER,
    {&synthModeMenu, &volumeMenu, &panMenu, &masterTransposeMenu, &vibratoMenu},
};

PLACE_SDRAM_BSS HorizontalMenu soundMasterMenuWithoutVibrato{
    STRING_FOR_MASTER,
    {&synthModeMenu, &volumeMenu, &panMenu, &masterTransposeMenu},
};

PLACE_SDRAM_BSS HorizontalMenuGroup sourceMenuGroup{
    {&source0Menu, &source1Menu, &modulator0Menu, &modulator1Menu, &oscMixerMenu}};

PLACE_SDRAM_BSS Submenu soundFXMenu{
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

PLACE_SDRAM_BSS Submenu soundEditorRootActionsMenu{
    STRING_FOR_ACTIONS,
    {&nameEditMenu, &sample0RecorderMenu, &sample1RecorderMenu},
};

PLACE_SDRAM_BSS Submenu soundEditorRootMenu{
    STRING_FOR_SOUND,
    {
        &soundEditorRootActionsMenu,
        &soundMasterMenu,
        &arpMenu,
        &randomizerMenu,
        &audioCompMenu,
        &soundFiltersMenu,
        &soundFXMenu,
        &sidechainMenu,
        &source0Menu,
        &source1Menu,
        &modulator0Menu,
        &modulator1Menu,
        &oscMixerMenu,
        &env1Menu,
        &env2Menu,
        &env3Menu,
        &env4Menu,
        &lfo1Menu,
        &lfo2Menu,
        &lfo3Menu,
        &lfo4Menu,
        &voiceMenu,
        &bendMenu,
        &drumBendRangeMenu,
        &patchCablesMenu,
        &sequenceDirectionMenu,
        &outputMidiSubmenu,
    },
};

PLACE_SDRAM_BSS menu_item::note::IteranceDivisor noteCustomIteranceDivisor{STRING_FOR_ITERANCE_DIVISOR};
PLACE_SDRAM_BSS menu_item::note::IteranceStepToggle noteCustomIteranceStep1{STRING_FOR_ITERATION_STEP_1,
                                                                            STRING_FOR_ITERATION_STEP_1, 0};
PLACE_SDRAM_BSS menu_item::note::IteranceStepToggle noteCustomIteranceStep2{STRING_FOR_ITERATION_STEP_2,
                                                                            STRING_FOR_ITERATION_STEP_2, 1};
PLACE_SDRAM_BSS menu_item::note::IteranceStepToggle noteCustomIteranceStep3{STRING_FOR_ITERATION_STEP_3,
                                                                            STRING_FOR_ITERATION_STEP_3, 2};
PLACE_SDRAM_BSS menu_item::note::IteranceStepToggle noteCustomIteranceStep4{STRING_FOR_ITERATION_STEP_4,
                                                                            STRING_FOR_ITERATION_STEP_4, 3};
PLACE_SDRAM_BSS menu_item::note::IteranceStepToggle noteCustomIteranceStep5{STRING_FOR_ITERATION_STEP_5,
                                                                            STRING_FOR_ITERATION_STEP_5, 4};
PLACE_SDRAM_BSS menu_item::note::IteranceStepToggle noteCustomIteranceStep6{STRING_FOR_ITERATION_STEP_6,
                                                                            STRING_FOR_ITERATION_STEP_6, 5};
PLACE_SDRAM_BSS menu_item::note::IteranceStepToggle noteCustomIteranceStep7{STRING_FOR_ITERATION_STEP_7,
                                                                            STRING_FOR_ITERATION_STEP_7, 6};
PLACE_SDRAM_BSS menu_item::note::IteranceStepToggle noteCustomIteranceStep8{STRING_FOR_ITERATION_STEP_8,
                                                                            STRING_FOR_ITERATION_STEP_8, 7};

// Root menu for note custom iterance
PLACE_SDRAM_BSS menu_item::Submenu noteCustomIteranceRootMenu{
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

PLACE_SDRAM_BSS menu_item::note::Velocity noteVelocityMenu{STRING_FOR_NOTE_EDITOR_VELOCITY};
PLACE_SDRAM_BSS menu_item::note::Probability noteProbabilityMenu{STRING_FOR_NOTE_EDITOR_PROBABILITY};
PLACE_SDRAM_BSS menu_item::note::IterancePreset noteIteranceMenu{STRING_FOR_NOTE_EDITOR_ITERANCE};
PLACE_SDRAM_BSS menu_item::note::Fill noteFillMenu{STRING_FOR_NOTE_EDITOR_FILL};

// Root menu for Note Editor
PLACE_SDRAM_BSS HorizontalMenu noteEditorRootMenu{STRING_FOR_NOTE_EDITOR,
                                                  {
                                                      &noteVelocityMenu,
                                                      &noteProbabilityMenu,
                                                      &noteIteranceMenu,
                                                      &noteFillMenu,
                                                  }};

PLACE_SDRAM_BSS menu_item::note_row::IteranceDivisor noteRowCustomIteranceDivisor{STRING_FOR_ITERANCE_DIVISOR};
PLACE_SDRAM_BSS menu_item::note_row::IteranceStepToggle noteRowCustomIteranceStep1{STRING_FOR_ITERATION_STEP_1,
                                                                                   STRING_FOR_ITERATION_STEP_1, 0};
PLACE_SDRAM_BSS menu_item::note_row::IteranceStepToggle noteRowCustomIteranceStep2{STRING_FOR_ITERATION_STEP_2,
                                                                                   STRING_FOR_ITERATION_STEP_2, 1};
PLACE_SDRAM_BSS menu_item::note_row::IteranceStepToggle noteRowCustomIteranceStep3{STRING_FOR_ITERATION_STEP_3,
                                                                                   STRING_FOR_ITERATION_STEP_3, 2};
PLACE_SDRAM_BSS menu_item::note_row::IteranceStepToggle noteRowCustomIteranceStep4{STRING_FOR_ITERATION_STEP_4,
                                                                                   STRING_FOR_ITERATION_STEP_4, 3};
PLACE_SDRAM_BSS menu_item::note_row::IteranceStepToggle noteRowCustomIteranceStep5{STRING_FOR_ITERATION_STEP_5,
                                                                                   STRING_FOR_ITERATION_STEP_5, 4};
PLACE_SDRAM_BSS menu_item::note_row::IteranceStepToggle noteRowCustomIteranceStep6{STRING_FOR_ITERATION_STEP_6,
                                                                                   STRING_FOR_ITERATION_STEP_6, 5};
PLACE_SDRAM_BSS menu_item::note_row::IteranceStepToggle noteRowCustomIteranceStep7{STRING_FOR_ITERATION_STEP_7,
                                                                                   STRING_FOR_ITERATION_STEP_7, 6};
PLACE_SDRAM_BSS menu_item::note_row::IteranceStepToggle noteRowCustomIteranceStep8{STRING_FOR_ITERATION_STEP_8,
                                                                                   STRING_FOR_ITERATION_STEP_8, 7};

// Root menu for note row custom iterance
PLACE_SDRAM_BSS menu_item::Submenu noteRowCustomIteranceRootMenu{
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

PLACE_SDRAM_BSS note_row::Probability noteRowProbabilityMenu{STRING_FOR_NOTE_ROW_EDITOR_PROBABILITY};
PLACE_SDRAM_BSS note_row::IterancePreset noteRowIteranceMenu{STRING_FOR_NOTE_ROW_EDITOR_ITERANCE};
PLACE_SDRAM_BSS note_row::Fill noteRowFillMenu{STRING_FOR_NOTE_ROW_EDITOR_FILL};

// Root menu for Note Row Editor
PLACE_SDRAM_BSS HorizontalMenu noteRowEditorRootMenu{STRING_FOR_NOTE_ROW_EDITOR,
                                                     {
                                                         &sequenceDirectionMenu,
                                                         &noteRowProbabilityMenu,
                                                         &noteRowIteranceMenu,
                                                         &noteRowFillMenu,
                                                     }};

PLACE_SDRAM_BSS menu_item::midi::ProgramSubMenu midiProgramMenu{STRING_FOR_MIDI_PROGRAM_MENU_TITLE,
                                                                {
                                                                    &midiBankMenu,
                                                                    &midiSubMenu,
                                                                    &midiPGMMenu,
                                                                },
                                                                HorizontalMenu::Layout::FIXED,
                                                                2};

// Root menu for MIDI / CV
PLACE_SDRAM_BSS menu_item::Submenu soundEditorRootMenuMIDIOrCV{
    STRING_FOR_MIDI_INST_MENU_TITLE,
    {
        &midiDeviceDefinitionMenu,
        &midiProgramMenu,
        &arpMenuMIDIOrCV,
        &randomizerMenu,
        &bendMenu,
        &cv2SourceMenu,
        &mpeyToModWheelMenu,
        &midiMPEMenu,
        &sequenceDirectionMenu,
    },
};

// Root menu for NonAudioDrums (MIDI and Gate drums)
PLACE_SDRAM_BSS menu_item::Submenu soundEditorRootMenuMidiDrum{
    STRING_FOR_MIDI,
    {
        &arpMenuMIDIOrCV,
        &randomizerMenu,
    },
};

PLACE_SDRAM_BSS menu_item::Submenu soundEditorRootMenuGateDrum{
    STRING_FOR_GATE,
    {
        &arpMenuMIDIOrCV,
        &randomizerMenu,
    },
};

// Root menu for AudioClips
PLACE_SDRAM_BSS menu_item::Submenu soundEditorRootMenuAudioClip{
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
    },
};

// Menu for Performance View Editing Mode
PLACE_SDRAM_BSS menu_item::performance_session_view::EditingMode performEditorMenu{STRING_FOR_PERFORM_EDITOR};

// Root menu for Performance View
PLACE_SDRAM_BSS menu_item::Submenu soundEditorRootMenuPerformanceView{
    STRING_FOR_PERFORM_FX,
    {
        &performEditorMenu,
        &globalFiltersMenu,
        &globalFXMenu,
    },
};

// Sub menu for Stem Export
PLACE_SDRAM_BSS menu_item::stem_export::Start startStemExportMenu{STRING_FOR_START_EXPORT};

PLACE_SDRAM_BSS ToggleBool configureNormalizationMenu{STRING_FOR_CONFIGURE_EXPORT_STEMS_NORMALIZATION,
                                                      STRING_FOR_CONFIGURE_EXPORT_STEMS_NORMALIZATION,
                                                      stemExport.allowNormalization};
PLACE_SDRAM_BSS ToggleBool configureNormalizationForDrumsMenu{STRING_FOR_CONFIGURE_EXPORT_STEMS_NORMALIZATION,
                                                              STRING_FOR_CONFIGURE_EXPORT_STEMS_NORMALIZATION,
                                                              stemExport.allowNormalizationForDrums};
PLACE_SDRAM_BSS ToggleBool configureSilenceMenu{STRING_FOR_CONFIGURE_EXPORT_STEMS_SILENCE,
                                                STRING_FOR_CONFIGURE_EXPORT_STEMS_SILENCE, stemExport.exportToSilence};
PLACE_SDRAM_BSS ToggleBool configureSongFXMenu{STRING_FOR_CONFIGURE_EXPORT_STEMS_SONGFX,
                                               STRING_FOR_CONFIGURE_EXPORT_STEMS_SONGFX, stemExport.includeSongFX};
PLACE_SDRAM_BSS ToggleBool configureKitFXMenu{STRING_FOR_CONFIGURE_EXPORT_STEMS_KITFX,
                                              STRING_FOR_CONFIGURE_EXPORT_STEMS_KITFX, stemExport.includeKitFX};
PLACE_SDRAM_BSS ToggleBool configureOfflineRenderingMenu{STRING_FOR_CONFIGURE_EXPORT_STEMS_OFFLINE_RENDERING,
                                                         STRING_FOR_CONFIGURE_EXPORT_STEMS_OFFLINE_RENDERING,
                                                         stemExport.renderOffline};
PLACE_SDRAM_BSS ToggleBool configureMixdownMenu{STRING_FOR_CONFIGURE_EXPORT_STEMS_MIXDOWN,
                                                STRING_FOR_CONFIGURE_EXPORT_STEMS_MIXDOWN, stemExport.exportMixdown};
PLACE_SDRAM_BSS menu_item::Submenu configureStemExportMenu{STRING_FOR_CONFIGURE_EXPORT_STEMS,
                                                           {
                                                               &configureNormalizationMenu,
                                                               &configureSilenceMenu,
                                                               &configureSongFXMenu,
                                                               &configureOfflineRenderingMenu,
                                                               &configureMixdownMenu,
                                                           }};

PLACE_SDRAM_BSS menu_item::Submenu stemExportMenu{
    STRING_FOR_EXPORT_AUDIO,
    {
        &startStemExportMenu,
        &configureStemExportMenu,
    },
};

PLACE_SDRAM_BSS menu_item::Submenu kitGlobalFXConfigureStemExportMenu{STRING_FOR_CONFIGURE_EXPORT_STEMS,
                                                                      {
                                                                          &configureKitFXMenu,
                                                                          &configureNormalizationForDrumsMenu,
                                                                          &configureSilenceMenu,
                                                                          &configureSongFXMenu,
                                                                          &configureOfflineRenderingMenu,
                                                                      }};

PLACE_SDRAM_BSS menu_item::Submenu kitGlobalFXStemExportMenu{
    STRING_FOR_EXPORT_AUDIO,
    {
        &startStemExportMenu,
        &kitGlobalFXConfigureStemExportMenu,
    },
};

PLACE_SDRAM_BSS ActiveScaleMenu activeScaleMenu{STRING_FOR_ACTIVE_SCALES, ActiveScaleMenu::SONG};
PLACE_SDRAM_BSS record::ThresholdMode songThresholdRecordingModeMenu{STRING_FOR_MODE, record::ThresholdMode::SONG};

PLACE_SDRAM_BSS Submenu songThresholdRecordingSubmenu{
    STRING_FOR_THRESHOLD_RECORDING,
    {
        &songThresholdRecordingModeMenu,
    },
};

PLACE_SDRAM_BSS song::ConfigureMacros configureSongMacrosMenu{STRING_FOR_CONFIGURE_SONG_MACROS};
PLACE_SDRAM_BSS song::MidiLearn midiLearnMenu{STRING_FOR_MIDI_LEARN};

// Root menu for Song View
PLACE_SDRAM_BSS menu_item::Submenu soundEditorRootMenuSongView{
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

PLACE_SDRAM_BSS menu_item::Submenu kitGlobalFXActionsMenu{
    STRING_FOR_ACTIONS,
    {
        &kitGlobalFXStemExportMenu,
    },
};

// Root menu for Kit Global FX
PLACE_SDRAM_BSS menu_item::Submenu soundEditorRootMenuKitGlobalFX{
    STRING_FOR_KIT_GLOBAL_FX,
    {
        &kitGlobalFXActionsMenu,
        &kitClipMasterMenu,
        &kitArpMenu,
        &audioCompMenu,
        &globalFiltersMenu,
        &globalFXMenu,
        &globalSidechainMenu,
    },
};

// Root Menu
PLACE_SDRAM_BSS Submenu settingsRootMenu{
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
    {&sample0RepeatMenu,		&sample0ReverseMenu,		&sample0TimeStretchMenu,		&sample0PitchSpeedMenu,			&sample0RecorderMenu,	&file0SelectorMenu,			&sample0InterpolationMenu,		&sample0StartMenu                  },
    {&sample1RepeatMenu,		&sample1ReverseMenu,		&sample1TimeStretchMenu,		&sample1PitchSpeedMenu,			&sample1RecorderMenu,	&file1SelectorMenu,			&sample1InterpolationMenu,		&sample1StartMenu                  },
    {&source0VolumeMenu,		&source0TransposeMenu,		&osc0TypeMenu,					&osc0PulseWidthMenu,			&osc0PhaseMenu,			&source0FeedbackMenu,		&source0WaveIndexMenu,			&noiseMenu                         },
    {&source1VolumeMenu,		&source1TransposeMenu,		&osc1TypeMenu,					&osc1PulseWidthMenu,			&osc1PhaseMenu,			&source1FeedbackMenu,		&source1WaveIndexMenu,			&oscSyncMenu                       },
    {&modulator0Volume,		&modulator0TransposeMenu,	comingSoonMenu,                 comingSoonMenu,                 &modulator0PhaseMenu,	&modulator0FeedbackMenu,	comingSoonMenu,					&sequenceDirectionMenu             },
    {&modulator1Volume,		&modulator1TransposeMenu,	comingSoonMenu,                 comingSoonMenu,                 &modulator1PhaseMenu,	&modulator1FeedbackMenu,	&modulatorDestMenu,				&stutterRateMenu                   },
    {&volumeMenu,			&masterTransposeMenu,		&vibratoMenu,                   &panMenu,                       &synthModeMenu,			&srrMenu,					&bitcrushMenu,					&clippingMenu                      },
    {&portaMenu,				&polyphonyMenu,				&priorityMenu,                  &unisonDetuneMenu,              &numUnisonMenu,			&threshold,					nullptr,						&foldMenu                          },
    {&env1ReleaseMenu,		&env1SustainMenu,			&env1DecayMenu,                 &env1AttackMenu,                &lpfMorphMenu,			&lpfModeMenu,				&lpfResMenu,					&lpfFreqMenu                       },
    {&env2ReleaseMenu,		&env2SustainMenu,			&env2DecayMenu,                 &env2AttackMenu,                &hpfMorphMenu,			&hpfModeMenu,				&hpfResMenu,					&hpfFreqMenu                       },
    {&sidechainReleaseMenu,	&sidechainSyncMenu,			&sidechainVolumeShortcutMenu,   &sidechainAttackMenu,           &sidechainShapeMenu,	&sidechainSendMenu,			&bassMenu,						&bassFreqMenu                      },
    {&arpRateMenu,			&arpSyncMenu,				&arpGateMenu,                   &arpOctavesMenu,                &arpPresetModeMenu,		&nameEditMenu,				&trebleMenu,					&trebleFreqMenu                    },
    {&lfo1RateMenu,			&lfo1SyncMenu,				&lfo1TypeMenu,                  &modFXTypeMenu,                 &modFXOffsetMenu,		&modFXFeedbackMenu,			&modFXDepthMenu,				&modFXRateMenu                     },
    {&lfo2RateMenu,			&lfo2SyncMenu,				&lfo2TypeMenu,                  &reverbAmountMenu,              &reverbPanMenu,			&reverbWidthMenu,			&reverbDampingMenu,				&reverbRoomSizeMenu                },
    {&delayRateMenu,			&delaySyncMenu,				&delayAnalogMenu,               &delayFeedbackMenu,             &delayPingPongMenu,		nullptr,					nullptr,						nullptr                            },
    {nullptr,				&spreadVelocityMenu,		&randomizerLockMenu,            &randomizerNoteProbabilityMenu,        nullptr,				nullptr,					nullptr,						nullptr                            },
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
    {nullptr,	        nullptr,	            nullptr,	        nullptr,	        nullptr,					nullptr,	nullptr,	nullptr              },
    {&lfo3RateMenu,	    &lfo3SyncMenu,	        &lfo3TypeMenu,	    nullptr,	        nullptr,	                nullptr,	nullptr,	nullptr              },
    {&lfo4RateMenu,	    &lfo4SyncMenu,	        &lfo4TypeMenu,	    nullptr,	        nullptr,	                nullptr,	nullptr,	nullptr              },
    {nullptr,	        nullptr,	            nullptr,	        nullptr,	        nullptr,                    nullptr,	nullptr,	nullptr              },
    {nullptr,	        nullptr,	            nullptr,	        nullptr,	        nullptr,                    nullptr,	nullptr,	nullptr              },
};

PLACE_SDRAM_DATA MenuItem* paramShortcutsForAudioClips[kDisplayWidth][kDisplayHeight] = {
    {nullptr,                 &audioClipReverseMenu,   nullptr,                    &sample0PitchSpeedMenu,       nullptr,             &file0SelectorMenu,  &audioClipInterpolationMenu,&audioClipSampleMarkerEditorMenuEnd},
    {nullptr,                 &audioClipReverseMenu,   nullptr,                    &sample0PitchSpeedMenu,       nullptr,             &file0SelectorMenu,  &audioClipInterpolationMenu,&audioClipSampleMarkerEditorMenuEnd},
    {nullptr,                 &audioClipTransposeMenu, nullptr,                    nullptr,                     nullptr,              nullptr,            nullptr,                  nullptr                            },
    {nullptr,                 &audioClipTransposeMenu, nullptr,                    nullptr,                     nullptr,              nullptr,            nullptr,                  nullptr                            },
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

PLACE_SDRAM_DATA MenuItem* paramShortcutsForKitGlobalFX[][kDisplayHeight] = {
    {nullptr,                 nullptr,                 nullptr,                        nullptr,                        nullptr,                     nullptr,                nullptr,                  nullptr                            },
    {nullptr,                 nullptr,                 nullptr,                        nullptr,                        nullptr,                     nullptr,                nullptr,                  nullptr                            },
    {nullptr,                 nullptr,                 nullptr,                        nullptr,                        nullptr,                     nullptr,                nullptr,                  nullptr                            },
    {nullptr,                 nullptr,                 nullptr,                        nullptr,                        nullptr,                     nullptr,                nullptr,                  nullptr                            },
    {nullptr,                 nullptr,                 nullptr,                        nullptr,                        nullptr,                     nullptr,                nullptr,                  nullptr                            },
    {nullptr,                 nullptr,                 nullptr,                        nullptr,                        nullptr,                     nullptr,                nullptr,                  nullptr             				 },
    {&globalLevelMenu,        &globalPitchMenu,        nullptr,                        &globalPanMenu,                 nullptr,                     &srrMenu,               &bitcrushMenu,            nullptr                            },
    {nullptr,              	 nullptr,                 nullptr,                        nullptr,                        nullptr,                     &threshold,             nullptr,                  nullptr                            },
    {nullptr,                 nullptr,                 nullptr,                        nullptr,                        &globalLPFMorphMenu,         &lpfModeMenu,           &globalLPFResMenu,        &globalLPFFreqMenu                 },
    {nullptr,                 nullptr,                 nullptr,                        nullptr,                        &globalHPFMorphMenu,         &hpfModeMenu,           &globalHPFResMenu,        &globalHPFFreqMenu                 },
    {&sidechainReleaseMenu,   &sidechainSyncMenu,      &globalSidechainVolumeMenu,     &sidechainAttackMenu,           &sidechainShapeMenu,         nullptr,                &bassMenu,                &bassFreqMenu                      },
    {&arpRateMenu,            &arpSyncMenu,            &arpGateMenu,                   &arpOctavesMenu,                &arpPresetModeMenu,          &nameEditMenu,          &trebleMenu,              &trebleFreqMenu                    },
    {nullptr,                 nullptr,                 nullptr,                        &modFXTypeMenu,                 &modFXOffsetMenu,            &modFXFeedbackMenu,     &globalModFXDepthMenu,    &globalModFXRateMenu               },
    {nullptr,                 nullptr,                 nullptr,                        &globalReverbSendAmountMenu,    &reverbPanMenu,              &reverbWidthMenu,       &reverbDampingMenu,       &reverbRoomSizeMenu                },
    {&globalDelayRateMenu,    &delaySyncMenu,          &delayAnalogMenu,               &globalDelayFeedbackMenu,       &delayPingPongMenu,          nullptr,                nullptr,                  nullptr                            },
    {nullptr,          	     &spreadVelocityMenu,	  &randomizerLockMenu,            &randomizerNoteProbabilityMenu,        nullptr,                     nullptr,                nullptr,                  nullptr                            },
};

PLACE_SDRAM_RODATA deluge::vector<HorizontalMenu*> horizontalMenusChainForSound = {
	&recorderMenu, &soundMasterMenuWithoutVibrato,
	&sourceMenuGroup, &voiceMenuGroup, &envMenuGroup, &lfoMenuGroup,
	&filtersMenuGroup, &eqMenu, &modFXMenu,
	&reverbMenuGroup, &delayMenu, &soundDistortionMenu,
	&sidechainMenu, &audioCompMenu, &stutterMenu,
	&arpMenuGroup, &randomizerMenu
};

PLACE_SDRAM_RODATA deluge::vector<HorizontalMenu*> horizontalMenusChainForKit = {
	&kitClipMasterMenu,
	&globalFiltersMenuGroup, &globalEQMenu, &globalModFXMenu,
	&globalReverbMenuGroup, &globalDelayMenu, &globalDistortionMenu,
	&globalSidechainMenu, &audioCompMenu, &stutterMenu,
	&arpMenuGroupKit, &randomizerMenu
};

PLACE_SDRAM_RODATA deluge::vector<HorizontalMenu*> horizontalMenusChainForSong = {
	&songMasterMenu,
	&globalFiltersMenuGroup, &globalEQMenu, &globalModFXMenu,
	&globalReverbMenuGroup, &globalDelayMenu, &globalDistortionMenu,
	&audioCompMenu, &stutterMenu
};

PLACE_SDRAM_RODATA deluge::vector<HorizontalMenu*> horizontalMenusChainForAudioClip = {
	&audioClipMasterMenu, &audioClipSampleMenu,
	&globalFiltersMenuGroup, &eqMenu, &globalModFXMenu,
	&globalReverbMenuGroup, &globalDelayMenu, &audioClipDistortionMenu,
	&globalSidechainMenu, &audioCompMenu, &stutterMenu
};

PLACE_SDRAM_RODATA deluge::vector<HorizontalMenu*> horizontalMenusChainForMidiOrCv = {
	&arpMenuGroupMIDIOrCV, &randomizerMenu
};

PLACE_SDRAM_BSS filter::FilterContainer lpfContainer{{&lpfFreqMenu, &lpfResMenu}, &lpfMorphMenu};
PLACE_SDRAM_BSS filter::FilterContainer hpfContainer{{&hpfFreqMenu, &hpfResMenu}, &hpfMorphMenu};
PLACE_SDRAM_BSS filter::FilterContainer globalLpfContainer{{&globalLPFFreqMenu, &globalLPFResMenu}, &globalLPFMorphMenu};
PLACE_SDRAM_BSS filter::FilterContainer globalHpfContainer{{&globalHPFFreqMenu, &globalHPFResMenu}, &globalHPFMorphMenu};
PLACE_SDRAM_BSS deluge::vector<HorizontalMenuContainer*> horizontalMenuContainers{&lpfContainer, &hpfContainer, &globalLpfContainer, &globalHpfContainer};

//clang-format on

void setCvNumberForTitle(int32_t num) {
	num += 1;
	cvSubmenu.format(num);
	cvVoltsMenu.format(num);
	cvTransposeMenu.format(num);
}
