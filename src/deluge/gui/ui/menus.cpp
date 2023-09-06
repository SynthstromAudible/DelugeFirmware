#include "gui/l10n/l10n.h"
#include "gui/l10n/strings.h"
#include "gui/menu_item/arpeggiator/gate.h"
#include "gui/menu_item/arpeggiator/midi_cv/gate.h"
#include "gui/menu_item/arpeggiator/midi_cv/rate.h"
#include "gui/menu_item/arpeggiator/mode.h"
#include "gui/menu_item/arpeggiator/octaves.h"
#include "gui/menu_item/arpeggiator/rate.h"
#include "gui/menu_item/arpeggiator/sync.h"
#include "gui/menu_item/audio_clip/attack.h"
#include "gui/menu_item/audio_clip/hpf_freq.h"
#include "gui/menu_item/audio_clip/lpf_freq.h"
#include "gui/menu_item/audio_clip/mod_fx/type.h"
#include "gui/menu_item/audio_clip/reverse.h"
#include "gui/menu_item/audio_clip/sample_marker_editor.h"
#include "gui/menu_item/audio_clip/transpose.h"
#include "gui/menu_item/bend_range.h"
#include "gui/menu_item/bend_range/main.h"
#include "gui/menu_item/bend_range/per_finger.h"
#include "gui/menu_item/colour.h"
#include "gui/menu_item/compressor/attack.h"
#include "gui/menu_item/compressor/release.h"
#include "gui/menu_item/compressor/volume.h"
#include "gui/menu_item/cv/selection.h"
#include "gui/menu_item/cv/submenu.h"
#include "gui/menu_item/cv/transpose.h"
#include "gui/menu_item/cv/volts.h"
#include "gui/menu_item/decimal.h"
#include "gui/menu_item/defaults/bend_range.h"
#include "gui/menu_item/defaults/delay_analog.h"
#include "gui/menu_item/defaults/delay_ping_pong.h"
#include "gui/menu_item/defaults/delay_sync.h"
#include "gui/menu_item/defaults/magnitude.h"
#include "gui/menu_item/defaults/scale.h"
#include "gui/menu_item/defaults/velocity.h"
#include "gui/menu_item/delay/analog.h"
#include "gui/menu_item/delay/ping_pong.h"
#include "gui/menu_item/delay/sync.h"
#include "gui/menu_item/dev_var/dev_var.h"
#include "gui/menu_item/drum_name.h"
#include "gui/menu_item/envelope/segment.h"
#include "gui/menu_item/file_selector.h"
#include "gui/menu_item/filter/hpf_freq.h"
#include "gui/menu_item/filter/hpf_mode.h"
#include "gui/menu_item/filter/lpf_freq.h"
#include "gui/menu_item/filter/lpf_mode.h"
#include "gui/menu_item/filter_route.h"
#include "gui/menu_item/firmware/version.h"
#include "gui/menu_item/flash/status.h"
#include "gui/menu_item/fx/clipping.h"
#include "gui/menu_item/gate/mode.h"
#include "gui/menu_item/gate/off_time.h"
#include "gui/menu_item/gate/selection.h"
#include "gui/menu_item/integer.h"
#include "gui/menu_item/integer_range.h"
#include "gui/menu_item/key_range.h"
#include "gui/menu_item/keyboard/layout.h"
#include "gui/menu_item/lfo/global/rate.h"
#include "gui/menu_item/lfo/global/sync.h"
#include "gui/menu_item/lfo/global/type.h"
#include "gui/menu_item/lfo/local/type.h"
#include "gui/menu_item/lfo/shape.h"
#include "gui/menu_item/master_transpose.h"
#include "gui/menu_item/menu_item.h"
#include "gui/menu_item/midi/bank.h"
#include "gui/menu_item/midi/clock_in_status.h"
#include "gui/menu_item/midi/clock_out_status.h"
#include "gui/menu_item/midi/command.h"
#include "gui/menu_item/midi/default_velocity_to_level.h"
#include "gui/menu_item/midi/device.h"
#include "gui/menu_item/midi/devices.h"
#include "gui/menu_item/midi/input_differentiation.h"
#include "gui/menu_item/midi/pgm.h"
#include "gui/menu_item/midi/preset.h"
#include "gui/menu_item/midi/sub.h"
#include "gui/menu_item/midi/takeover.h"
#include "gui/menu_item/midi/thru.h"
#include "gui/menu_item/mod_fx/depth.h"
#include "gui/menu_item/mod_fx/feedback.h"
#include "gui/menu_item/mod_fx/offset.h"
#include "gui/menu_item/mod_fx/type.h"
#include "gui/menu_item/modulator/destination.h"
#include "gui/menu_item/modulator/transpose.h"
#include "gui/menu_item/monitor/mode.h"
#include "gui/menu_item/mpe/direction_selector.h"
#include "gui/menu_item/mpe/zone_num_member_channels.h"
#include "gui/menu_item/mpe/zone_selector.h"
#include "gui/menu_item/multi_range.h"
#include "gui/menu_item/number.h"
#include "gui/menu_item/osc/audio_recorder.h"
#include "gui/menu_item/osc/pulse_width.h"
#include "gui/menu_item/osc/retrigger_phase.h"
#include "gui/menu_item/osc/source/feedback.h"
#include "gui/menu_item/osc/source/volume.h"
#include "gui/menu_item/osc/source/wave_index.h"
#include "gui/menu_item/osc/sync.h"
#include "gui/menu_item/osc/type.h"
#include "gui/menu_item/param.h"
#include "gui/menu_item/patch_cable_strength.h"
#include "gui/menu_item/patch_cable_strength/fixed.h"
#include "gui/menu_item/patch_cable_strength/range.h"
#include "gui/menu_item/patch_cable_strength/regular.h"
#include "gui/menu_item/patch_cables.h"
#include "gui/menu_item/patched_param.h"
#include "gui/menu_item/patched_param/integer.h"
#include "gui/menu_item/patched_param/integer_non_fm.h"
#include "gui/menu_item/patched_param/pan.h"
#include "gui/menu_item/ppqn.h"
#include "gui/menu_item/range.h"
#include "gui/menu_item/record/count_in.h"
#include "gui/menu_item/record/margins.h"
#include "gui/menu_item/record/quantize.h"
#include "gui/menu_item/reverb/compressor/shape.h"
#include "gui/menu_item/reverb/compressor/volume.h"
#include "gui/menu_item/reverb/dampening.h"
#include "gui/menu_item/reverb/pan.h"
#include "gui/menu_item/reverb/room_size.h"
#include "gui/menu_item/reverb/width.h"
#include "gui/menu_item/runtime_feature/setting.h"
#include "gui/menu_item/runtime_feature/settings.h"
#include "gui/menu_item/sample/browser_preview/mode.h"
#include "gui/menu_item/sample/end.h"
#include "gui/menu_item/sample/interpolation.h"
#include "gui/menu_item/sample/loop_point.h"
#include "gui/menu_item/sample/pitch_speed.h"
#include "gui/menu_item/sample/repeat.h"
#include "gui/menu_item/sample/reverse.h"
#include "gui/menu_item/sample/selection.h"
#include "gui/menu_item/sample/start.h"
#include "gui/menu_item/sample/time_stretch.h"
#include "gui/menu_item/sample/transpose.h"
#include "gui/menu_item/sequence/direction.h"
#include "gui/menu_item/shortcuts/version.h"
#include "gui/menu_item/sidechain/send.h"
#include "gui/menu_item/sidechain/sync.h"
#include "gui/menu_item/source/patched_param.h"
#include "gui/menu_item/source/patched_param/fm.h"
#include "gui/menu_item/source/transpose.h"
#include "gui/menu_item/source_selection.h"
#include "gui/menu_item/source_selection/range.h"
#include "gui/menu_item/source_selection/regular.h"
#include "gui/menu_item/submenu.h"
#include "gui/menu_item/submenu/actual_source.h"
#include "gui/menu_item/submenu/arpeggiator.h"
#include "gui/menu_item/submenu/bend.h"
#include "gui/menu_item/submenu/compressor.h"
#include "gui/menu_item/submenu/envelope.h"
#include "gui/menu_item/submenu/filter.h"
#include "gui/menu_item/submenu/modulator.h"
#include "gui/menu_item/submenu_referring_to_one_thing.h"
#include "gui/menu_item/swing/interval.h"
#include "gui/menu_item/sync_level.h"
#include "gui/menu_item/sync_level/relative_to_song.h"
#include "gui/menu_item/synth_mode.h"
#include "gui/menu_item/tempo/magnitude_matching.h"
#include "gui/menu_item/transpose.h"
#include "gui/menu_item/trigger/in/auto_start.h"
#include "gui/menu_item/trigger/in/ppqn.h"
#include "gui/menu_item/trigger/out/ppqn.h"
#include "gui/menu_item/unison/count.h"
#include "gui/menu_item/unison/detune.h"
#include "gui/menu_item/unison/stereoSpread.h"
#include "gui/menu_item/unpatched_param.h"
#include "gui/menu_item/unpatched_param/pan.h"
#include "gui/menu_item/unpatched_param/updating_reverb_params.h"
#include "gui/menu_item/value.h"
#include "gui/menu_item/voice/polyphony.h"
#include "gui/menu_item/voice/priority.h"
#include "processing/sound/sound.h"

using namespace deluge;
using namespace gui;
using namespace gui::menu_item;
using enum l10n::String;

// Dev vars
dev_var::AMenu devVarAMenu{STRING_FOR_DEV_MENU_A};
dev_var::BMenu devVarBMenu{STRING_FOR_DEV_MENU_B};
dev_var::CMenu devVarCMenu{STRING_FOR_DEV_MENU_C};
dev_var::DMenu devVarDMenu{STRING_FOR_DEV_MENU_D};
dev_var::EMenu devVarEMenu{STRING_FOR_DEV_MENU_E};
dev_var::FMenu devVarFMenu{STRING_FOR_DEV_MENU_F};
dev_var::GMenu devVarGMenu{STRING_FOR_DEV_MENU_G};

// LPF menu ----------------------------------------------------------------------------------------------------

filter::LPFFreq lpfFreqMenu{STRING_FOR_FREQUENCY, STRING_FOR_LPF_FREQUENCY, ::Param::Local::LPF_FREQ};
patched_param::IntegerNonFM lpfResMenu{STRING_FOR_RESONANCE, STRING_FOR_LPF_RESONANCE, ::Param::Local::LPF_RESONANCE};
patched_param::IntegerNonFM lpfMorphMenu{STRING_FOR_MORPH, STRING_FOR_LPF_MORPH, ::Param::Local::LPF_MORPH};
filter::LPFMode lpfModeMenu{STRING_FOR_MODE, STRING_FOR_LPF_MODE};

submenu::Filter lpfMenu{
    STRING_FOR_LPF,
    {
        &lpfFreqMenu,
        &lpfResMenu,
        &lpfModeMenu,
        &lpfMorphMenu,
    },
};

// HPF menu ----------------------------------------------------------------------------------------------------

filter::HPFFreq hpfFreqMenu{STRING_FOR_FREQUENCY, STRING_FOR_HPF_FREQUENCY, ::Param::Local::HPF_FREQ};
patched_param::IntegerNonFM hpfResMenu{STRING_FOR_RESONANCE, STRING_FOR_HPF_RESONANCE, ::Param::Local::HPF_RESONANCE};
patched_param::IntegerNonFM hpfMorphMenu{STRING_FOR_MORPH, STRING_FOR_HPF_MORPH, ::Param::Local::HPF_MORPH};
filter::HPFMode hpfModemenu{STRING_FOR_MODE, STRING_FOR_HPF_MODE};

submenu::Filter hpfMenu{
    STRING_FOR_HPF,
    {
        &hpfFreqMenu,
        &hpfResMenu,
        &hpfModemenu,
        &hpfMorphMenu,
    },
};

//Filter Route Menu ----------------------------------------------------------------------------------------------
FilterRouting filterRoutingMenu{STRING_FOR_FILTER_ROUTE};

// Envelope menu ----------------------------------------------------------------------------------------------------

envelope::Segment envAttackMenu{STRING_FOR_ATTACK, STRING_FOR_ENV_ATTACK_MENU_TITLE, ::Param::Local::ENV_0_ATTACK};
envelope::Segment envDecayMenu{STRING_FOR_DECAY, STRING_FOR_ENV_DECAY_MENU_TITLE, ::Param::Local::ENV_0_DECAY};
envelope::Segment envSustainMenu{STRING_FOR_SUSTAIN, STRING_FOR_ENV_SUSTAIN_MENU_TITLE, ::Param::Local::ENV_0_SUSTAIN};
envelope::Segment envReleaseMenu{STRING_FOR_RELEASE, STRING_FOR_ENV_RELEASE_MENU_TITLE, ::Param::Local::ENV_0_RELEASE};

MenuItem* envMenuItems[] = {
    &envAttackMenu,
    &envDecayMenu,
    &envSustainMenu,
    &envReleaseMenu,
};
submenu::Envelope env0Menu{STRING_FOR_ENVELOPE_1, envMenuItems, 0};
submenu::Envelope env1Menu{STRING_FOR_ENVELOPE_2, envMenuItems, 1};

// Osc menu -------------------------------------------------------------------------------------------------------

osc::Type oscTypeMenu{STRING_FOR_TYPE, STRING_FOR_OSC_TYPE_MENU_TITLE};
osc::source::WaveIndex sourceWaveIndexMenu{STRING_FOR_WAVE_INDEX, STRING_FOR_OSC_WAVE_IND_MENU_TITLE,
                                           ::Param::Local::OSC_A_WAVE_INDEX};
osc::source::Volume sourceVolumeMenu{STRING_FOR_VOLUME_LEVEL, STRING_FOR_OSC_LEVEL_MENU_TITLE,
                                     ::Param::Local::OSC_A_VOLUME};
osc::source::Feedback sourceFeedbackMenu{STRING_FOR_FEEDBACK, STRING_FOR_CARRIER_FEED_MENU_TITLE,
                                         ::Param::Local::CARRIER_0_FEEDBACK};
osc::AudioRecorder audioRecorderMenu{STRING_FOR_RECORD_AUDIO};
sample::Reverse sampleReverseMenu{STRING_FOR_REVERSE, STRING_FOR_SAMP_REVERSE_MENU_TITLE};
sample::Repeat sampleRepeatMenu{STRING_FOR_REPEAT_MODE, STRING_FOR_SAMP_REPEAT_MENU_TITLE};
sample::Start sampleStartMenu{STRING_FOR_START_POINT};
sample::End sampleEndMenu{STRING_FOR_END_POINT};
sample::Transpose sourceTransposeMenu{STRING_FOR_TRANSPOSE, STRING_FOR_OSC_TRANSPOSE_MENU_TITLE,
                                      ::Param::Local::OSC_A_PITCH_ADJUST};
sample::PitchSpeed samplePitchSpeedMenu{STRING_FOR_PITCH_SPEED};
sample::TimeStretch timeStretchMenu{STRING_FOR_SPEED, STRING_FOR_SAMP_SPEED_MENU_TITLE};
sample::Interpolation interpolationMenu{STRING_FOR_INTERPOLATION, STRING_FOR_SAMP_INTERP_MENU_TITLE};
osc::PulseWidth pulseWidthMenu{STRING_FOR_PULSE_WIDTH, STRING_FOR_OSC_P_WIDTH_MENU_TITLE,
                               ::Param::Local::OSC_A_PHASE_WIDTH};
osc::Sync oscSyncMenu{STRING_FOR_OSCILLATOR_SYNC};
osc::RetriggerPhase oscPhaseMenu{STRING_FOR_RETRIGGER_PHASE, STRING_FOR_OSC_R_PHASE_MENU_TITLE, false};

MenuItem* oscMenuItems[] = {
    &oscTypeMenu,         &sourceVolumeMenu,     &sourceWaveIndexMenu, &sourceFeedbackMenu, &fileSelectorMenu,
    &audioRecorderMenu,   &sampleReverseMenu,    &sampleRepeatMenu,    &sampleStartMenu,    &sampleEndMenu,
    &sourceTransposeMenu, &samplePitchSpeedMenu, &timeStretchMenu,     &interpolationMenu,  &pulseWidthMenu,
    &oscSyncMenu,         &oscPhaseMenu,
};

submenu::ActualSource source0Menu{STRING_FOR_OSCILLATOR_1, oscMenuItems, 0};
submenu::ActualSource source1Menu{STRING_FOR_OSCILLATOR_2, oscMenuItems, 1};

// Unison --------------------------------------------------------------------------------------

unison::Count numUnisonMenu{STRING_FOR_UNISON_NUMBER};
unison::Detune unisonDetuneMenu{STRING_FOR_UNISON_DETUNE};
unison::StereoSpread unison::stereoSpreadMenu{STRING_FOR_UNISON_STEREO_SPREAD};

Submenu unisonMenu{
    STRING_FOR_UNISON,
    {
        &numUnisonMenu,
        &unisonDetuneMenu,
        &unison::stereoSpreadMenu,
    },
};

// Arp --------------------------------------------------------------------------------------
arpeggiator::Mode arpModeMenu{STRING_FOR_MODE, STRING_FOR_ARP_MODE_MENU_TITLE};
arpeggiator::Sync arpSyncMenu{STRING_FOR_SYNC, STRING_FOR_ARP_SYNC_MENU_TITLE};
arpeggiator::Octaves arpOctavesMenu{STRING_FOR_NUMBER_OF_OCTAVES, STRING_FOR_ARP_OCTAVES_MENU_TITLE};
arpeggiator::Gate arpGateMenu{STRING_FOR_GATE, STRING_FOR_ARP_GATE_MENU_TITLE, ::Param::Unpatched::Sound::ARP_GATE};
arpeggiator::midi_cv::Gate arpGateMenuMIDIOrCV{STRING_FOR_GATE, STRING_FOR_ARP_GATE_MENU_TITLE};
arpeggiator::Rate arpRateMenu{STRING_FOR_RATE, STRING_FOR_ARP_RATE_MENU_TITLE, ::Param::Global::ARP_RATE};
arpeggiator::midi_cv::Rate arpRateMenuMIDIOrCV{STRING_FOR_RATE, STRING_FOR_ARP_RATE_MENU_TITLE};

submenu::Arpeggiator arpMenu{
    STRING_FOR_ARPEGGIATOR,
    {
        &arpModeMenu,
        &arpSyncMenu,
        &arpOctavesMenu,
        &arpGateMenu,
        &arpGateMenuMIDIOrCV,
        &arpRateMenu,
        &arpRateMenuMIDIOrCV,
    },
};

// Voice menu ----------------------------------------------------------------------------------------------------

voice::Polyphony polyphonyMenu{STRING_FOR_POLYPHONY};
UnpatchedParam portaMenu{STRING_FOR_PORTAMENTO, ::Param::Unpatched::Sound::PORTAMENTO};
voice::Priority priorityMenu{STRING_FOR_PRIORITY};

Submenu voiceMenu{STRING_FOR_VOICE, {&polyphonyMenu, &unisonMenu, &portaMenu, &arpMenu, &priorityMenu}};

// Modulator menu -----------------------------------------------------------------------

modulator::Transpose modulatorTransposeMenu{STRING_FOR_TRANSPOSE, STRING_FOR_FM_MOD_TRAN_MENU_TITLE,
                                            ::Param::Local::MODULATOR_0_PITCH_ADJUST};
source::patched_param::FM modulatorVolume{STRING_FOR_AMOUNT_LEVEL, STRING_FOR_FM_MOD_LEVEL_MENU_TITLE,
                                          ::Param::Local::MODULATOR_0_VOLUME};
source::patched_param::FM modulatorFeedbackMenu{STRING_FOR_FEEDBACK, STRING_FOR_FM_MOD_FBACK_MENU_TITLE,
                                                ::Param::Local::MODULATOR_0_FEEDBACK};
modulator::Destination modulatorDestMenu{STRING_FOR_DESTINATION, STRING_FOR_FM_MOD2_DEST_MENU_TITLE};
osc::RetriggerPhase modulatorPhaseMenu{STRING_FOR_RETRIGGER_PHASE, STRING_FOR_FM_MOD_RETRIG_MENU_TITLE, true};

// LFO1 menu ---------------------------------------------------------------------------------

lfo::global::Type lfo1TypeMenu{STRING_FOR_SHAPE, STRING_FOR_LFO1_TYPE};
lfo::global::Rate lfo1RateMenu{STRING_FOR_RATE, STRING_FOR_LFO1_RATE, ::Param::Global::LFO_FREQ};
lfo::global::Sync lfo1SyncMenu{STRING_FOR_SYNC, STRING_FOR_LFO1_SYNC};

Submenu lfo0Menu{STRING_FOR_LFO1, {&lfo1TypeMenu, &lfo1RateMenu, &lfo1SyncMenu}};

// LFO2 menu ---------------------------------------------------------------------------------
lfo::local::Type lfo2TypeMenu{STRING_FOR_SHAPE, STRING_FOR_LFO2_TYPE};
patched_param::Integer lfo2RateMenu{STRING_FOR_RATE, STRING_FOR_LFO2_RATE, ::Param::Local::LFO_LOCAL_FREQ};

Submenu lfo1Menu{STRING_FOR_LFO2, {&lfo2TypeMenu, &lfo2RateMenu}};

// Mod FX ----------------------------------------------------------------------------------
mod_fx::Type modFXTypeMenu{STRING_FOR_TYPE, STRING_FOR_MODFX_TYPE};
patched_param::Integer modFXRateMenu{STRING_FOR_RATE, STRING_FOR_MODFX_RATE, ::Param::Global::MOD_FX_RATE};
mod_fx::Feedback modFXFeedbackMenu{STRING_FOR_FEEDBACK, STRING_FOR_MODFX_FEEDBACK, ::Param::Unpatched::MOD_FX_FEEDBACK};
mod_fx::Depth modFXDepthMenu{STRING_FOR_DEPTH, STRING_FOR_MODFX_DEPTH, ::Param::Global::MOD_FX_DEPTH};
mod_fx::Offset modFXOffsetMenu{STRING_FOR_OFFSET, STRING_FOR_MODFX_OFFSET, ::Param::Unpatched::MOD_FX_OFFSET};

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
UnpatchedParam bassMenu{STRING_FOR_BASS, ::Param::Unpatched::BASS};
UnpatchedParam trebleMenu{STRING_FOR_TREBLE, ::Param::Unpatched::TREBLE};
UnpatchedParam bassFreqMenu{STRING_FOR_BASS_FREQUENCY, ::Param::Unpatched::BASS_FREQ};
UnpatchedParam trebleFreqMenu{STRING_FOR_TREBLE_FREQUENCY, ::Param::Unpatched::TREBLE_FREQ};

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
patched_param::Integer delayFeedbackMenu{STRING_FOR_AMOUNT, STRING_FOR_DELAY_AMOUNT, ::Param::Global::DELAY_FEEDBACK};
patched_param::Integer delayRateMenu{STRING_FOR_RATE, STRING_FOR_DELAY_RATE, ::Param::Global::DELAY_RATE};
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

// Sidechain/Compressor-----------------------------------------------------------------------

sidechain::Send sidechainSendMenu{STRING_FOR_SEND_TO_SIDECHAIN, STRING_FOR_SEND_TO_SIDECH_MENU_TITLE};
compressor::VolumeShortcut compressorVolumeShortcutMenu{
    STRING_FOR_VOLUME_DUCKING, ::Param::Global::VOLUME_POST_REVERB_SEND, PatchSource::COMPRESSOR};
reverb::compressor::Volume reverbCompressorVolumeMenu{STRING_FOR_VOLUME_DUCKING};
sidechain::Sync sidechainSyncMenu{STRING_FOR_SYNC, STRING_FOR_SIDECHAIN_SYNC};
compressor::Attack compressorAttackMenu{STRING_FOR_ATTACK, STRING_FOR_SIDECH_ATTACK_MENU_TITLE};
compressor::Release compressorReleaseMenu{STRING_FOR_RELEASE, STRING_FOR_SIDECH_RELEASE_MENU_TITLE};
unpatched_param::UpdatingReverbParams compressorShapeMenu{STRING_FOR_SHAPE, STRING_FOR_SIDECH_SHAPE_MENU_TITLE,
                                                          ::Param::Unpatched::COMPRESSOR_SHAPE};
reverb::compressor::Shape reverbCompressorShapeMenu{STRING_FOR_SHAPE, STRING_FOR_SIDECH_SHAPE_MENU_TITLE};

submenu::Compressor compressorMenu{
    STRING_FOR_SIDECHAIN_COMPRESSOR,
    STRING_FOR_SIDECHAIN_COMP_MENU_TITLE,
    {
        &sidechainSendMenu,
        &compressorVolumeShortcutMenu,
        &sidechainSyncMenu,
        &compressorAttackMenu,
        &compressorReleaseMenu,
        &compressorShapeMenu,
    },
    false,
};

submenu::Compressor reverbCompressorMenu{
    STRING_FOR_REVERB_SIDECHAIN,
    STRING_FOR_REVERB_SIDECH_MENU_TITLE,
    {
        &reverbCompressorVolumeMenu,
        &sidechainSyncMenu,
        &compressorAttackMenu,
        &compressorReleaseMenu,
        &reverbCompressorShapeMenu,
    },
    true,
};

// Reverb ----------------------------------------------------------------------------------
patched_param::Integer reverbAmountMenu{STRING_FOR_AMOUNT, STRING_FOR_REVERB_AMOUNT, ::Param::Global::REVERB_AMOUNT};
reverb::RoomSize reverbRoomSizeMenu{STRING_FOR_ROOM_SIZE};
reverb::Dampening reverbDampeningMenu{STRING_FOR_DAMPENING};
reverb::Width reverbWidthMenu{STRING_FOR_WIDTH, STRING_FOR_REVERB_WIDTH};
reverb::Pan reverbPanMenu{STRING_FOR_PAN, STRING_FOR_REVERB_PAN};

Submenu reverbMenu{
    STRING_FOR_REVERB,
    {
        &reverbAmountMenu,
        &reverbRoomSizeMenu,
        &reverbDampeningMenu,
        &reverbWidthMenu,
        &reverbPanMenu,
        &reverbCompressorMenu,
    },
};

// FX ----------------------------------------------------------------------------------------

fx::Clipping clippingMenu{STRING_FOR_SATURATION};
UnpatchedParam srrMenu{STRING_FOR_DECIMATION, ::Param::Unpatched::SAMPLE_RATE_REDUCTION};
UnpatchedParam bitcrushMenu{STRING_FOR_BITCRUSH, ::Param::Unpatched::BITCRUSHING};
patched_param::Integer foldMenu{STRING_FOR_WAVEFOLD, STRING_FOR_WAVEFOLD, ::Param::Local::FOLD};
Submenu fxMenu{
    STRING_FOR_FX,
    {
        &modFXMenu,
        &eqMenu,
        &delayMenu,
        &reverbMenu,
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

// Clip-level stuff --------------------------------------------------------------------------

sequence::Direction sequenceDirectionMenu{STRING_FOR_PLAY_DIRECTION};

// AudioClip stuff ---------------------------------------------------------------------------

// Sample Menu
audio_clip::Reverse audioClipReverseMenu{STRING_FOR_REVERSE};
audio_clip::SampleMarkerEditor audioClipSampleMarkerEditorMenuStart{EMPTY_STRING, MarkerType::START};
audio_clip::SampleMarkerEditor audioClipSampleMarkerEditorMenuEnd{STRING_FOR_WAVEFORM, MarkerType::END};

Submenu audioClipSampleMenu{
    STRING_FOR_SAMPLE,
    {
        &fileSelectorMenu,
        &audioClipReverseMenu,
        &samplePitchSpeedMenu,
        &audioClipSampleMarkerEditorMenuEnd,
        &interpolationMenu,
    },
};

// LPF Menu
audio_clip::LPFFreq audioClipLPFFreqMenu{STRING_FOR_FREQUENCY, STRING_FOR_LPF_FREQUENCY,
                                         ::Param::Unpatched::GlobalEffectable::LPF_FREQ};
UnpatchedParam audioClipLPFResMenu{STRING_FOR_RESONANCE, STRING_FOR_LPF_RESONANCE,
                                   ::Param::Unpatched::GlobalEffectable::LPF_RES};

Submenu audioClipLPFMenu{
    STRING_FOR_LPF,
    {
        &audioClipLPFFreqMenu,
        &audioClipLPFResMenu,
        &lpfModeMenu,
    },
};

// HPF Menu
audio_clip::HPFFreq audioClipHPFFreqMenu{STRING_FOR_FREQUENCY, STRING_FOR_HPF_FREQUENCY,
                                         ::Param::Unpatched::GlobalEffectable::HPF_FREQ};
UnpatchedParam audioClipHPFResMenu{STRING_FOR_RESONANCE, STRING_FOR_HPF_RESONANCE,
                                   ::Param::Unpatched::GlobalEffectable::HPF_RES};

Submenu audioClipHPFMenu{
    STRING_FOR_HPF,
    {
        &audioClipHPFFreqMenu,
        &audioClipHPFResMenu,
        &hpfModemenu,
    },
};

// Mod FX Menu
audio_clip::mod_fx::Type audioClipModFXTypeMenu{STRING_FOR_TYPE, STRING_FOR_MOD_FX_TYPE};
UnpatchedParam audioClipModFXRateMenu{STRING_FOR_RATE, STRING_FOR_MOD_FX_RATE,
                                      ::Param::Unpatched::GlobalEffectable::MOD_FX_RATE};
UnpatchedParam audioClipModFXDepthMenu{STRING_FOR_DEPTH, STRING_FOR_MOD_FX_DEPTH,
                                       ::Param::Unpatched::GlobalEffectable::MOD_FX_DEPTH};

Submenu audioClipModFXMenu{
    STRING_FOR_MOD_FX,
    {
        &audioClipModFXTypeMenu,
        &audioClipModFXRateMenu,
        &modFXFeedbackMenu,
        &audioClipModFXDepthMenu,
        &modFXOffsetMenu,
    },
};

// Delay Menu
UnpatchedParam audioClipDelayRateMenu{STRING_FOR_AMOUNT, STRING_FOR_DELAY_AMOUNT,
                                      ::Param::Unpatched::GlobalEffectable::DELAY_AMOUNT};
UnpatchedParam audioClipDelayFeedbackMenu{STRING_FOR_RATE, STRING_FOR_DELAY_RATE,
                                          ::Param::Unpatched::GlobalEffectable::DELAY_RATE};

Submenu audioClipDelayMenu{
    STRING_FOR_DELAY,
    {
        &audioClipDelayFeedbackMenu,
        &audioClipDelayRateMenu,
        &delayPingPongMenu,
        &delayAnalogMenu,
        &delaySyncMenu,
    },
};

// Reverb Menu
UnpatchedParam audioClipReverbSendAmountMenu{
    STRING_FOR_AMOUNT,
    STRING_FOR_REVERB_AMOUNT,
    ::Param::Unpatched::GlobalEffectable::REVERB_SEND_AMOUNT,
};
Submenu audioClipReverbMenu{
    STRING_FOR_REVERB,
    {
        &audioClipReverbSendAmountMenu,
        &reverbRoomSizeMenu,
        &reverbDampeningMenu,
        &reverbWidthMenu,
        &reverbPanMenu,
        &reverbCompressorMenu,
    },
};

// Sidechain menu
unpatched_param::UpdatingReverbParams audioClipCompressorVolumeMenu{
    STRING_FOR_VOLUME_DUCKING, ::Param::Unpatched::GlobalEffectable::SIDECHAIN_VOLUME};

Submenu audioClipCompressorMenu{
    STRING_FOR_SIDECHAIN_COMPRESSOR,
    {
        &audioClipCompressorVolumeMenu,
        &sidechainSyncMenu,
        &compressorAttackMenu,
        &compressorReleaseMenu,
        &compressorShapeMenu,
    },
};

audio_clip::Transpose audioClipTransposeMenu{STRING_FOR_TRANSPOSE};
audio_clip::Attack audioClipAttackMenu{STRING_FOR_ATTACK};

Submenu audioClipFXMenu{
    STRING_FOR_FX,
    {
        &audioClipModFXMenu,
        &eqMenu,
        &audioClipDelayMenu,
        &audioClipReverbMenu,
        &clippingMenu,
        &srrMenu,
        &bitcrushMenu,
    },
};

UnpatchedParam audioClipLevelMenu{STRING_FOR_VOLUME_LEVEL, ::Param::Unpatched::GlobalEffectable::VOLUME};
unpatched_param::Pan audioClipPanMenu{STRING_FOR_PAN, ::Param::Unpatched::GlobalEffectable::PAN};

const MenuItem* midiOrCVParamShortcuts[8] = {
    &arpRateMenuMIDIOrCV, &arpSyncMenu, &arpGateMenuMIDIOrCV, &arpOctavesMenu, &arpModeMenu, nullptr, nullptr, nullptr,
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
record::Margins recordMarginsMenu{STRING_FOR_LOOP_MARGINS};
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
midi::Thru midiThruMenu{STRING_FOR_MIDI_THRU};

// MIDI Takeover
midi::Takeover midiTakeoverMenu{STRING_FOR_TAKEOVER};

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

Submenu midiCommandsMenu{
    STRING_FOR_COMMANDS,
    STRING_FOR_MIDI_COMMANDS,
    {
        &playMidiCommand,
        &playbackRestartMidiCommand,
        &recordMidiCommand,
        &tapMidiCommand,
        &undoMidiCommand,
        &redoMidiCommand,
        &loopMidiCommand,
        &loopContinuousLayeringMidiCommand,
        &fillMidiCommand,
    },
};

// MIDI device submenu - for after we've selected which device we want it for

midi::DefaultVelocityToLevel defaultVelocityToLevelMenu{STRING_FOR_VELOCITY};
midi::Device midiDeviceMenu{
    EMPTY_STRING,
    {
        &mpe::directionSelectorMenu,
        &defaultVelocityToLevelMenu,
    },
};

// MIDI input differentiation menu
midi::InputDifferentiation midiInputDifferentiationMenu{STRING_FOR_DIFFERENTIATE_INPUTS};

// MIDI clock menu
midi::ClockOutStatus midiClockOutStatusMenu{STRING_FOR_OUTPUT, STRING_FOR_MIDI_CLOCK_OUT};
midi::ClockInStatus midiClockInStatusMenu{STRING_FOR_INPUT, STRING_FOR_MIDI_CLOCK_IN};
tempo::MagnitudeMatching tempoMagnitudeMatchingMenu{STRING_FOR_TEMPO_MAGNITUDE_MATCHING,
                                                    STRING_FOR_TEMPO_M_MATCH_MENU_TITLE};

//Midi devices menu
midi::Devices midi::devicesMenu{STRING_FOR_DEVICES, STRING_FOR_MIDI_DEVICES};
mpe::DirectionSelector mpe::directionSelectorMenu{STRING_FOR_MPE};

//MIDI menu
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
        &midiThruMenu,
        &midiTakeoverMenu,
        &midiCommandsMenu,
        &midiInputDifferentiationMenu,
        &midi::devicesMenu,
    },
};

// Clock menu
// Trigger clock in menu
trigger::in::PPQN triggerInPPQNMenu{STRING_FOR_PPQN, STRING_FOR_INPUT_PPQN};
trigger::in::AutoStart triggerInAutoStartMenu{STRING_FOR_AUTO_START};
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

defaults::DelayPingPong defaultDelayPingPongMenu{STRING_FOR_PINGPONG, STRING_FOR_DELAY_PINGPONG};
defaults::DelayAnalog defaultDelayAnalogMenu{STRING_FOR_TYPE, STRING_FOR_DELAY_TYPE};
defaults::DelaySync defaultDelaySyncMenu{STRING_FOR_SYNC, STRING_FOR_DELAY_SYNC};

Submenu defaultFXDelay{
    STRING_FOR_DELAY,
    STRING_FOR_DELAY,
    {&defaultDelayPingPongMenu, &defaultDelayAnalogMenu, &defaultDelaySyncMenu},
};

Submenu defaultFX{
    STRING_FOR_FX,
    STRING_FOR_FX,
    {&defaultFXDelay},
};

IntegerRange defaultTempoMenu{STRING_FOR_TEMPO, STRING_FOR_DEFAULT_TEMPO, 60, 240};
IntegerRange defaultSwingMenu{STRING_FOR_SWING, STRING_FOR_DEFAULT_SWING, 1, 99};
KeyRange defaultKeyMenu{STRING_FOR_KEY, STRING_FOR_DEFAULT_KEY};
defaults::Scale defaultScaleMenu{STRING_FOR_SCALE, STRING_FOR_DEFAULT_SCALE};
defaults::Velocity defaultVelocityMenu{STRING_FOR_VELOCITY, STRING_FOR_DEFAULT_VELOC_MENU_TITLE};
defaults::Magnitude defaultMagnitudeMenu{STRING_FOR_RESOLUTION, STRING_FOR_DEFAULT_RESOL_MENU_TITLE};
defaults::BendRange defaultBendRangeMenu{STRING_FOR_BEND_RANGE, STRING_FOR_DEFAULT_BEND_R};

Submenu defaultsSubmenu{
    STRING_FOR_DEFAULTS,
    {
        &defaultFX,
        &defaultTempoMenu,
        &defaultSwingMenu,
        &defaultKeyMenu,
        &defaultScaleMenu,
        &defaultVelocityMenu,
        &defaultMagnitudeMenu,
        &defaultBendRangeMenu,
    },
};

// Sound editor menu -----------------------------------------------------------------------------

// FM only
MenuItem* modulatorMenuItems[] = {
    &modulatorVolume, &modulatorTransposeMenu, &modulatorFeedbackMenu, &modulatorDestMenu, &modulatorPhaseMenu,
};

submenu::Modulator modulator0Menu{STRING_FOR_FM_MODULATOR_1, modulatorMenuItems, 0};
submenu::Modulator modulator1Menu{STRING_FOR_FM_MODULATOR_2, modulatorMenuItems, 1};

// Not FM
patched_param::IntegerNonFM noiseMenu{STRING_FOR_NOISE_LEVEL, ::Param::Local::NOISE_VOLUME};

MasterTranspose masterTransposeMenu{STRING_FOR_MASTER_TRANSPOSE, STRING_FOR_MASTER_TRAN_MENU_TITLE};

patch_cable_strength::Fixed vibratoMenu{STRING_FOR_VIBRATO, ::Param::Local::PITCH_ADJUST, PatchSource::LFO_GLOBAL};

// Drum only
menu_item::DrumName drumNameMenu{STRING_FOR_NAME};

// Synth only
menu_item::SynthMode synthModeMenu{STRING_FOR_SYNTH_MODE};

bend_range::PerFinger drumBendRangeMenu{STRING_FOR_BEND_RANGE}; // The single option available for Drums
patched_param::Integer volumeMenu{STRING_FOR_VOLUME_LEVEL, STRING_FOR_MASTER_LEVEL, ::Param::Global::VOLUME_POST_FX};
patched_param::Pan panMenu{STRING_FOR_PAN, ::Param::Local::PAN};

PatchCables patchCablesMenu{STRING_FOR_MOD_MATRIX};

menu_item::Submenu soundEditorRootMenu{
    STRING_FOR_SOUND,
    {
        &source0Menu,
        &source1Menu,
        &modulator0Menu,
        &modulator1Menu,
        &noiseMenu,
        &masterTransposeMenu,
        &vibratoMenu,
        &lpfMenu,
        &hpfMenu,
        &filterRoutingMenu,
        &drumNameMenu,
        &synthModeMenu,
        &env0Menu,
        &env1Menu,
        &lfo0Menu,
        &lfo1Menu,
        &voiceMenu,
        &fxMenu,
        &compressorMenu,
        &bendMenu,
        &drumBendRangeMenu,
        &volumeMenu,
        &panMenu,
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
        &sequenceDirectionMenu,
    },
};

// Root menu for AudioClips
menu_item::Submenu soundEditorRootMenuAudioClip{
    STRING_FOR_AUDIO_CLIP,
    {
        &audioClipSampleMenu,
        &audioClipTransposeMenu,
        &audioClipLPFMenu,
        &audioClipHPFMenu,
        &filterRoutingMenu,
        &audioClipAttackMenu,
        &priorityMenu,
        &audioClipFXMenu,
        &audioClipCompressorMenu,
        &audioClipLevelMenu,
        &audioClipPanMenu,
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
        &swingIntervalMenu,
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
    {&sampleRepeatMenu,       &sampleReverseMenu,      &timeStretchMenu,               &samplePitchSpeedMenu,          &audioRecorderMenu,   &fileSelectorMenu,      &interpolationMenu,       &sampleStartMenu      },
    {&sampleRepeatMenu,       &sampleReverseMenu,      &timeStretchMenu,               &samplePitchSpeedMenu,          &audioRecorderMenu,   &fileSelectorMenu,      &interpolationMenu,       &sampleStartMenu      },
    {&sourceVolumeMenu,       &sourceTransposeMenu,    &oscTypeMenu,                   &pulseWidthMenu,                &oscPhaseMenu,        &sourceFeedbackMenu,    &sourceWaveIndexMenu,     &noiseMenu            },
    {&sourceVolumeMenu,       &sourceTransposeMenu,    &oscTypeMenu,                   &pulseWidthMenu,                &oscPhaseMenu,        &sourceFeedbackMenu,    &sourceWaveIndexMenu,     &oscSyncMenu          },
    {&modulatorVolume,        &modulatorTransposeMenu, comingSoonMenu,                 comingSoonMenu,                 &modulatorPhaseMenu,  &modulatorFeedbackMenu, comingSoonMenu,           &sequenceDirectionMenu},
    {&modulatorVolume,        &modulatorTransposeMenu, comingSoonMenu,                 comingSoonMenu,                 &modulatorPhaseMenu,  &modulatorFeedbackMenu, &modulatorDestMenu,       NULL                  },
    {&volumeMenu,             &masterTransposeMenu,    &vibratoMenu,                   &panMenu,                       &synthModeMenu,       &srrMenu,               &bitcrushMenu,            &clippingMenu         },
    {&portaMenu,              &polyphonyMenu,          &priorityMenu,                  &unisonDetuneMenu,              &numUnisonMenu,       nullptr,                nullptr,                  &foldMenu             },
    {&envReleaseMenu,         &envSustainMenu,         &envDecayMenu,                  &envAttackMenu,                 &lpfMorphMenu,        &lpfModeMenu,           &lpfResMenu,              &lpfFreqMenu          },
    {&envReleaseMenu,         &envSustainMenu,         &envDecayMenu,                  &envAttackMenu,                 &hpfMorphMenu,        &hpfModemenu,           &hpfResMenu,              &hpfFreqMenu          },
    {&compressorReleaseMenu,  &sidechainSyncMenu,      &compressorVolumeShortcutMenu,  &compressorAttackMenu,          &compressorShapeMenu, &sidechainSendMenu,     &bassMenu,                &bassFreqMenu         },
    {&arpRateMenu,            &arpSyncMenu,            &arpGateMenu,                   &arpOctavesMenu,                &arpModeMenu,         &drumNameMenu,          &trebleMenu,              &trebleFreqMenu       },
    {&lfo1RateMenu,           &lfo1SyncMenu,           &lfo1TypeMenu,                  &modFXTypeMenu,                 &modFXOffsetMenu,     &modFXFeedbackMenu,     &modFXDepthMenu,          &modFXRateMenu        },
    {&lfo2RateMenu,           comingSoonMenu,          &lfo2TypeMenu,                  &reverbAmountMenu,              &reverbPanMenu,       &reverbWidthMenu,       &reverbDampeningMenu,     &reverbRoomSizeMenu   },
    {&delayRateMenu,          &delaySyncMenu,          &delayAnalogMenu,               &delayFeedbackMenu,             &delayPingPongMenu,   nullptr,                nullptr,                  NULL                  },
};

MenuItem* paramShortcutsForAudioClips[][8] = {
    {nullptr,                 &audioClipReverseMenu,   nullptr,                        &samplePitchSpeedMenu,          nullptr,              &fileSelectorMenu,      &interpolationMenu,       &audioClipSampleMarkerEditorMenuEnd},
    {nullptr,                 &audioClipReverseMenu,   nullptr,                        &samplePitchSpeedMenu,          nullptr,              &fileSelectorMenu,      &interpolationMenu,       &audioClipSampleMarkerEditorMenuEnd},
    {&audioClipLevelMenu,     &audioClipTransposeMenu, nullptr,                        nullptr,                        nullptr,              nullptr,                nullptr,                  NULL                               },
    {&audioClipLevelMenu,     &audioClipTransposeMenu, nullptr,                        nullptr,                        nullptr,              nullptr,                nullptr,                  NULL                               },
    {nullptr,                 nullptr,                 nullptr,                        nullptr,                        nullptr,              nullptr,                nullptr,                  NULL                               },
    {nullptr,                 nullptr,                 nullptr,                        nullptr,                        nullptr,              nullptr,                nullptr,                  NULL                               },
    {&audioClipLevelMenu,     &audioClipTransposeMenu, nullptr,                        &audioClipPanMenu,              nullptr,              &srrMenu,               &bitcrushMenu,            &clippingMenu                      },
    {nullptr,                 nullptr,                 &priorityMenu,                  nullptr,                        nullptr,              nullptr,                nullptr,                  comingSoonMenu                     },
    {nullptr,                 nullptr,                 nullptr,                        &audioClipAttackMenu,           comingSoonMenu,       &lpfModeMenu,           &audioClipLPFResMenu,     &audioClipLPFFreqMenu              },
    {nullptr,                 nullptr,                 nullptr,                        &audioClipAttackMenu,           comingSoonMenu,       &hpfModemenu,           &audioClipHPFResMenu,     &audioClipHPFFreqMenu              },
    {&compressorReleaseMenu,  &sidechainSyncMenu,      &audioClipCompressorVolumeMenu, &compressorAttackMenu,          &compressorShapeMenu, nullptr,                &bassMenu,                &bassFreqMenu                      },
    {nullptr,                 nullptr,                 nullptr,                        nullptr,                        nullptr,              nullptr,                &trebleMenu,              &trebleFreqMenu                    },
    {nullptr,                 nullptr,                 nullptr,                        &audioClipModFXTypeMenu,        &modFXOffsetMenu,     &modFXFeedbackMenu,     &audioClipModFXDepthMenu, &audioClipModFXRateMenu            },
    {nullptr,                 nullptr,                 nullptr,                        &audioClipReverbSendAmountMenu, &reverbPanMenu,       &reverbWidthMenu,       &reverbDampeningMenu,     &reverbRoomSizeMenu                },
    {&audioClipDelayRateMenu, &delaySyncMenu,          &delayAnalogMenu,               &audioClipDelayFeedbackMenu,    &delayPingPongMenu,   nullptr,                nullptr,                  NULL                               },
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
