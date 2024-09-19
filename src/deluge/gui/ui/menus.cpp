#include "gui/l10n/strings.h"
#include "gui/menu_item/active_scales.h"
#include "gui/menu_item/arpeggiator/midi_cv/gate.h"
#include "gui/menu_item/arpeggiator/midi_cv/ratchet_amount.h"
#include "gui/menu_item/arpeggiator/midi_cv/ratchet_probability.h"
#include "gui/menu_item/arpeggiator/midi_cv/rate.h"
#include "gui/menu_item/arpeggiator/midi_cv/rhythm.h"
#include "gui/menu_item/arpeggiator/midi_cv/sequence_length.h"
#include "gui/menu_item/arpeggiator/mode.h"
#include "gui/menu_item/arpeggiator/mpe_velocity.h"
#include "gui/menu_item/arpeggiator/note_mode.h"
#include "gui/menu_item/arpeggiator/octave_mode.h"
#include "gui/menu_item/arpeggiator/octaves.h"
#include "gui/menu_item/arpeggiator/preset_mode.h"
#include "gui/menu_item/arpeggiator/rate.h"
#include "gui/menu_item/arpeggiator/rhythm.h"
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
#include "gui/menu_item/cv/selection.h"
#include "gui/menu_item/cv/submenu.h"
#include "gui/menu_item/cv/transpose.h"
#include "gui/menu_item/cv/volts.h"
#include "gui/menu_item/defaults/bend_range.h"
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
#include "gui/menu_item/drum_name.h"
#include "gui/menu_item/dx/browse.h"
#include "gui/menu_item/dx/engine_select.h"
#include "gui/menu_item/dx/global_params.h"
#include "gui/menu_item/dx/param.h"
#include "gui/menu_item/envelope/segment.h"
#include "gui/menu_item/file_selector.h"
#include "gui/menu_item/filter/hpf_freq.h"
#include "gui/menu_item/filter/hpf_mode.h"
#include "gui/menu_item/filter/lpf_freq.h"
#include "gui/menu_item/filter/lpf_mode.h"
#include "gui/menu_item/filter/morph.h"
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
#include "gui/menu_item/lfo/rate.h"
#include "gui/menu_item/lfo/sync.h"
#include "gui/menu_item/lfo/type.h"
#include "gui/menu_item/master_transpose.h"
#include "gui/menu_item/menu_item.h"
#include "gui/menu_item/midi/after_touch_to_mono.h"
#include "gui/menu_item/midi/bank.h"
#include "gui/menu_item/midi/command.h"
#include "gui/menu_item/midi/default_velocity_to_level.h"
#include "gui/menu_item/midi/device.h"
#include "gui/menu_item/midi/device_send_clock.h"
#include "gui/menu_item/midi/devices.h"
#include "gui/menu_item/midi/follow/follow_channel.h"
#include "gui/menu_item/midi/follow/follow_feedback_automation.h"
#include "gui/menu_item/midi/follow/follow_feedback_channel_type.h"
#include "gui/menu_item/midi/follow/follow_kit_root_note.h"
#include "gui/menu_item/midi/mpe_to_mono.h"
#include "gui/menu_item/midi/pgm.h"
#include "gui/menu_item/midi/sub.h"
#include "gui/menu_item/midi/takeover.h"
#include "gui/menu_item/midi/transpose.h"
#include "gui/menu_item/mod_fx/depth_patched.h"
#include "gui/menu_item/mod_fx/depth_unpatched.h"
#include "gui/menu_item/mod_fx/feedback.h"
#include "gui/menu_item/mod_fx/offset.h"
#include "gui/menu_item/mod_fx/type.h"
#include "gui/menu_item/modulator/destination.h"
#include "gui/menu_item/modulator/transpose.h"
#include "gui/menu_item/monitor/mode.h"
#include "gui/menu_item/mpe/direction_selector.h"
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
#include "gui/menu_item/record/quantize.h"
#include "gui/menu_item/reverb/damping.h"
#include "gui/menu_item/reverb/hpf.h"
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
#include "gui/menu_item/shortcuts/version.h"
#include "gui/menu_item/sidechain/attack.h"
#include "gui/menu_item/sidechain/release.h"
#include "gui/menu_item/sidechain/send.h"
#include "gui/menu_item/sidechain/sync.h"
#include "gui/menu_item/sidechain/volume.h"
#include "gui/menu_item/song_macros/configure.h"
#include "gui/menu_item/source/patched_param/fm.h"
#include "gui/menu_item/stem_export/start.h"
#include "gui/menu_item/submenu.h"
#include "gui/menu_item/submenu/MPE.h"
#include "gui/menu_item/submenu/actual_source.h"
#include "gui/menu_item/submenu/arpeggiator.h"
#include "gui/menu_item/submenu/bend.h"
#include "gui/menu_item/submenu/envelope.h"
#include "gui/menu_item/submenu/modulator.h"
#include "gui/menu_item/submenu/sidechain.h"
#include "gui/menu_item/submenu_referring_to_one_thing.h"
#include "gui/menu_item/swing/interval.h"
#include "gui/menu_item/synth_mode.h"
#include "gui/menu_item/trigger/in/ppqn.h"
#include "gui/menu_item/trigger/out/ppqn.h"
#include "gui/menu_item/unison/count.h"
#include "gui/menu_item/unison/detune.h"
#include "gui/menu_item/unison/stereoSpread.h"
#include "gui/menu_item/unpatched_param.h"
#include "gui/menu_item/unpatched_param/pan.h"
#include "gui/menu_item/unpatched_param/sound_unpatched_param.h"
#include "gui/menu_item/unpatched_param/updating_reverb_params.h"
#include "gui/menu_item/voice/polyphony.h"
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

// This menu item is special, it only exists on the grid (not in the menu hierarchy). To avoid confusion in
// autogenerated menu documentation, this item should be left alone..
unison::CountToStereoSpread numUnisonToStereoSpreadMenu{STRING_FOR_UNISON_NUMBER};

// Arp --------------------------------------------------------------------------------------
arpeggiator::PresetMode arpPresetModeMenu{STRING_FOR_PRESET, STRING_FOR_ARP_PRESET_MENU_TITLE};
arpeggiator::Mode arpModeMenu{STRING_FOR_ENABLED, STRING_FOR_ARP_MODE_MENU_TITLE};
arpeggiator::Sync arpSyncMenu{STRING_FOR_SYNC, STRING_FOR_ARP_SYNC_MENU_TITLE};
arpeggiator::Octaves arpOctavesMenu{STRING_FOR_NUMBER_OF_OCTAVES, STRING_FOR_ARP_OCTAVES_MENU_TITLE};
arpeggiator::OctaveMode arpOctaveModeMenu{STRING_FOR_OCTAVE_MODE, STRING_FOR_ARP_OCTAVE_MODE_MENU_TITLE};
arpeggiator::OctaveModeToNoteMode arpeggiator::arpOctaveModeToNoteModeMenu{STRING_FOR_OCTAVE_MODE,
                                                                           STRING_FOR_ARP_OCTAVE_MODE_MENU_TITLE};
arpeggiator::NoteMode arpNoteModeMenu{STRING_FOR_NOTE_MODE, STRING_FOR_ARP_NOTE_MODE_MENU_TITLE};
arpeggiator::NoteModeFromOctaveMode arpeggiator::arpNoteModeFromOctaveModeMenu{STRING_FOR_NOTE_MODE,
                                                                               STRING_FOR_ARP_NOTE_MODE_MENU_TITLE};
arpeggiator::OnlyForSoundUnpatchedParam arpGateMenu{STRING_FOR_GATE, STRING_FOR_ARP_GATE_MENU_TITLE,
                                                    params::UNPATCHED_ARP_GATE};
arpeggiator::midi_cv::Gate arpGateMenuMIDIOrCV{STRING_FOR_GATE, STRING_FOR_ARP_GATE_MENU_TITLE};
arpeggiator::Rate arpRateMenu{STRING_FOR_RATE, STRING_FOR_ARP_RATE_MENU_TITLE, params::GLOBAL_ARP_RATE};
arpeggiator::midi_cv::Rate arpRateMenuMIDIOrCV{STRING_FOR_RATE, STRING_FOR_ARP_RATE_MENU_TITLE};
arpeggiator::Rhythm arpRhythmMenu{STRING_FOR_RHYTHM, STRING_FOR_ARP_RHYTHM_MENU_TITLE, params::UNPATCHED_ARP_RHYTHM};
arpeggiator::midi_cv::Rhythm arpRhythmMenuMIDIOrCV{STRING_FOR_RHYTHM, STRING_FOR_ARP_RHYTHM_MENU_TITLE};
arpeggiator::OnlyForSoundUnpatchedParam arpSequenceLengthMenu{
    STRING_FOR_SEQUENCE_LENGTH, STRING_FOR_ARP_SEQUENCE_LENGTH_MENU_TITLE, params::UNPATCHED_ARP_SEQUENCE_LENGTH};
arpeggiator::midi_cv::SequenceLength arpSequenceLengthMenuMIDIOrCV{STRING_FOR_SEQUENCE_LENGTH,
                                                                   STRING_FOR_ARP_SEQUENCE_LENGTH_MENU_TITLE};
arpeggiator::OnlyForSoundUnpatchedParam arpRatchetAmountMenu{
    STRING_FOR_NUMBER_OF_RATCHETS, STRING_FOR_ARP_RATCHETS_MENU_TITLE, params::UNPATCHED_ARP_RATCHET_AMOUNT};
arpeggiator::midi_cv::RatchetAmount arpRatchetAmountMenuMIDIOrCV{STRING_FOR_NUMBER_OF_RATCHETS,
                                                                 STRING_FOR_ARP_RATCHETS_MENU_TITLE};
arpeggiator::OnlyForSoundUnpatchedParam arpRatchetProbabilityMenu{STRING_FOR_RATCHET_PROBABILITY,
                                                                  STRING_FOR_ARP_RATCHET_PROBABILITY_MENU_TITLE,
                                                                  params::UNPATCHED_ARP_RATCHET_PROBABILITY};
arpeggiator::midi_cv::RatchetProbability arpRatchetProbabilityMenuMIDIOrCV{
    STRING_FOR_RATCHET_PROBABILITY, STRING_FOR_ARP_RATCHET_PROBABILITY_MENU_TITLE};

// Arp: MPE
arpeggiator::ArpMpeVelocity arpMpeVelocityMenu{STRING_FOR_VELOCITY, STRING_FOR_VELOCITY};
Submenu arpMpeMenu{STRING_FOR_MPE, {&arpMpeVelocityMenu}};

submenu::Arpeggiator arpMenu{
    STRING_FOR_ARPEGGIATOR,
    {
        &arpModeMenu,
        &arpSyncMenu,
        &arpRateMenu,
        &arpRateMenuMIDIOrCV,
        &arpGateMenu,
        &arpGateMenuMIDIOrCV,
        &arpOctavesMenu,
        &arpOctaveModeMenu,
        &arpNoteModeMenu,
        &arpRhythmMenu,
        &arpRhythmMenuMIDIOrCV,
        &arpSequenceLengthMenu,
        &arpSequenceLengthMenuMIDIOrCV,
        &arpRatchetAmountMenu,
        &arpRatchetAmountMenuMIDIOrCV,
        &arpRatchetProbabilityMenu,
        &arpRatchetProbabilityMenuMIDIOrCV,
        &arpMpeMenu,
    },
};

// Voice menu ----------------------------------------------------------------------------------------------------

voice::PolyphonyType polyphonyMenu{STRING_FOR_POLYPHONY};
voice::VoiceCount voice::polyphonicVoiceCountMenu{STRING_FOR_MAX_VOICES};
UnpatchedParam portaMenu{STRING_FOR_PORTAMENTO, params::UNPATCHED_PORTAMENTO};
voice::Priority priorityMenu{STRING_FOR_PRIORITY};

Submenu voiceMenu{STRING_FOR_VOICE,
                  {&polyphonyMenu, &unisonMenu, &voice::polyphonicVoiceCountMenu, &portaMenu, &priorityMenu}};

// Modulator menu -----------------------------------------------------------------------

modulator::Transpose modulatorTransposeMenu{STRING_FOR_TRANSPOSE, STRING_FOR_FM_MOD_TRAN_MENU_TITLE,
                                            params::LOCAL_MODULATOR_0_PITCH_ADJUST};
source::patched_param::FM modulatorVolume{STRING_FOR_AMOUNT_LEVEL, STRING_FOR_FM_MOD_LEVEL_MENU_TITLE,
                                          params::LOCAL_MODULATOR_0_VOLUME};
source::patched_param::FM modulatorFeedbackMenu{STRING_FOR_FEEDBACK, STRING_FOR_FM_MOD_FBACK_MENU_TITLE,
                                                params::LOCAL_MODULATOR_0_FEEDBACK};
modulator::Destination modulatorDestMenu{STRING_FOR_DESTINATION, STRING_FOR_FM_MOD2_DEST_MENU_TITLE};
osc::RetriggerPhase modulatorPhaseMenu{STRING_FOR_RETRIGGER_PHASE, STRING_FOR_FM_MOD_RETRIG_MENU_TITLE, true};

// LFO1 menu ---------------------------------------------------------------------------------

lfo::Type lfo1TypeMenu{STRING_FOR_SHAPE, STRING_FOR_LFO1_TYPE, LFO1_ID};
lfo::Rate lfo1RateMenu{STRING_FOR_RATE, STRING_FOR_LFO1_RATE, params::GLOBAL_LFO_FREQ, LFO1_ID};
lfo::Sync lfo1SyncMenu{STRING_FOR_SYNC, STRING_FOR_LFO1_SYNC, LFO1_ID};

Submenu lfo1Menu{STRING_FOR_LFO1, {&lfo1TypeMenu, &lfo1RateMenu, &lfo1SyncMenu}};

// LFO2 menu ---------------------------------------------------------------------------------
lfo::Type lfo2TypeMenu{STRING_FOR_SHAPE, STRING_FOR_LFO2_TYPE, LFO2_ID};
lfo::Rate lfo2RateMenu{STRING_FOR_RATE, STRING_FOR_LFO2_RATE, params::LOCAL_LFO_LOCAL_FREQ, LFO2_ID};
lfo::Sync lfo2SyncMenu{STRING_FOR_SYNC, STRING_FOR_LFO2_SYNC, LFO2_ID};

Submenu lfo2Menu{STRING_FOR_LFO2, {&lfo2TypeMenu, &lfo2RateMenu, &lfo2SyncMenu}};

// Mod FX ----------------------------------------------------------------------------------
mod_fx::Type modFXTypeMenu{STRING_FOR_TYPE, STRING_FOR_MODFX_TYPE};
patched_param::Integer modFXRateMenu{STRING_FOR_RATE, STRING_FOR_MODFX_RATE, params::GLOBAL_MOD_FX_RATE};
mod_fx::Feedback modFXFeedbackMenu{STRING_FOR_FEEDBACK, STRING_FOR_MODFX_FEEDBACK, params::UNPATCHED_MOD_FX_FEEDBACK};
mod_fx::Depth_Patched modFXDepthMenu{STRING_FOR_DEPTH, STRING_FOR_MODFX_DEPTH, params::GLOBAL_MOD_FX_DEPTH};
mod_fx::Offset modFXOffsetMenu{STRING_FOR_OFFSET, STRING_FOR_MODFX_OFFSET, params::UNPATCHED_MOD_FX_OFFSET};

Submenu modFXMenu{
    STRING_FOR_MOD_FX,
    {
        &modFXTypeMenu,
        &modFXRateMenu,
        &modFXFeedbackMenu,
        &modFXDepthMenu,
        &modFXOffsetMenu,
    },
};

// EQ -------------------------------------------------------------------------------------
UnpatchedParam bassMenu{STRING_FOR_BASS, params::UNPATCHED_BASS};
UnpatchedParam trebleMenu{STRING_FOR_TREBLE, params::UNPATCHED_TREBLE};
UnpatchedParam bassFreqMenu{STRING_FOR_BASS_FREQUENCY, params::UNPATCHED_BASS_FREQ};
UnpatchedParam trebleFreqMenu{STRING_FOR_TREBLE_FREQUENCY, params::UNPATCHED_TREBLE_FREQ};

Submenu eqMenu{
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

Submenu delayMenu{
    STRING_FOR_DELAY,
    {
        &delayFeedbackMenu,
        &delayRateMenu,
        &delayPingPongMenu,
        &delayAnalogMenu,
        &delaySyncMenu,
    },
};

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

submenu::Sidechain sidechainMenu{
    STRING_FOR_SIDECHAIN,
    STRING_FOR_SIDECHAIN,
    {
        &sidechainSendMenu,
        &sidechainVolumeShortcutMenu,
        &sidechainSyncMenu,
        &sidechainAttackMenu,
        &sidechainReleaseMenu,
        &sidechainShapeMenu,
    },
    false,
};

submenu::Sidechain reverbSidechainMenu{
    STRING_FOR_REVERB_SIDECHAIN,
    STRING_FOR_REVERB_SIDECH_MENU_TITLE,
    {
        &reverbSidechainVolumeMenu,
        &sidechainSyncMenu,
        &sidechainAttackMenu,
        &sidechainReleaseMenu,
        &reverbSidechainShapeMenu,
    },
    true,
};

// Reverb ----------------------------------------------------------------------------------
patched_param::Integer reverbAmountMenu{STRING_FOR_AMOUNT, STRING_FOR_REVERB_AMOUNT, params::GLOBAL_REVERB_AMOUNT};
reverb::RoomSize reverbRoomSizeMenu{STRING_FOR_ROOM_SIZE};
reverb::Damping reverbDampingMenu{STRING_FOR_DAMPING};
reverb::Width reverbWidthMenu{STRING_FOR_WIDTH, STRING_FOR_REVERB_WIDTH};
reverb::Pan reverbPanMenu{STRING_FOR_PAN, STRING_FOR_REVERB_PAN};
reverb::Model reverbModelMenu{STRING_FOR_MODEL};
reverb::HPF reverbHPFMenu{STRING_FOR_HPF};

Submenu reverbMenu{
    STRING_FOR_REVERB,
    {
        &reverbAmountMenu,
        &reverbModelMenu,
        &reverbRoomSizeMenu,
        &reverbDampingMenu,
        &reverbWidthMenu,
        &reverbHPFMenu,
        &reverbPanMenu,
        &reverbSidechainMenu,
    },
};

// FX ----------------------------------------------------------------------------------------

fx::Clipping clippingMenu{STRING_FOR_SATURATION};
UnpatchedParam srrMenu{STRING_FOR_DECIMATION, params::UNPATCHED_SAMPLE_RATE_REDUCTION};
UnpatchedParam bitcrushMenu{STRING_FOR_BITCRUSH, params::UNPATCHED_BITCRUSHING};
patched_param::Integer foldMenu{STRING_FOR_WAVEFOLD, STRING_FOR_WAVEFOLD, params::LOCAL_FOLD};

Submenu soundDistortionMenu{
    STRING_FOR_DISTORTION,
    {
        &clippingMenu,
        &srrMenu,
        &bitcrushMenu,
        &foldMenu,
    },
};

// MIDIInstrument menu ----------------------------------------------------------------------

midi::Bank midiBankMenu{STRING_FOR_BANK, STRING_FOR_MIDI_BANK};
midi::Sub midiSubMenu{STRING_FOR_SUB_BANK, STRING_FOR_MIDI_SUB_BANK};
midi::PGM midiPGMMenu{STRING_FOR_PGM, STRING_FOR_MIDI_PGM_NUMB_MENU_TITLE};
midi::MPEYToModWheel mpeyToModWheelMenu{STRING_FOR_Y_AXIS_CONVERSION, STRING_FOR_Y_AXIS_CONVERSION};
midi::AftertouchToMono midiAftertouchCollapseMenu{STRING_FOR_PATCH_SOURCE_AFTERTOUCH,
                                                  STRING_FOR_PATCH_SOURCE_AFTERTOUCH};
midi::MPEToMono midiMPECollapseMenu{STRING_FOR_MPE, STRING_FOR_MPE};
submenu::PolyMonoConversion midiMPEMenu{STRING_FOR_MPE_MONO, {&midiAftertouchCollapseMenu, &midiMPECollapseMenu}};
// Clip-level stuff --------------------------------------------------------------------------

sequence::Direction sequenceDirectionMenu{STRING_FOR_PLAY_DIRECTION};

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
UnpatchedParam globalLPFFreqMenu{STRING_FOR_FREQUENCY, STRING_FOR_LPF_FREQUENCY, params::UNPATCHED_LPF_FREQ};
UnpatchedParam globalLPFResMenu{STRING_FOR_RESONANCE, STRING_FOR_LPF_RESONANCE, params::UNPATCHED_LPF_RES};
UnpatchedParam globalLPFMorphMenu{STRING_FOR_MORPH, STRING_FOR_LPF_MORPH, params::UNPATCHED_LPF_MORPH};
Submenu globalLPFMenu{
    STRING_FOR_LPF,
    {
        &globalLPFFreqMenu,
        &globalLPFResMenu,
        &globalLPFMorphMenu,
        &lpfModeMenu,
    },
};

// HPF Menu
UnpatchedParam globalHPFFreqMenu{STRING_FOR_FREQUENCY, STRING_FOR_HPF_FREQUENCY, params::UNPATCHED_HPF_FREQ};
UnpatchedParam globalHPFResMenu{STRING_FOR_RESONANCE, STRING_FOR_HPF_RESONANCE, params::UNPATCHED_HPF_RES};
UnpatchedParam globalHPFMorphMenu{STRING_FOR_MORPH, STRING_FOR_HPF_MORPH, params::UNPATCHED_HPF_MORPH};

Submenu globalHPFMenu{
    STRING_FOR_HPF,
    {
        &globalHPFFreqMenu,
        &globalHPFResMenu,
        &globalHPFMorphMenu,
        &hpfModeMenu,
    },
};

Submenu globalFiltersMenu{
    STRING_FOR_FILTERS,
    {
        &globalLPFMenu,
        &globalHPFMenu,
        &filterRoutingMenu,
    },
};

// EQ Menu

Submenu globalEQMenu{
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

Submenu globalDelayMenu{
    STRING_FOR_DELAY,
    {
        &globalDelayFeedbackMenu,
        &globalDelayRateMenu,
        &delayPingPongMenu,
        &delayAnalogMenu,
        &delaySyncMenu,
    },
};

// Reverb Menu

UnpatchedParam globalReverbSendAmountMenu{
    STRING_FOR_AMOUNT,
    STRING_FOR_REVERB_AMOUNT,
    params::UNPATCHED_REVERB_SEND_AMOUNT,
};

Submenu globalReverbMenu{
    STRING_FOR_REVERB,
    {
        &globalReverbSendAmountMenu,
        &reverbModelMenu,
        &reverbRoomSizeMenu,
        &reverbDampingMenu,
        &reverbWidthMenu,
        &reverbHPFMenu,
        &reverbPanMenu,
        &reverbSidechainMenu,
    },
};

// Mod FX Menu

mod_fx::Depth_Unpatched globalModFXDepthMenu{STRING_FOR_DEPTH, STRING_FOR_MOD_FX_DEPTH, params::UNPATCHED_MOD_FX_DEPTH};
UnpatchedParam globalModFXRateMenu{STRING_FOR_RATE, STRING_FOR_MOD_FX_RATE, params::UNPATCHED_MOD_FX_RATE};

Submenu globalModFXMenu{
    STRING_FOR_MOD_FX,
    {
        &modFXTypeMenu,
        &globalModFXRateMenu,
        &globalModFXDepthMenu,
        &modFXFeedbackMenu,
        &modFXOffsetMenu,
    },
};

Submenu globalDistortionMenu{
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
        &globalModFXMenu,
        &globalDistortionMenu,
    },
};

// Stutter Menu
UnpatchedParam globalStutterRateMenu{
    STRING_FOR_STUTTER,
    STRING_FOR_STUTTER_RATE,
    params::UNPATCHED_STUTTER_RATE,
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

Submenu audioClipDistortionMenu{
    STRING_FOR_DISTORTION,
    {
        &clippingMenu,
        &srrMenu,
        &bitcrushMenu,
    },
};

Submenu audioClipFXMenu{
    STRING_FOR_FX,
    {
        &eqMenu,
        &globalDelayMenu,
        &globalReverbMenu,
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

const MenuItem* midiOrCVParamShortcuts[8] = {
    &arpRateMenuMIDIOrCV,
    &arpSyncMenu,
    &arpGateMenuMIDIOrCV,
    &arpOctavesMenu,
    &arpPresetModeMenu,
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

Submenu recordSubmenu{
    STRING_FOR_RECORDING,
    {
        &recordCountInMenu,
        &recordQuantizeMenu,
        &recordMarginsMenu,
        &monitorModeMenu,
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

Submenu midiCommandsMenu{
    STRING_FOR_COMMANDS,
    STRING_FOR_MIDI_COMMANDS,
    {&playMidiCommand, &playbackRestartMidiCommand, &recordMidiCommand, &tapMidiCommand, &undoMidiCommand,
     &redoMidiCommand, &loopMidiCommand, &loopContinuousLayeringMidiCommand, &fillMidiCommand, &transposeMidiCommand},
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
ToggleBool tempoMagnitudeMatchingMenu{STRING_FOR_TEMPO_MAGNITUDE_MATCHING, STRING_FOR_TEMPO_M_MATCH_MENU_TITLE,
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
    {&defaultKeyboardLayoutMenu, &defaultKeyboardFunctionsMenu},
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

ToggleBool defaultAutomationInterpolateMenu{STRING_FOR_COMMUNITY_FEATURE_AUTOMATION_INTERPOLATION,
                                            STRING_FOR_COMMUNITY_FEATURE_AUTOMATION_INTERPOLATION,
                                            FlashStorage::automationInterpolate};

ToggleBool defaultAutomationClearMenu{STRING_FOR_COMMUNITY_FEATURE_AUTOMATION_CLEAR,
                                      STRING_FOR_COMMUNITY_FEATURE_AUTOMATION_CLEAR, FlashStorage::automationClear};

ToggleBool defaultAutomationShiftMenu{STRING_FOR_COMMUNITY_FEATURE_AUTOMATION_SHIFT,
                                      STRING_FOR_COMMUNITY_FEATURE_AUTOMATION_SHIFT, FlashStorage::automationShift};

ToggleBool defaultAutomationNudgeNoteMenu{STRING_FOR_COMMUNITY_FEATURE_AUTOMATION_NUDGE_NOTE,
                                          STRING_FOR_COMMUNITY_FEATURE_AUTOMATION_NUDGE_NOTE,
                                          FlashStorage::automationNudgeNote};

ToggleBool defaultAutomationDisableAuditionPadShortcutsMenu{
    STRING_FOR_COMMUNITY_FEATURE_AUTOMATION_DISABLE_AUDITION_PAD_SHORTCUTS,
    STRING_FOR_COMMUNITY_FEATURE_AUTOMATION_DISABLE_AUDITION_PAD_SHORTCUTS,
    FlashStorage::automationDisableAuditionPadShortcuts};

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

// Submenu::SubmenuReferringToOneThing if we need to address osc1 and osc2 directly
std::array<MenuItem*, 3> dxMenuItems = {
    &dxBrowseMenu,
    &dxGlobalParams,
    &dxEngineSelect,
};
menu_item::Submenu dxMenu{STRING_FOR_DX_1, dxMenuItems};

// Not FM
patched_param::IntegerNonFM noiseMenu{STRING_FOR_NOISE_LEVEL, params::LOCAL_NOISE_VOLUME};

MasterTranspose masterTransposeMenu{STRING_FOR_MASTER_TRANSPOSE, STRING_FOR_MASTER_TRAN_MENU_TITLE};

patch_cable_strength::Fixed vibratoMenu{STRING_FOR_VIBRATO, params::LOCAL_PITCH_ADJUST, PatchSource::LFO_GLOBAL};

// Drum only
menu_item::DrumName drumNameMenu{STRING_FOR_NAME};

// Synth only
menu_item::SynthMode synthModeMenu{STRING_FOR_SYNTH_MODE};
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
        &drumNameMenu,
    },
};

Submenu soundFXMenu{
    STRING_FOR_FX,
    {
        &eqMenu,
        &delayMenu,
        &reverbMenu,
        &modFXMenu,
        &soundDistortionMenu,
        &noiseMenu,
    },
};

menu_item::Submenu soundEditorRootMenu{
    STRING_FOR_SOUND,
    {
        &soundMasterMenu,
        &arpMenu,
        &audioCompMenu,
        &soundFiltersMenu,
        &soundFXMenu,
        &sidechainMenu,
        &source0Menu,
        &source1Menu,
        &modulator0Menu,
        &modulator1Menu,
        &env0Menu,
        &env1Menu,
        &lfo1Menu,
        &lfo2Menu,
        &voiceMenu,
        &bendMenu,
        &drumBendRangeMenu,
        &patchCablesMenu,
        &sequenceDirectionMenu,
    },
};

// Root menu for MIDI / CV
menu_item::Submenu soundEditorRootMenuMIDIOrCV{
    STRING_FOR_MIDI_INST_MENU_TITLE,
    {
        &midiPGMMenu,
        &midiBankMenu,
        &midiSubMenu,
        &arpMenu,
        &bendMenu,
        &mpeyToModWheelMenu,
        &midiMPEMenu,
        &sequenceDirectionMenu,
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
menu_item::Submenu configureStemExportMenu{STRING_FOR_CONFIGURE_EXPORT_STEMS,
                                           {
                                               &configureNormalizationMenu,
                                               &configureSilenceMenu,
                                               &configureSongFXMenu,
                                               &configureOfflineRenderingMenu,
                                           }};

menu_item::Submenu stemExportMenu{
    STRING_FOR_EXPORT_AUDIO,
    {
        &startStemExportMenu,
        &configureStemExportMenu,
    },
};

ActiveScaleMenu activeScaleMenu{STRING_FOR_ACTIVE_SCALES, ActiveScaleMenu::SONG};

song_macros::Configure configureSongMacrosMenu{STRING_FOR_CONFIGURE_SONG_MACROS};

// Root menu for Song View
menu_item::Submenu soundEditorRootMenuSongView{
    STRING_FOR_SONG,
    {
        &songMasterMenu,
        &globalFiltersMenu,
        &globalFXMenu,
        &swingIntervalMenu,
        &activeScaleMenu,
        &configureSongMacrosMenu,
        &stemExportMenu,
    },
};

// Root menu for Kit Global FX
menu_item::Submenu soundEditorRootMenuKitGlobalFX{
    STRING_FOR_KIT_GLOBAL_FX,
    {
        &kitClipMasterMenu,
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
MenuItem* paramShortcutsForSounds[][8] = {
  // Post V3
    {&sampleRepeatMenu,       &sampleReverseMenu,      &timeStretchMenu,               &samplePitchSpeedMenu,          &audioRecorderMenu,   &fileSelectorMenu,      &interpolationMenu,       &sampleStartMenu                   },
    {&sampleRepeatMenu,       &sampleReverseMenu,      &timeStretchMenu,               &samplePitchSpeedMenu,          &audioRecorderMenu,   &fileSelectorMenu,      &interpolationMenu,       &sampleStartMenu                   },
    {&sourceVolumeMenu,       &sourceTransposeMenu,    &oscTypeMenu,                   &pulseWidthMenu,                &oscPhaseMenu,        &sourceFeedbackMenu,    &sourceWaveIndexMenu,     &noiseMenu                         },
    {&sourceVolumeMenu,       &sourceTransposeMenu,    &oscTypeMenu,                   &pulseWidthMenu,                &oscPhaseMenu,        &sourceFeedbackMenu,    &sourceWaveIndexMenu,     &oscSyncMenu                       },
    {&modulatorVolume,        &modulatorTransposeMenu, comingSoonMenu,                 comingSoonMenu,                 &modulatorPhaseMenu,  &modulatorFeedbackMenu, comingSoonMenu,           &sequenceDirectionMenu             },
    {&modulatorVolume,        &modulatorTransposeMenu, comingSoonMenu,                 comingSoonMenu,                 &modulatorPhaseMenu,  &modulatorFeedbackMenu, &modulatorDestMenu,       nullptr                            },
    {&volumeMenu,             &masterTransposeMenu,    &vibratoMenu,                   &panMenu,                       &synthModeMenu,       &srrMenu,               &bitcrushMenu,            &clippingMenu                      },
    {&portaMenu,              &polyphonyMenu,          &priorityMenu,                  &unisonDetuneMenu,              &numUnisonToStereoSpreadMenu,       nullptr,  nullptr,                  &foldMenu                          },
    {&envReleaseMenu,         &envSustainMenu,         &envDecayMenu,                  &envAttackMenu,                 &lpfMorphMenu,        &lpfModeMenu,           &lpfResMenu,              &lpfFreqMenu                       },
    {&envReleaseMenu,         &envSustainMenu,         &envDecayMenu,                  &envAttackMenu,                 &hpfMorphMenu,        &hpfModeMenu,           &hpfResMenu,              &hpfFreqMenu                       },
    {&sidechainReleaseMenu,   &sidechainSyncMenu,      &sidechainVolumeShortcutMenu,   &sidechainAttackMenu,           &sidechainShapeMenu,  &sidechainSendMenu,     &bassMenu,                &bassFreqMenu                      },
    {&arpRateMenu,            &arpSyncMenu,            &arpGateMenu,                   &arpOctavesMenu,                &arpPresetModeMenu,   &drumNameMenu,          &trebleMenu,              &trebleFreqMenu                    },
    {&lfo1RateMenu,           &lfo1SyncMenu,           &lfo1TypeMenu,                  &modFXTypeMenu,                 &modFXOffsetMenu,     &modFXFeedbackMenu,     &modFXDepthMenu,          &modFXRateMenu                     },
    {&lfo2RateMenu,           &lfo2SyncMenu,           &lfo2TypeMenu,                  &reverbAmountMenu,              &reverbPanMenu,       &reverbWidthMenu,       &reverbDampingMenu,       &reverbRoomSizeMenu                },
    {&delayRateMenu,          &delaySyncMenu,          &delayAnalogMenu,               &delayFeedbackMenu,             &delayPingPongMenu,   nullptr,                nullptr,                  nullptr                            },
};

MenuItem* paramShortcutsForAudioClips[][8] = {
    {nullptr,                 &audioClipReverseMenu,   nullptr,                        &samplePitchSpeedMenu,          nullptr,              &fileSelectorMenu,      &audioClipInterpolationMenu,&audioClipSampleMarkerEditorMenuEnd},
    {nullptr,                 &audioClipReverseMenu,   nullptr,                        &samplePitchSpeedMenu,          nullptr,              &fileSelectorMenu,      &audioClipInterpolationMenu,&audioClipSampleMarkerEditorMenuEnd},
    {nullptr,     	  		  nullptr, 				   nullptr,                        nullptr,                        nullptr,              nullptr,                nullptr,                  nullptr                            },
    {nullptr,     	  		  nullptr, 				   nullptr,                        nullptr,                        nullptr,              nullptr,                nullptr,                  nullptr                            },
    {nullptr,                 nullptr,                 nullptr,                        nullptr,                        nullptr,              nullptr,                nullptr,                  nullptr                            },
    {nullptr,                 nullptr,                 nullptr,                        nullptr,                        nullptr,              nullptr,                nullptr,                  nullptr                            },
    {&globalLevelMenu,     	  &audioClipTransposeMenu, nullptr,                        &globalPanMenu,                 nullptr,              &srrMenu,               &bitcrushMenu,            &clippingMenu                      },
    {nullptr,                 nullptr,                 &priorityMenu,                  nullptr,                        nullptr,              nullptr,                nullptr,                  comingSoonMenu                     },
    {nullptr,                 nullptr,                 nullptr,                        &audioClipAttackMenu,           &globalLPFMorphMenu,  &lpfModeMenu,           &globalLPFResMenu,        &globalLPFFreqMenu              	  },
    {nullptr,                 nullptr,                 nullptr,                        &audioClipAttackMenu,           &globalHPFMorphMenu,  &hpfModeMenu,           &globalHPFResMenu,        &globalHPFFreqMenu                 },
    {&sidechainReleaseMenu,   &sidechainSyncMenu,      &globalSidechainVolumeMenu,     &sidechainAttackMenu,           &sidechainShapeMenu,  nullptr,                &bassMenu,                &bassFreqMenu                      },
    {nullptr,                 nullptr,                 nullptr,                        nullptr,                        nullptr,              nullptr,                &trebleMenu,              &trebleFreqMenu                    },
    {nullptr,                 nullptr,                 nullptr,                        &modFXTypeMenu,           	   &modFXOffsetMenu,     &modFXFeedbackMenu,     &globalModFXDepthMenu,    &globalModFXRateMenu            	  },
    {nullptr,                 nullptr,                 nullptr,                        &globalReverbSendAmountMenu,    &reverbPanMenu,       &reverbWidthMenu,       &reverbDampingMenu,       &reverbRoomSizeMenu                },
    {&globalDelayRateMenu, 	  &delaySyncMenu,          &delayAnalogMenu,               &globalDelayFeedbackMenu,       &delayPingPongMenu,   nullptr,                nullptr,                  nullptr                            },
};

MenuItem* paramShortcutsForSongView[][8] = {
    {nullptr,                 nullptr,                 nullptr,                        nullptr,                        nullptr,              nullptr,                nullptr,                  nullptr                            },
    {nullptr,                 nullptr,                 nullptr,                        nullptr,                        nullptr,              nullptr,                nullptr,                  nullptr                            },
    {nullptr,                 nullptr,                 nullptr,                        nullptr,                        nullptr,              nullptr,                nullptr,                  nullptr                            },
    {nullptr,                 nullptr,                 nullptr,                        nullptr,                        nullptr,              nullptr,                nullptr,                  nullptr                            },
    {nullptr,                 nullptr,                 nullptr,                        nullptr,                        nullptr,              nullptr,                nullptr,                  nullptr                            },
    {nullptr,                 nullptr,                 nullptr,                        nullptr,                        nullptr,              nullptr,                nullptr,                  &globalStutterRateMenu             },
    {&globalLevelMenu,        nullptr,                 nullptr,                        &globalPanMenu,                 nullptr,              &srrMenu,               &bitcrushMenu,            nullptr                            },
    {nullptr,                 nullptr,                 nullptr,                        nullptr,                        nullptr,              nullptr,                nullptr,                  nullptr                            },
    {nullptr,                 nullptr,                 nullptr,                        nullptr,                        &globalLPFMorphMenu,  &lpfModeMenu,           &globalLPFResMenu,        &globalLPFFreqMenu                 },
    {nullptr,                 nullptr,                 nullptr,                        nullptr,                        &globalHPFMorphMenu,  &hpfModeMenu,           &globalHPFResMenu,        &globalHPFFreqMenu                 },
    {nullptr,                 nullptr,                 nullptr,                        nullptr,                        nullptr,              nullptr,                &bassMenu,                &bassFreqMenu                      },
    {nullptr,                 nullptr,                 nullptr,                        nullptr,                        nullptr,              nullptr,                &trebleMenu,              &trebleFreqMenu                    },
    {nullptr,                 nullptr,                 nullptr,                        &modFXTypeMenu,                 &modFXOffsetMenu,     &modFXFeedbackMenu,     &globalModFXDepthMenu,    &globalModFXRateMenu               },
    {nullptr,                 nullptr,                 nullptr,                        &globalReverbSendAmountMenu,    &reverbPanMenu,       &reverbWidthMenu,       &reverbDampingMenu,       &reverbRoomSizeMenu                },
    {&globalDelayRateMenu,    &delaySyncMenu,          &delayAnalogMenu,               &globalDelayFeedbackMenu,       &delayPingPongMenu,   nullptr,                nullptr,                  nullptr                            },
};

MenuItem* paramShortcutsForKitGlobalFX[][8] = {
    {nullptr,                 nullptr,                 nullptr,                        nullptr,                        nullptr,              nullptr,                nullptr,                  nullptr                            },
    {nullptr,                 nullptr,                 nullptr,                        nullptr,                        nullptr,              nullptr,                nullptr,                  nullptr                            },
    {nullptr,                 nullptr,                 nullptr,                        nullptr,                        nullptr,              nullptr,                nullptr,                  nullptr                            },
    {nullptr,                 nullptr,                 nullptr,                        nullptr,                        nullptr,              nullptr,                nullptr,                  nullptr                            },
    {nullptr,                 nullptr,                 nullptr,                        nullptr,                        nullptr,              nullptr,                nullptr,                  nullptr                            },
    {nullptr,                 nullptr,                 nullptr,                        nullptr,                        nullptr,              nullptr,                nullptr,                  nullptr             				  },
    {&globalLevelMenu,        &globalPitchMenu,        nullptr,                        &globalPanMenu,                 nullptr,              &srrMenu,               &bitcrushMenu,            nullptr                            },
    {nullptr,              	  nullptr,                 nullptr,                        nullptr,                        nullptr,              nullptr,                nullptr,                  nullptr                            },
    {nullptr,                 nullptr,                 nullptr,                        nullptr,                        &globalLPFMorphMenu,  &lpfModeMenu,           &globalLPFResMenu,        &globalLPFFreqMenu                 },
    {nullptr,                 nullptr,                 nullptr,                        nullptr,                        &globalHPFMorphMenu,  &hpfModeMenu,           &globalHPFResMenu,        &globalHPFFreqMenu                 },
    {&sidechainReleaseMenu,   &sidechainSyncMenu,      &globalSidechainVolumeMenu,     &sidechainAttackMenu,           &sidechainShapeMenu,  nullptr,                &bassMenu,                &bassFreqMenu                      },
    {nullptr,                 nullptr,            	   nullptr,                        nullptr,                		   nullptr,         	 nullptr,                &trebleMenu,              &trebleFreqMenu                    },
    {nullptr,                 nullptr,                 nullptr,                        &modFXTypeMenu,                 &modFXOffsetMenu,     &modFXFeedbackMenu,     &globalModFXDepthMenu,    &globalModFXRateMenu               },
    {nullptr,                 nullptr,                 nullptr,                        &globalReverbSendAmountMenu,    &reverbPanMenu,       &reverbWidthMenu,       &reverbDampingMenu,       &reverbRoomSizeMenu                },
    {&globalDelayRateMenu,    &delaySyncMenu,          &delayAnalogMenu,               &globalDelayFeedbackMenu,       &delayPingPongMenu,   nullptr,                nullptr,                  nullptr                            },
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

void setEnvelopeNumberForTitles(int32_t num) {
	num += 1;
	envAttackMenu.format(num);
	envDecayMenu.format(num);
	envSustainMenu.format(num);
	envReleaseMenu.format(num);
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
