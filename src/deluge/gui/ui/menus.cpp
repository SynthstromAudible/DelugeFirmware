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
#include "gui/menu_item/cv/transpose.h"
#include "gui/menu_item/cv/volts.h"
#include "gui/menu_item/decimal.h"
#include "gui/menu_item/defaults/bend_range.h"
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
#include "gui/menu_item/filter/lpf_freq.h"
#include "gui/menu_item/filter/lpf_mode.h"
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
using namespace deluge::gui;
using namespace deluge::gui::menu_item;

// Dev vars
dev_var::AMenu devVarAMenu;
dev_var::BMenu devVarBMenu;
dev_var::CMenu devVarCMenu;
dev_var::DMenu devVarDMenu;
dev_var::EMenu devVarEMenu;
dev_var::FMenu devVarFMenu;
dev_var::GMenu devVarGMenu;

// LPF menu ----------------------------------------------------------------------------------------------------

filter::LPFFreq lpfFreqMenu{"Frequency", "LPF frequency", ::Param::Local::LPF_FREQ};
patched_param::IntegerNonFM lpfResMenu{"Resonance", "LPF resonance", ::Param::Local::LPF_RESONANCE};
filter::LPFMode lpfModeMenu{"MODE", "LPF mode"};

submenu::Filter lpfMenu{
    "LPF",
    {
        &lpfFreqMenu,
        &lpfResMenu,
        &lpfModeMenu,
    },
};

// HPF menu ----------------------------------------------------------------------------------------------------

filter::HPFFreq hpfFreqMenu{"Frequency", "HPF frequency", ::Param::Local::HPF_FREQ};
patched_param::IntegerNonFM hpfResMenu{"Resonance", "HPF resonance", ::Param::Local::HPF_RESONANCE};

submenu::Filter hpfMenu{
    "HPF",
    {
        &hpfFreqMenu,
        &hpfResMenu,
    },
};

// Envelope menu ----------------------------------------------------------------------------------------------------

envelope::Segment envAttackMenu{"ATTACK", "Env{} attack", ::Param::Local::ENV_0_ATTACK};
envelope::Segment envDecayMenu{"DECAY", "Env{} decay", ::Param::Local::ENV_0_DECAY};
envelope::Segment envSustainMenu{"SUSTAIN", "Env{} sustain", ::Param::Local::ENV_0_SUSTAIN};
envelope::Segment envReleaseMenu{"RELEASE", "Env{} release", ::Param::Local::ENV_0_RELEASE};

MenuItem* envMenuItems[] = {
    &envAttackMenu,
    &envDecayMenu,
    &envSustainMenu,
    &envReleaseMenu,
};
submenu::Envelope env0Menu{HAVE_OLED ? "Envelope 1" : "ENV1", envMenuItems, 0};
submenu::Envelope env1Menu{HAVE_OLED ? "Envelope 2" : "ENV2", envMenuItems, 1};

// Osc menu -------------------------------------------------------------------------------------------------------

osc::Type oscTypeMenu{"TYPE", "Osc{} type"};
osc::source::WaveIndex sourceWaveIndexMenu{"Wave-index", "Osc{} wave-ind.", Param::Local::OSC_A_WAVE_INDEX};
osc::source::Volume sourceVolumeMenu{HAVE_OLED ? "Level" : "VOLUME", "Osc{} level", Param::Local::OSC_A_VOLUME};
osc::source::Feedback sourceFeedbackMenu{"FEEDBACK", "Carrier{} feed.", Param::Local::CARRIER_0_FEEDBACK};
osc::AudioRecorder audioRecorderMenu{"Record audio"};
sample::Reverse sampleReverseMenu{"REVERSE", "Samp{} reverse"};
sample::Repeat sampleRepeatMenu{HAVE_OLED ? "Repeat mode" : "MODE", "Samp{} repeat"};
sample::Start sampleStartMenu{"Start-point"};
sample::End sampleEndMenu{"End-point"};
sample::Transpose sourceTransposeMenu{"TRANSPOSE", "Osc{} transpose", Param::Local::OSC_A_PITCH_ADJUST};
sample::PitchSpeed samplePitchSpeedMenu{HAVE_OLED ? "Pitch/speed" : "PISP"};
sample::TimeStretch timeStretchMenu{"SPEED", "Samp{} speed"};
sample::Interpolation interpolationMenu{"INTERPOLATION", "Samp{} interp."};
osc::PulseWidth pulseWidthMenu{"PULSE WIDTH", "Osc{} p. width", Param::Local::OSC_A_PHASE_WIDTH};
osc::Sync oscSyncMenu{HAVE_OLED ? "Oscillator sync" : "SYNC"};
osc::RetriggerPhase oscPhaseMenu{"Retrigger phase", "Osc{} r. phase", false};

MenuItem* oscMenuItems[] = {
    &oscTypeMenu,         &sourceVolumeMenu,     &sourceWaveIndexMenu, &sourceFeedbackMenu, &fileSelectorMenu,
    &audioRecorderMenu,   &sampleReverseMenu,    &sampleRepeatMenu,    &sampleStartMenu,    &sampleEndMenu,
    &sourceTransposeMenu, &samplePitchSpeedMenu, &timeStretchMenu,     &interpolationMenu,  &pulseWidthMenu,
    &oscSyncMenu,         &oscPhaseMenu,
};

submenu::ActualSource source0Menu{HAVE_OLED ? "Oscillator 1" : "OSC1", oscMenuItems, 0};
submenu::ActualSource source1Menu{HAVE_OLED ? "Oscillator 2" : "OSC2", oscMenuItems, 1};

// Unison --------------------------------------------------------------------------------------

unison::Count numUnisonMenu{HAVE_OLED ? "Unison number" : "NUM"};
unison::Detune unisonDetuneMenu{HAVE_OLED ? "Unison detune" : "DETUNE"};
unison::StereoSpread unison::stereoSpreadMenu{HAVE_OLED ? "Unison stereo spread" : "SPREAD"};

Submenu unisonMenu{
    "UNISON",
    {
        &numUnisonMenu,
        &unisonDetuneMenu,
        &unison::stereoSpreadMenu,
    },
};

// Arp --------------------------------------------------------------------------------------
arpeggiator::Mode arpModeMenu{"MODE", "Arp. mode"};
arpeggiator::Sync arpSyncMenu{"SYNC", "Arp. sync"};
arpeggiator::Octaves arpOctavesMenu{HAVE_OLED ? "Number of octaves" : "OCTAVES", "Arp. octaves"};
arpeggiator::Gate arpGateMenu{"GATE", "Arp. gate", ::Param::Unpatched::Sound::ARP_GATE};
arpeggiator::midi_cv::Gate arpGateMenuMIDIOrCV{"GATE", "Arp. gate"};
arpeggiator::Rate arpRateMenu{"RATE", "Arp. rate", ::Param::Global::ARP_RATE};
arpeggiator::midi_cv::Rate arpRateMenuMIDIOrCV{"RATE", "Arp. rate"};

submenu::Arpeggiator arpMenu{
    "ARPEGGIATOR",
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

voice::Polyphony polyphonyMenu{"POLYPHONY"};
UnpatchedParam portaMenu{"PORTAMENTO", ::Param::Unpatched::Sound::PORTAMENTO};
voice::Priority priorityMenu{"PRIORITY"};

Submenu voiceMenu{"VOICE", {&polyphonyMenu, &unisonMenu, &portaMenu, &arpMenu, &priorityMenu}};

// Modulator menu -----------------------------------------------------------------------

modulator::Transpose modulatorTransposeMenu{"Transpose", "FM Mod{} tran.", ::Param::Local::MODULATOR_0_PITCH_ADJUST};
source::patched_param::FM modulatorVolume{HAVE_OLED ? "Level" : "AMOUNT", "FM Mod{} level",
                                          ::Param::Local::MODULATOR_0_VOLUME};
source::patched_param::FM modulatorFeedbackMenu{"FEEDBACK", "FM Mod{} f.back", ::Param::Local::MODULATOR_0_FEEDBACK};
modulator::Destination modulatorDestMenu{"Destination", "FM Mod2 dest."};
osc::RetriggerPhase modulatorPhaseMenu{"Retrigger phase", "FM Mod{} retrig", true};

// LFO1 menu ---------------------------------------------------------------------------------

lfo::global::Type lfo1TypeMenu{HAVE_OLED ? "SHAPE" : "TYPE", "LFO1 type"};
lfo::global::Rate lfo1RateMenu{"RATE", "LFO1 rate", ::Param::Global::LFO_FREQ};
lfo::global::Sync lfo1SyncMenu{"SYNC", "LFO1 sync"};

Submenu lfo0Menu{"LFO1", {&lfo1TypeMenu, &lfo1RateMenu, &lfo1SyncMenu}};

// LFO2 menu ---------------------------------------------------------------------------------
lfo::local::Type lfo2TypeMenu{HAVE_OLED ? "SHAPE" : "TYPE", "LFO2 type"};
patched_param::Integer lfo2RateMenu{"RATE", "LFO2 rate", ::Param::Local::LFO_LOCAL_FREQ};

Submenu lfo1Menu{"LFO2", {&lfo2TypeMenu, &lfo2RateMenu}};

// Mod FX ----------------------------------------------------------------------------------
mod_fx::Type modFXTypeMenu{"TYPE", "MODFX type"};
patched_param::Integer modFXRateMenu{"RATE", "MODFX rate", ::Param::Global::MOD_FX_RATE};
mod_fx::Feedback modFXFeedbackMenu{"FEEDBACK", "MODFX feedback", ::Param::Unpatched::MOD_FX_FEEDBACK};
mod_fx::Depth modFXDepthMenu{"DEPTH", "MODFX depth", ::Param::Global::MOD_FX_DEPTH};
mod_fx::Offset modFXOffsetMenu{"OFFSET", "MODFX offset", ::Param::Unpatched::MOD_FX_OFFSET};

Submenu modFXMenu{
    HAVE_OLED ? "Mod-fx" : "MODU",
    {
        &modFXTypeMenu,
        &modFXRateMenu,
        &modFXFeedbackMenu,
        &modFXDepthMenu,
        &modFXOffsetMenu,
    },
};

// EQ -------------------------------------------------------------------------------------
UnpatchedParam bassMenu{"BASS", ::Param::Unpatched::BASS};
UnpatchedParam trebleMenu{"TREBLE", ::Param::Unpatched::TREBLE};
UnpatchedParam bassFreqMenu{HAVE_OLED ? "Bass frequency" : "BAFR", ::Param::Unpatched::BASS_FREQ};
UnpatchedParam trebleFreqMenu{HAVE_OLED ? "Treble frequency" : "TRFR", ::Param::Unpatched::TREBLE_FREQ};

Submenu eqMenu{
    "EQ",
    {
        &bassMenu,
        &trebleMenu,
        &bassFreqMenu,
        &trebleFreqMenu,
    },
};

// Delay ---------------------------------------------------------------------------------
patched_param::Integer delayFeedbackMenu{"AMOUNT", "Delay amount", ::Param::Global::DELAY_FEEDBACK};
patched_param::Integer delayRateMenu{"RATE", "Delay rate", ::Param::Global::DELAY_RATE};
delay::PingPong delayPingPongMenu{"Pingpong", "Delay pingpong"};
delay::Analog delayAnalogMenu{"TYPE", "Delay type"};
delay::Sync delaySyncMenu{"SYNC", "Delay sync"};

Submenu delayMenu{
    "DELAY",
    {
        &delayFeedbackMenu,
        &delayRateMenu,
        &delayPingPongMenu,
        &delayAnalogMenu,
        &delaySyncMenu,
    },
};

// Bend Ranges -------------------------------------------------------------------------------

bend_range::Main mainBendRangeMenu{"Normal"};
bend_range::PerFinger perFingerBendRangeMenu{HAVE_OLED ? "Poly / finger / MPE" : "MPE"};

submenu::Bend bendMenu{
    "Bend range",
    {
        &mainBendRangeMenu,
        &perFingerBendRangeMenu,
    },
};

// Sidechain/Compressor-----------------------------------------------------------------------

sidechain::Send sidechainSendMenu{"Send to sidechain", "Send to sidech"};
compressor::VolumeShortcut compressorVolumeShortcutMenu{"Volume ducking", ::Param::Global::VOLUME_POST_REVERB_SEND,
                                                        PatchSource::COMPRESSOR};
reverb::compressor::Volume reverbCompressorVolumeMenu{"Volume ducking"};
sidechain::Sync sidechainSyncMenu{"SYNC", "Sidechain sync"};
compressor::Attack compressorAttackMenu{"ATTACK", "Sidech. attack"};
compressor::Release compressorReleaseMenu{"RELEASE", "Sidech release"};
unpatched_param::UpdatingReverbParams compressorShapeMenu{"SHAPE", "Sidech. shape",
                                                          ::Param::Unpatched::COMPRESSOR_SHAPE};
reverb::compressor::Shape reverbCompressorShapeMenu{"SHAPE", "Sidech. shape"};

submenu::Compressor compressorMenu{
    "Sidechain compressor",
    "Sidechain comp",
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
    HAVE_OLED ? "Reverb sidechain" : "SIDE",
    "Reverb sidech.",
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
patched_param::Integer reverbAmountMenu{"AMOUNT", "Reverb amount", ::Param::Global::REVERB_AMOUNT};
reverb::RoomSize reverbRoomSizeMenu{HAVE_OLED ? "Room size" : "SIZE"};
reverb::Dampening reverbDampeningMenu{"DAMPENING"};
reverb::Width reverbWidthMenu{"WIDTH", "Reverb width"};
reverb::Pan reverbPanMenu{"PAN", "Reverb pan"};

Submenu reverbMenu{
    "REVERB",
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

fx::Clipping clippingMenu{"SATURATION"};
UnpatchedParam srrMenu{"DECIMATION", ::Param::Unpatched::SAMPLE_RATE_REDUCTION};
UnpatchedParam bitcrushMenu{HAVE_OLED ? "Bitcrush" : "CRUSH", ::Param::Unpatched::BITCRUSHING};

Submenu fxMenu{
    "FX",
    {
        &modFXMenu,
        &eqMenu,
        &delayMenu,
        &reverbMenu,
        &clippingMenu,
        &srrMenu,
        &bitcrushMenu,
    },
};

// MIDIInstrument menu ----------------------------------------------------------------------

midi::Bank midiBankMenu{"BANK", "MIDI bank"};
midi::Sub midiSubMenu{HAVE_OLED ? "Sub-bank" : "SUB", "MIDI sub-bank"};
midi::PGM midiPGMMenu{"PGM", "MIDI PGM numb."};

// Clip-level stuff --------------------------------------------------------------------------

sequence::Direction sequenceDirectionMenu{HAVE_OLED ? "Play direction" : "DIRECTION"};

// AudioClip stuff ---------------------------------------------------------------------------

// Sample Menu
audio_clip::Reverse audioClipReverseMenu{"REVERSE"};
audio_clip::SampleMarkerEditor audioClipSampleMarkerEditorMenuStart{"", MarkerType::START};
audio_clip::SampleMarkerEditor audioClipSampleMarkerEditorMenuEnd{"WAVEFORM", MarkerType::END};

Submenu audioClipSampleMenu{
    "SAMPLE",
    {
        &fileSelectorMenu,
        &audioClipReverseMenu,
        &samplePitchSpeedMenu,
        &audioClipSampleMarkerEditorMenuEnd,
        &interpolationMenu,
    },
};

// LPF Menu
audio_clip::LPFFreq audioClipLPFFreqMenu{"Frequency", "LPF frequency", ::Param::Unpatched::GlobalEffectable::LPF_FREQ};
UnpatchedParam audioClipLPFResMenu{"Resonance", "LPF resonance", ::Param::Unpatched::GlobalEffectable::LPF_RES};

Submenu audioClipLPFMenu{
    "LPF",
    {
        &audioClipLPFFreqMenu,
        &audioClipLPFResMenu,
        &lpfModeMenu,
    },
};

// HPF Menu
audio_clip::HPFFreq audioClipHPFFreqMenu{"Frequency", "HPF frequency", ::Param::Unpatched::GlobalEffectable::HPF_FREQ};
UnpatchedParam audioClipHPFResMenu{"Resonance", "HPF resonance", ::Param::Unpatched::GlobalEffectable::HPF_RES};

Submenu audioClipHPFMenu{
    "HPF",
    {
        &audioClipHPFFreqMenu,
        &audioClipHPFResMenu,
    },
};

// Mod FX Menu
audio_clip::mod_fx::Type audioClipModFXTypeMenu{"TYPE", "MOD FX type"};
UnpatchedParam audioClipModFXRateMenu{"RATE", "MOD FX rate", ::Param::Unpatched::GlobalEffectable::MOD_FX_RATE};
UnpatchedParam audioClipModFXDepthMenu{"DEPTH", "MOD FX depth", ::Param::Unpatched::GlobalEffectable::MOD_FX_DEPTH};

Submenu audioClipModFXMenu{
    HAVE_OLED ? "Mod-fx" : "MODU",
    {
        &audioClipModFXTypeMenu,
        &audioClipModFXRateMenu,
        &modFXFeedbackMenu,
        &audioClipModFXDepthMenu,
        &modFXOffsetMenu,
    },
};

// Delay Menu
UnpatchedParam audioClipDelayRateMenu{"AMOUNT", "Delay amount", ::Param::Unpatched::GlobalEffectable::DELAY_AMOUNT};
UnpatchedParam audioClipDelayFeedbackMenu{"RATE", "Delay rate", ::Param::Unpatched::GlobalEffectable::DELAY_RATE};

Submenu audioClipDelayMenu{
    "DELAY",
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
    "AMOUNT",
    "Reverb amount",
    ::Param::Unpatched::GlobalEffectable::REVERB_SEND_AMOUNT,
};
Submenu audioClipReverbMenu{
    "REVERB",
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
    "Volume ducking", ::Param::Unpatched::GlobalEffectable::SIDECHAIN_VOLUME};

Submenu audioClipCompressorMenu{
    "Sidechain compressor",
    {
        &audioClipCompressorVolumeMenu,
        &sidechainSyncMenu,
        &compressorAttackMenu,
        &compressorReleaseMenu,
        &compressorShapeMenu,
    },
};

audio_clip::Transpose audioClipTransposeMenu{"TRANSPOSE"};
audio_clip::Attack audioClipAttackMenu{"ATTACK"};

Submenu audioClipFXMenu{
    "FX",
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

UnpatchedParam audioClipLevelMenu{HAVE_OLED ? "Level" : "VOLUME", ::Param::Unpatched::GlobalEffectable::VOLUME};
unpatched_param::Pan audioClipPanMenu{"PAN", ::Param::Unpatched::GlobalEffectable::PAN};

const MenuItem* midiOrCVParamShortcuts[8] = {
    &arpRateMenuMIDIOrCV, &arpSyncMenu, &arpGateMenuMIDIOrCV, &arpOctavesMenu, &arpModeMenu, nullptr, nullptr, nullptr,
};

// Gate stuff
gate::Mode gateModeMenu;
gate::OffTime gateOffTimeMenu{HAVE_OLED ? "Min. off-time" : ""};

// Root menu

// CV Menu
cv::Volts cvVoltsMenu{"Volts per octave", "CV{} V/octave"};
cv::Transpose cvTransposeMenu{"TRANSPOSE", "CV{} transpose"};

Submenu<2> cvSubmenu{"", {&cvVoltsMenu, &cvTransposeMenu}};

cv::Selection cvSelectionMenu{"CV", "CV outputs"};
gate::Selection gateSelectionMenu{"GATE", "Gate outputs"};

swing::Interval swingIntervalMenu{HAVE_OLED ? "Swing interval" : "SWIN"};

// Pads menu
shortcuts::Version shortcutsVersionMenu{HAVE_OLED ? "Shortcuts version" : "SHOR", "Shortcuts ver."};
menu_item::keyboard::Layout keyboardLayoutMenu{HAVE_OLED ? "Keyboard for text" : "KEYB", "Key layout"};

// Colours submenu
Submenu coloursSubmenu{
    "COLOURS",
    {
        &activeColourMenu,
        &stoppedColourMenu,
        &mutedColourMenu,
        &soloColourMenu,
    },
};

Submenu padsSubmenu{
    "PADS",
    {
        &shortcutsVersionMenu,
        &keyboardLayoutMenu,
        &coloursSubmenu,
    },
};

// Record submenu
record::Quantize recordQuantizeMenu{"Quantization"};
record::Margins recordMarginsMenu{HAVE_OLED ? "Loop margins" : "MARGINS"};
record::CountIn recordCountInMenu{"Count-in", "Rec count-in"};
monitor::Mode monitorModeMenu{HAVE_OLED ? "Sampling monitoring" : "MONITORING", "Monitoring"};

Submenu recordSubmenu{
    "Recording",
    {
        &recordCountInMenu,
        &recordQuantizeMenu,
        &recordMarginsMenu,
        &monitorModeMenu,
    },
};

sample::browser_preview::Mode sampleBrowserPreviewModeMenu{HAVE_OLED ? "Sample preview" : "PREV"};

flash::Status flashStatusMenu{HAVE_OLED ? "Play-cursor" : "CURS"};

#ifdef DBT_FW_VERSION_STRING
char const* firmwareString = DBT_FW_VERSION_STRING;
#else
char const* firmwareString = "4.1.4-alpha3-c";
#endif // ifdef DBT_FW_VERSION_STRING

firmware::Version firmwareVersionMenu{"Firmware version", "Firmware ver."};

runtime_feature::Settings runtimeFeatureSettingsMenu{HAVE_OLED ? "Community fts." : "FEAT", "Community fts."};

// CV menu

// MIDI
// MIDI thru
midi::Thru midiThruMenu{HAVE_OLED ? "MIDI-thru" : "THRU"};

// MIDI Takeover
midi::Takeover midiTakeoverMenu{HAVE_OLED ? "TAKEOVER" : "TOVR"};

// MIDI commands submenu
midi::Command playbackRestartMidiCommand{"Restart", GlobalMIDICommand::PLAYBACK_RESTART};
midi::Command playMidiCommand{"PLAY", GlobalMIDICommand::PLAY};
midi::Command recordMidiCommand{HAVE_OLED ? "Record" : "REC", GlobalMIDICommand::RECORD};
midi::Command tapMidiCommand{"Tap tempo", GlobalMIDICommand::TAP};
midi::Command undoMidiCommand{"UNDO", GlobalMIDICommand::UNDO};
midi::Command redoMidiCommand{"REDO", GlobalMIDICommand::REDO};
midi::Command loopMidiCommand{"LOOP", GlobalMIDICommand::LOOP};
midi::Command loopContinuousLayeringMidiCommand{"LAYERING loop", GlobalMIDICommand::LOOP_CONTINUOUS_LAYERING};

Submenu midiCommandsMenu{
    HAVE_OLED ? "Commands" : "CMD",
    "MIDI commands",
    {
        &playMidiCommand,
        &playbackRestartMidiCommand,
        &recordMidiCommand,
        &tapMidiCommand,
        &undoMidiCommand,
        &redoMidiCommand,
        &loopMidiCommand,
        &loopContinuousLayeringMidiCommand,
    },
};

// MIDI device submenu - for after we've selected which device we want it for

midi::DefaultVelocityToLevel defaultVelocityToLevelMenu{"VELOCITY"};
Submenu midiDeviceMenu{
    "",
    {
        &mpe::directionSelectorMenu,
        &defaultVelocityToLevelMenu,
    },
};

// MIDI input differentiation menu
midi::InputDifferentiation midiInputDifferentiationMenu{"Differentiate inputs"};

// MIDI clock menu
midi::ClockOutStatus midiClockOutStatusMenu{HAVE_OLED ? "Output" : "OUT", "MIDI clock out"};
midi::ClockInStatus midiClockInStatusMenu{HAVE_OLED ? "Input" : "IN", "MIDI clock in"};
tempo::MagnitudeMatching tempoMagnitudeMatchingMenu{HAVE_OLED ? "Tempo magnitude matching" : "MAGN", "Tempo m. match"};

//Midi devices menu
midi::Devices midi::devicesMenu{"Devices", "MIDI devices"};
mpe::DirectionSelector mpe::directionSelectorMenu{"MPE"};

//MIDI menu
Submenu midiClockMenu{
    "CLOCK",
    "MIDI clock",
    {
        &midiClockInStatusMenu,
        &midiClockOutStatusMenu,
        &tempoMagnitudeMatchingMenu,
    },
};
Submenu midiMenu{
    "MIDI",
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
trigger::in::PPQN triggerInPPQNMenu{"PPQN", "Input PPQN"};
trigger::in::AutoStart triggerInAutoStartMenu{"Auto-start"};
Submenu triggerClockInMenu{
    HAVE_OLED ? "Input" : "IN",
    "T. clock input",
    {
        &triggerInPPQNMenu,
        &triggerInAutoStartMenu,
    },
};

// Trigger clock out menu
trigger::out::PPQN triggerOutPPQNMenu{"PPQN", "Output PPQN"};
Submenu triggerClockOutMenu{
    HAVE_OLED ? "Output" : "OUT",
    "T. clock out",
    {
        &triggerOutPPQNMenu,
    },
};

// Trigger clock menu
Submenu triggerClockMenu{
    HAVE_OLED ? "Trigger clock" : "TCLOCK",
    {
        &triggerClockInMenu,
        &triggerClockOutMenu,
    },
};

// Defaults menu
IntegerRange defaultTempoMenu{"TEMPO", "Default tempo", 60, 240};
IntegerRange defaultSwingMenu{"SWING", "Default swing", 1, 99};
KeyRange defaultKeyMenu{"KEY", "Default key"};
defaults::Scale defaultScaleMenu{"SCALE", "Default scale"};
defaults::Velocity defaultVelocityMenu{"VELOCITY", "Default veloc."};
defaults::Magnitude defaultMagnitudeMenu{"RESOLUTION", "Default resol."};
defaults::BendRange defaultBendRangeMenu{"Bend range", "Default bend r"};

Submenu defaultsSubmenu{
    "DEFAULTS",
    {
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

submenu::Modulator modulator0Menu{HAVE_OLED ? "FM modulator 1" : "MOD1", modulatorMenuItems, 0};
submenu::Modulator modulator1Menu{HAVE_OLED ? "FM modulator 2" : "MOD2", modulatorMenuItems, 1};

// Not FM
patched_param::IntegerNonFM noiseMenu{HAVE_OLED ? "Noise level" : "NOISE", ::Param::Local::NOISE_VOLUME};

MasterTranspose masterTransposeMenu{HAVE_OLED ? "Master transpose" : "TRANSPOSE", "Master tran."};

patch_cable_strength::Fixed vibratoMenu{"VIBRATO", ::Param::Local::PITCH_ADJUST, PatchSource::LFO_GLOBAL};

// Drum only
menu_item::DrumName drumNameMenu{"NAME"};

// Synth only
menu_item::SynthMode synthModeMenu{HAVE_OLED ? "Synth mode" : "MODE"};

bend_range::PerFinger drumBendRangeMenu{"Bend range"}; // The single option available for Drums
patched_param::Integer volumeMenu{HAVE_OLED ? "Level" : "VOLUME", "Master level", ::Param::Global::VOLUME_POST_FX};
patched_param::Pan panMenu{"PAN", ::Param::Local::PAN};

menu_item::Submenu soundEditorRootMenu{
    "Sound",
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
        &sequenceDirectionMenu,
    },
};

// Root menu for MIDI / CV
menu_item::Submenu soundEditorRootMenuMIDIOrCV{
    "MIDI inst.",
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
    "Audio clip",
    {
        &audioClipSampleMenu,
        &audioClipTransposeMenu,
        &audioClipLPFMenu,
        &audioClipHPFMenu,
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
    "Settings",
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
    {&portaMenu,              &polyphonyMenu,          &priorityMenu,                  &unisonDetuneMenu,              &numUnisonMenu,       nullptr,                nullptr,                  NULL                  },
    {&envReleaseMenu,         &envSustainMenu,         &envDecayMenu,                  &envAttackMenu,                 nullptr,              &lpfModeMenu,           &lpfResMenu,              &lpfFreqMenu          },
    {&envReleaseMenu,         &envSustainMenu,         &envDecayMenu,                  &envAttackMenu,                 nullptr,              comingSoonMenu,         &hpfResMenu,              &hpfFreqMenu          },
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
    {nullptr,                 nullptr,                 &priorityMenu,                  nullptr,                        nullptr,              nullptr,                nullptr,                  NULL                               },
    {nullptr,                 nullptr,                 nullptr,                        &audioClipAttackMenu,           nullptr,              &lpfModeMenu,           &audioClipLPFResMenu,     &audioClipLPFFreqMenu              },
    {nullptr,                 nullptr,                 nullptr,                        &audioClipAttackMenu,           nullptr,              comingSoonMenu,         &audioClipHPFResMenu,     &audioClipHPFFreqMenu              },
    {&compressorReleaseMenu,  &sidechainSyncMenu,      &audioClipCompressorVolumeMenu, &compressorAttackMenu,          &compressorShapeMenu, nullptr,                &bassMenu,                &bassFreqMenu                      },
    {nullptr,                 nullptr,                 nullptr,                        nullptr,                        nullptr,              nullptr,                &trebleMenu,              &trebleFreqMenu                    },
    {nullptr,                 nullptr,                 nullptr,                        &audioClipModFXTypeMenu,        &modFXOffsetMenu,     &modFXFeedbackMenu,     &audioClipModFXDepthMenu, &audioClipModFXRateMenu            },
    {nullptr,                 nullptr,                 nullptr,                        &audioClipReverbSendAmountMenu, &reverbPanMenu,       &reverbWidthMenu,       &reverbDampeningMenu,     &reverbRoomSizeMenu                },
    {&audioClipDelayRateMenu, &delaySyncMenu,          &delayAnalogMenu,               &audioClipDelayFeedbackMenu,    &delayPingPongMenu,   nullptr,                nullptr,                  NULL                               },
};
//clang-format on

#if HAVE_OLED
void setOscillatorNumberForTitles(int num) {
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

void setEnvelopeNumberForTitles(int num) {
	num += 1;
	envAttackMenu.format(num);
	envDecayMenu.format(num);
	envSustainMenu.format(num);
	envReleaseMenu.format(num);
}

void setModulatorNumberForTitles(int num) {
	num += 1;
	modulatorTransposeMenu.format(num);
	modulatorVolume.format(num);
	modulatorFeedbackMenu.format(num);
	modulatorPhaseMenu.format(num);
}

void setCvNumberForTitle(int num) {
	num += 1;
	cvVoltsMenu.format(num);
	cvTransposeMenu.format(num);
}
#endif
