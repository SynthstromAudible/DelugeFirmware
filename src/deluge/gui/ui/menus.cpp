#include "gui/menu_item/runtime_feature/setting.h"
#include "gui/menu_item/runtime_feature/settings.h"
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
#include "gui/menu_item/audio_clip/time_stretch.h"
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
#include "gui/menu_item/drum_name.h"
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
#include "gui/menu_item/keyboard/layout.h"
#include "gui/menu_item/key_range.h"
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
#include "gui/menu_item/midi/thru.h"
#include "gui/menu_item/midi/takeover.h"
#include "gui/menu_item/modulator/destination.h"
#include "gui/menu_item/modulator/transpose.h"
#include "gui/menu_item/mod_fx/depth.h"
#include "gui/menu_item/mod_fx/feedback.h"
#include "gui/menu_item/mod_fx/offset.h"
#include "gui/menu_item/mod_fx/type.h"
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
#include "gui/menu_item/patched_param/integer.h"
#include "gui/menu_item/patched_param/integer_non_fm.h"
#include "gui/menu_item/patched_param/pan.h"
#include "gui/menu_item/patched_param.h"
#include "gui/menu_item/patch_cable_strength/fixed.h"
#include "gui/menu_item/patch_cable_strength/range.h"
#include "gui/menu_item/patch_cable_strength/regular.h"
#include "gui/menu_item/patch_cable_strength.h"
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
#include "gui/menu_item/selection.h"
#include "gui/menu_item/selection_for_value.h"
#include "gui/menu_item/sequence/direction.h"
#include "gui/menu_item/shortcuts/version.h"
#include "gui/menu_item/sidechain/send.h"
#include "gui/menu_item/sidechain/sync.h"
#include "gui/menu_item/source/patched_param/fm.h"
#include "gui/menu_item/source/patched_param.h"
#include "gui/menu_item/source/transpose.h"
#include "gui/menu_item/source_selection/range.h"
#include "gui/menu_item/source_selection/regular.h"
#include "gui/menu_item/source_selection.h"
#include "gui/menu_item/submenu/actual_source.h"
#include "gui/menu_item/submenu/arpeggiator.h"
#include "gui/menu_item/submenu/bend.h"
#include "gui/menu_item/submenu/compressor.h"
#include "gui/menu_item/submenu/envelope.h"
#include "gui/menu_item/submenu/filter.h"
#include "gui/menu_item/submenu/modulator.h"
#include "gui/menu_item/submenu.h"
#include "gui/menu_item/submenu_referring_to_one_thing.h"
#include "gui/menu_item/swing/interval.h"
#include "gui/menu_item/sync_level/relative_to_song.h"
#include "gui/menu_item/sync_level.h"
#include "gui/menu_item/synth_mode.h"
#include "gui/menu_item/tempo/magnitude_matching.h"
#include "gui/menu_item/transpose.h"
#include "gui/menu_item/trigger/in/auto_start.h"
#include "gui/menu_item/trigger/in/ppqn.h"
#include "gui/menu_item/trigger/out/ppqn.h"
#include "gui/menu_item/unison/count.h"
#include "gui/menu_item/unison/detune.h"
#include "gui/menu_item/unpatched_param/pan.h"
#include "gui/menu_item/unpatched_param/updating_reverb_params.h"
#include "gui/menu_item/unpatched_param.h"
#include "gui/menu_item/value.h"
#include "gui/menu_item/voice/polyphony.h"
#include "gui/menu_item/voice/priority.h"
#include "gui/menu_item/dev_var/dev_var.h"
#include "processing/sound/sound.h"

using namespace menu_item;

#if HAVE_OLED
char oscTypeTitle[] = "OscX type";
char oscLevelTitle[] = "OscX level";
char waveIndexTitle[] = "OscX wave-ind.";
char carrierFeedback[] = "CarrierX feed.";
char sampleReverseTitle[] = "SampX reverse";
char sampleModeTitle[] = "SampX repeat";
char oscTransposeTitle[] = "OscX transpose";
char sampleSpeedTitle[] = "SampX speed";
char sampleInterpolationTitle[] = "SampX interp.";
char pulseWidthTitle[] = "OscX p. width";
char retriggerPhaseTitle[] = "OscX r. phase";

char attackTitle[] = "EnvX attack";
char decayTitle[] = "EnvX decay";
char sustainTitle[] = "EnvX sustain";
char releaseTitle[] = "EnvX release";

char modulatorTransposeTitle[] = "FM ModX tran.";
char modulatorLevelTitle[] = "FM ModX level";
char modulatorFeedbackTitle[] = "FM ModX f.back";
char modulatorRetriggerPhaseTitle[] = "FM ModX retrig";

char cvVoltsTitle[] = "CVx V/octave";
char cvTransposeTitle[] = "CVx transpose";

void setOscillatorNumberForTitles(int s) {
	oscTypeTitle[3] = '1' + s;
	oscLevelTitle[3] = '1' + s;
	waveIndexTitle[3] = '1' + s;
	oscTransposeTitle[3] = '1' + s;
	pulseWidthTitle[3] = '1' + s;
	retriggerPhaseTitle[3] = '1' + s;

	carrierFeedback[7] = '1' + s;

	sampleReverseTitle[4] = '1' + s;
	sampleModeTitle[4] = '1' + s;
	sampleSpeedTitle[4] = '1' + s;
	sampleInterpolationTitle[4] = '1' + s;
}

void setEnvelopeNumberForTitles(int e) {
	attackTitle[3] = '1' + e;
	decayTitle[3] = '1' + e;
	sustainTitle[3] = '1' + e;
	releaseTitle[3] = '1' + e;
}

void setModulatorNumberForTitles(int m) {
	modulatorTransposeTitle[6] = '1' + m;
	modulatorLevelTitle[6] = '1' + m;
	modulatorFeedbackTitle[6] = '1' + m;
	modulatorRetriggerPhaseTitle[6] = '1' + m;
}

void setCvNumberForTitle(int m) {
	cvVoltsTitle[2] = '1' + m;
	cvTransposeTitle[2] = '1' + m;
}
#endif

// Dev vars
dev_var::AMenu devVarAMenu;
dev_var::BMenu devVarBMenu;
dev_var::CMenu devVarCMenu;
dev_var::DMenu devVarDMenu;
dev_var::EMenu devVarEMenu;
dev_var::FMenu devVarFMenu;
dev_var::GMenu devVarGMenu;

// LPF menu ----------------------------------------------------------------------------------------------------

filter::LPFFreq lpfFreqMenu{"Frequency", PARAM_LOCAL_LPF_FREQ};
patched_param::IntegerNonFM lpfResMenu{"Resonance", PARAM_LOCAL_LPF_RESONANCE};
filter::LPFMode lpfModeMenu{"MODE"};

MenuItem* lpfMenuItems[] = {&lpfFreqMenu, &lpfResMenu, &lpfModeMenu, NULL};
submenu::Filter lpfMenu{"LPF", lpfMenuItems};

// HPF menu ----------------------------------------------------------------------------------------------------

filter::HPFFreq hpfFreqMenu{"Frequency", PARAM_LOCAL_HPF_FREQ};
patched_param::IntegerNonFM hpfResMenu{"Resonance", PARAM_LOCAL_HPF_RESONANCE};

MenuItem* hpfMenuItems[] = {&hpfFreqMenu, &hpfResMenu, NULL};
submenu::Filter hpfMenu{"HPF", hpfMenuItems};

// Envelope menu ----------------------------------------------------------------------------------------------------

source::PatchedParam envAttackMenu{"ATTACK", PARAM_LOCAL_ENV_0_ATTACK};
source::PatchedParam envDecayMenu{"DECAY", PARAM_LOCAL_ENV_0_DECAY};
source::PatchedParam envSustainMenu{"SUSTAIN", PARAM_LOCAL_ENV_0_SUSTAIN};
source::PatchedParam envReleaseMenu{"RELEASE", PARAM_LOCAL_ENV_0_RELEASE};
MenuItem* envMenuItems[] = {&envAttackMenu, &envDecayMenu, &envSustainMenu, &envReleaseMenu, NULL};

submenu::Envelope env0Menu{HAVE_OLED ? "Envelope 1" : "ENV1", envMenuItems, 0};
submenu::Envelope env1Menu{HAVE_OLED ? "Envelope 2" : "ENV2", envMenuItems, 1};

// Osc menu -------------------------------------------------------------------------------------------------------

osc::Type oscTypeMenu{"TYPE"};
osc::source::WaveIndex sourceWaveIndexMenu{"Wave-index", PARAM_LOCAL_OSC_A_WAVE_INDEX};
osc::source::Volume sourceVolumeMenu{HAVE_OLED ? "Level" : "VOLUME", PARAM_LOCAL_OSC_A_VOLUME};
osc::source::Feedback sourceFeedbackMenu{"FEEDBACK", PARAM_LOCAL_CARRIER_0_FEEDBACK};
osc::AudioRecorder audioRecorderMenu{"Record audio"};
sample::Reverse sampleReverseMenu{"REVERSE"};
sample::Repeat sampleRepeatMenu{HAVE_OLED ? "Repeat mode" : "MODE"};
sample::Start sampleStartMenu{"Start-point"};
sample::End sampleEndMenu{"End-point"};
sample::Transpose sourceTransposeMenu{"TRANSPOSE", PARAM_LOCAL_OSC_A_PITCH_ADJUST};
sample::PitchSpeed samplePitchSpeedMenu{HAVE_OLED ? "Pitch/speed" : "PISP"};
sample::TimeStretch timeStretchMenu{"SPEED"};
sample::Interpolation interpolationMenu{"INTERPOLATION"};
osc::PulseWidth pulseWidthMenu{"PULSE WIDTH", PARAM_LOCAL_OSC_A_PHASE_WIDTH};
osc::Sync oscSyncMenu{HAVE_OLED ? "Oscillator sync" : "SYNC"};
osc::RetriggerPhase oscPhaseMenu{"Retrigger phase", false};

MenuItem* oscMenuItems[] = {&oscTypeMenu,        &sourceVolumeMenu,    &sourceWaveIndexMenu,
                            &sourceFeedbackMenu, &fileSelectorMenu,    &audioRecorderMenu,
                            &sampleReverseMenu,  &sampleRepeatMenu,    &sampleStartMenu,
                            &sampleEndMenu,      &sourceTransposeMenu, &samplePitchSpeedMenu,
                            &timeStretchMenu,    &interpolationMenu,   &pulseWidthMenu,
                            &oscSyncMenu,        &oscPhaseMenu,        NULL};

submenu::ActualSource source0Menu{HAVE_OLED ? "Oscillator 1" : "OSC1", oscMenuItems, 0};
submenu::ActualSource source1Menu{HAVE_OLED ? "Oscillator 2" : "OSC2", oscMenuItems, 1};

// Unison --------------------------------------------------------------------------------------

unison::Count numUnisonMenu{HAVE_OLED ? "Unison number" : "NUM"};
unison::Detune unisonDetuneMenu{HAVE_OLED ? "Unison detune" : "DETUNE"};

MenuItem* unisonMenuItems[] = {&numUnisonMenu, &unisonDetuneMenu, NULL};

// Arp --------------------------------------------------------------------------------------
arpeggiator::Mode arpModeMenu{"MODE"};
arpeggiator::Sync arpSyncMenu{"SYNC"};
arpeggiator::Octaves arpOctavesMenu{HAVE_OLED ? "Number of octaves" : "OCTAVES"};
arpeggiator::Gate arpGateMenu{"GATE", PARAM_UNPATCHED_SOUND_ARP_GATE};
arpeggiator::midi_cv::Gate arpGateMenuMIDIOrCV{"GATE"};
arpeggiator::Rate arpRateMenu{"RATE", PARAM_GLOBAL_ARP_RATE};
arpeggiator::midi_cv::Rate arpRateMenuMIDIOrCV{"RATE"};

MenuItem* arpMenuItems[] = {&arpModeMenu,         &arpSyncMenu, &arpOctavesMenu,      &arpGateMenu,
                            &arpGateMenuMIDIOrCV, &arpRateMenu, &arpRateMenuMIDIOrCV, NULL};

// Voice menu ----------------------------------------------------------------------------------------------------

voice::Polyphony polyphonyMenu{"POLYPHONY"};
Submenu unisonMenu{"UNISON", unisonMenuItems};
UnpatchedParam portaMenu{"PORTAMENTO", PARAM_UNPATCHED_SOUND_PORTA};
submenu::Arpeggiator arpMenu{"ARPEGGIATOR", arpMenuItems};
voice::Priority priorityMenu{"PRIORITY"};

static MenuItem* voiceMenuItems[] = {&polyphonyMenu, &unisonMenu, &portaMenu, &arpMenu, &priorityMenu, NULL};

// Modulator menu -----------------------------------------------------------------------

modulator::Transpose modulatorTransposeMenu{"Transpose", PARAM_LOCAL_MODULATOR_0_PITCH_ADJUST};
source::patched_param::FM modulatorVolume{HAVE_OLED ? "Level" : "AMOUNT", PARAM_LOCAL_MODULATOR_0_VOLUME};
source::patched_param::FM modulatorFeedbackMenu{"FEEDBACK", PARAM_LOCAL_MODULATOR_0_FEEDBACK};
modulator::Destination modulatorDestMenu{"Destination"};
osc::RetriggerPhase modulatorPhaseMenu{"Retrigger phase", true};

// LFO1 menu ---------------------------------------------------------------------------------

lfo::global::Type lfo1TypeMenu{HAVE_OLED ? "SHAPE" : "TYPE"};
lfo::global::Rate lfo1RateMenu{"RATE", PARAM_GLOBAL_LFO_FREQ};
lfo::global::Sync lfo1SyncMenu{"SYNC"};

MenuItem* lfo1MenuItems[] = {&lfo1TypeMenu, &lfo1RateMenu, &lfo1SyncMenu, NULL};

// LFO2 menu ---------------------------------------------------------------------------------
lfo::local::Type lfo2TypeMenu{HAVE_OLED ? "SHAPE" : "TYPE"};
patched_param::Integer lfo2RateMenu{"RATE", PARAM_LOCAL_LFO_LOCAL_FREQ};

MenuItem* lfo2MenuItems[] = {&lfo2TypeMenu, &lfo2RateMenu, NULL};

// Mod FX ----------------------------------------------------------------------------------
mod_fx::Type modFXTypeMenu{"TYPE"};
patched_param::Integer modFXRateMenu{"RATE", PARAM_GLOBAL_MOD_FX_RATE};
mod_fx::Feedback modFXFeedbackMenu{"FEEDBACK", PARAM_UNPATCHED_MOD_FX_FEEDBACK};
mod_fx::Depth modFXDepthMenu{"DEPTH", PARAM_GLOBAL_MOD_FX_DEPTH};
mod_fx::Offset modFXOffsetMenu{"OFFSET", PARAM_UNPATCHED_MOD_FX_OFFSET};

MenuItem* modFXMenuItems[] = {&modFXTypeMenu,  &modFXRateMenu,   &modFXFeedbackMenu,
                              &modFXDepthMenu, &modFXOffsetMenu, NULL};

// EQ -------------------------------------------------------------------------------------
UnpatchedParam bassMenu{"BASS", PARAM_UNPATCHED_BASS};
UnpatchedParam trebleMenu{"TREBLE", PARAM_UNPATCHED_TREBLE};
UnpatchedParam bassFreqMenu{HAVE_OLED ? "Bass frequency" : "BAFR", PARAM_UNPATCHED_BASS_FREQ};
UnpatchedParam trebleFreqMenu{HAVE_OLED ? "Treble frequency" : "TRFR", PARAM_UNPATCHED_TREBLE_FREQ};

MenuItem* eqMenuItems[] = {&bassMenu, &trebleMenu, &bassFreqMenu, &trebleFreqMenu, NULL};

// Delay ---------------------------------------------------------------------------------
patched_param::Integer delayFeedbackMenu{"AMOUNT", PARAM_GLOBAL_DELAY_FEEDBACK};
patched_param::Integer delayRateMenu{"RATE", PARAM_GLOBAL_DELAY_RATE};
delay::PingPong delayPingPongMenu{"Pingpong"};
delay::Analog delayAnalogMenu{"TYPE"};
delay::Sync delaySyncMenu{"SYNC"};

MenuItem* delayMenuItems[] = {&delayFeedbackMenu, &delayRateMenu, &delayPingPongMenu,
                              &delayAnalogMenu,   &delaySyncMenu, NULL};

// Bend Ranges -------------------------------------------------------------------------------

bend_range::Main mainBendRangeMenu{"Normal"};
bend_range::PerFinger perFingerBendRangeMenu{HAVE_OLED ? "Poly / finger / MPE" : "MPE"};

MenuItem* bendMenuItems[] = {&mainBendRangeMenu, &perFingerBendRangeMenu, NULL};

// Sidechain/Compressor-----------------------------------------------------------------------

sidechain::Send sidechainSendMenu{"Send to sidechain"};
compressor::VolumeShortcut compressorVolumeShortcutMenu{"Volume ducking", PARAM_GLOBAL_VOLUME_POST_REVERB_SEND,
                                                        PATCH_SOURCE_COMPRESSOR};
reverb::compressor::Volume reverbCompressorVolumeMenu{"Volume ducking"};
sidechain::Sync sidechainSyncMenu{"SYNC"};
compressor::Attack compressorAttackMenu{"ATTACK"};
compressor::Release compressorReleaseMenu{"RELEASE"};
unpatched_param::UpdatingReverbParams compressorShapeMenu{"SHAPE", PARAM_UNPATCHED_COMPRESSOR_SHAPE};
reverb::compressor::Shape reverbCompressorShapeMenu{"SHAPE"};

MenuItem* sidechainMenuItemsForSound[] = {&sidechainSendMenu,
                                          &compressorVolumeShortcutMenu,
                                          &sidechainSyncMenu,
                                          &compressorAttackMenu,
                                          &compressorReleaseMenu,
                                          &compressorShapeMenu,
                                          NULL};

MenuItem* sidechainMenuItemsForReverb[] = {&reverbCompressorVolumeMenu, &sidechainSyncMenu,
                                           &compressorAttackMenu,       &compressorReleaseMenu,
                                           &reverbCompressorShapeMenu,  NULL};

// Reverb ----------------------------------------------------------------------------------
patched_param::Integer reverbAmountMenu{"AMOUNT", PARAM_GLOBAL_REVERB_AMOUNT};
reverb::RoomSize reverbRoomSizeMenu{HAVE_OLED ? "Room size" : "SIZE"};
reverb::Dampening reverbDampeningMenu{"DAMPENING"};
reverb::Width reverbWidthMenu{"WIDTH"};
reverb::Pan reverbPanMenu{"PAN"};
submenu::Compressor reverbCompressorMenu{HAVE_OLED ? "Reverb sidechain" : "SIDE", sidechainMenuItemsForReverb, true};

MenuItem* reverbMenuItems[] = {&reverbAmountMenu,
                               &reverbRoomSizeMenu,
                               &reverbDampeningMenu,
                               &reverbWidthMenu,
                               &reverbPanMenu,
                               &reverbCompressorMenu,
                               NULL};

// FX ----------------------------------------------------------------------------------------
Submenu modFXMenu{HAVE_OLED ? "Mod-fx" : "MODU", modFXMenuItems};
Submenu eqMenu{"EQ", eqMenuItems};
Submenu delayMenu{"DELAY", delayMenuItems};
Submenu reverbMenu{"REVERB", reverbMenuItems};
fx::Clipping clippingMenu{"SATURATION"};
UnpatchedParam srrMenu{"DECIMATION", PARAM_UNPATCHED_SAMPLE_RATE_REDUCTION};
UnpatchedParam bitcrushMenu{HAVE_OLED ? "Bitcrush" : "CRUSH", PARAM_UNPATCHED_BITCRUSHING};

MenuItem* fxMenuItems[] = {&modFXMenu, &eqMenu, &delayMenu, &reverbMenu, &clippingMenu, &srrMenu, &bitcrushMenu, NULL};

// MIDIInstrument menu ----------------------------------------------------------------------

midi::Bank midiBankMenu{"BANK"};
midi::Sub midiSubMenu{HAVE_OLED ? "Sub-bank" : "SUB"};
midi::PGM midiPGMMenu{"PGM"};

// Clip-level stuff --------------------------------------------------------------------------

sequence::Direction sequenceDirectionMenu{HAVE_OLED ? "Play direction" : "DIRECTION"};

// AudioClip stuff ---------------------------------------------------------------------------

// Sample Menu

audio_clip::TimeStretch audioClipTimeStretchMenu{HAVE_OLED ? "Timestretch" : "STRETCH"};
audio_clip::Reverse audioClipReverseMenu{"REVERSE"};
audio_clip::SampleMarkerEditor audioClipSampleMarkerEditorMenuStart{"", MARKER_START};
audio_clip::SampleMarkerEditor audioClipSampleMarkerEditorMenuEnd{"WAVEFORM", MARKER_END};

MenuItem* audioClipSampleMenuItems[] = {&fileSelectorMenu,
                                        &audioClipTimeStretchMenu,
                                        &audioClipReverseMenu,
                                        &samplePitchSpeedMenu,
                                        &audioClipSampleMarkerEditorMenuEnd,
                                        &interpolationMenu,
                                        NULL};
Submenu audioClipSampleMenu{"SAMPLE", audioClipSampleMenuItems};

// LPF Menu
audio_clip::LPFFreq audioClipLPFFreqMenu{"Frequency", PARAM_UNPATCHED_GLOBALEFFECTABLE_LPF_FREQ};
UnpatchedParam audioClipLPFResMenu{"Resonance", PARAM_UNPATCHED_GLOBALEFFECTABLE_LPF_RES};

MenuItem* audioClipLPFMenuItems[] = {&audioClipLPFFreqMenu, &audioClipLPFResMenu, &lpfModeMenu, NULL};
Submenu audioClipLPFMenu{"LPF", audioClipLPFMenuItems};

// HPF Menu
audio_clip::HPFFreq audioClipHPFFreqMenu{"Frequency", PARAM_UNPATCHED_GLOBALEFFECTABLE_HPF_FREQ};
UnpatchedParam audioClipHPFResMenu{"Resonance", PARAM_UNPATCHED_GLOBALEFFECTABLE_HPF_RES};

MenuItem* audioClipHPFMenuItems[] = {&audioClipHPFFreqMenu, &audioClipHPFResMenu, NULL};
Submenu audioClipHPFMenu{"HPF", audioClipHPFMenuItems};

// Mod FX Menu
audio_clip::mod_fx::Type audioClipModFXTypeMenu{"TYPE"};
UnpatchedParam audioClipModFXRateMenu{"RATE", PARAM_UNPATCHED_GLOBALEFFECTABLE_MOD_FX_RATE};
UnpatchedParam audioClipModFXDepthMenu{"DEPTH", PARAM_UNPATCHED_GLOBALEFFECTABLE_MOD_FX_DEPTH};

MenuItem* audioClipModFXMenuItems[] = {&audioClipModFXTypeMenu,  &audioClipModFXRateMenu, &modFXFeedbackMenu,
                                       &audioClipModFXDepthMenu, &modFXOffsetMenu,        NULL};
Submenu audioClipModFXMenu{HAVE_OLED ? "Mod-fx" : "MODU", audioClipModFXMenuItems};

// Delay Menu
UnpatchedParam audioClipDelayRateMenu{"AMOUNT", PARAM_UNPATCHED_GLOBALEFFECTABLE_DELAY_AMOUNT};
UnpatchedParam audioClipDelayFeedbackMenu{"RATE", PARAM_UNPATCHED_GLOBALEFFECTABLE_DELAY_RATE};

MenuItem* audioClipDelayMenuItems[] = {
    &audioClipDelayFeedbackMenu, &audioClipDelayRateMenu, &delayPingPongMenu, &delayAnalogMenu, &delaySyncMenu, NULL};
Submenu audioClipDelayMenu{"DELAY", audioClipDelayMenuItems};

// Reverb Menu
UnpatchedParam audioClipReverbSendAmountMenu{"AMOUNT", PARAM_UNPATCHED_GLOBALEFFECTABLE_REVERB_SEND_AMOUNT};
MenuItem* audioClipReverbMenuItems[] = {&audioClipReverbSendAmountMenu,
                                        &reverbRoomSizeMenu,
                                        &reverbDampeningMenu,
                                        &reverbWidthMenu,
                                        &reverbPanMenu,
                                        &reverbCompressorMenu,
                                        NULL};
Submenu audioClipReverbMenu{"REVERB", audioClipReverbMenuItems};

// Sidechain menu
unpatched_param::UpdatingReverbParams audioClipCompressorVolumeMenu{"Volume ducking",
                                                                    PARAM_UNPATCHED_GLOBALEFFECTABLE_SIDECHAIN_VOLUME};
MenuItem* audioClipSidechainMenuItems[] = {&audioClipCompressorVolumeMenu, &sidechainSyncMenu,   &compressorAttackMenu,
                                           &compressorReleaseMenu,         &compressorShapeMenu, NULL};

Submenu audioClipCompressorMenu{"Sidechain compressor", audioClipSidechainMenuItems};

audio_clip::Transpose audioClipTransposeMenu{"TRANSPOSE"};
audio_clip::Attack audioClipAttackMenu{"ATTACK"};

MenuItem* audioClipFXMenuItems[] = {&audioClipModFXMenu, &eqMenu,  &audioClipDelayMenu, &audioClipReverbMenu,
                                    &clippingMenu,       &srrMenu, &bitcrushMenu,       NULL};
Submenu audioClipFXMenu{"FX", audioClipFXMenuItems};

UnpatchedParam audioClipLevelMenu{HAVE_OLED ? "Level" : "VOLUME", PARAM_UNPATCHED_GLOBALEFFECTABLE_VOLUME};
unpatched_param::Pan audioClipPanMenu{"PAN", PARAM_UNPATCHED_GLOBALEFFECTABLE_PAN};

#define comingSoonMenu (MenuItem*)0xFFFFFFFF

const MenuItem* midiOrCVParamShortcuts[8] = {
    &arpRateMenuMIDIOrCV, &arpSyncMenu, &arpGateMenuMIDIOrCV, &arpOctavesMenu, &arpModeMenu, NULL, NULL, NULL,
};

// Gate stuff
gate::Mode gateModeMenu{};
gate::OffTime gateOffTimeMenu{HAVE_OLED ? "Min. off-time" : ""};

// Root menu

// CV Menu
cv::Volts cvVoltsMenu{"Volts per octave"};
cv::Transpose cvTransposeMenu{"TRANSPOSE"};

MenuItem* cvMenuItems[] = {&cvVoltsMenu, &cvTransposeMenu, NULL};

Submenu cvSubmenu{"", cvMenuItems};

cv::Selection cvSelectionMenu{"CV"};
gate::Selection gateSelectionMenu{"GATE"};

swing::Interval swingIntervalMenu{HAVE_OLED ? "Swing interval" : "SWIN"};

// Pads menu
shortcuts::Version shortcutsVersionMenu{HAVE_OLED ? "Shortcuts version" : "SHOR"};
keyboard::Layout keyboardLayoutMenu{HAVE_OLED ? "Keyboard for text" : "KEYB"};

// Colours submenu
MenuItem* coloursMenuItems[] = {&activeColourMenu, &stoppedColourMenu, &mutedColourMenu, &soloColourMenu, NULL};
Submenu coloursSubmenu{"COLOURS", coloursMenuItems};

static MenuItem* layoutMenuItems[] = {&shortcutsVersionMenu, &keyboardLayoutMenu, &coloursSubmenu, NULL};
Submenu padsSubmenu{"PADS", layoutMenuItems};

// Record submenu
record::Quantize recordQuantizeMenu{"Quantization"};
record::Margins recordMarginsMenu{HAVE_OLED ? "Loop margins" : "MARGINS"};
record::CountIn recordCountInMenu{"Count-in"};
monitor::Mode monitorModeMenu{HAVE_OLED ? "Sampling monitoring" : "MONITORING"};

MenuItem* recordMenuItems[] = {&recordCountInMenu, &recordQuantizeMenu, &recordMarginsMenu, &monitorModeMenu, NULL};
Submenu recordSubmenu{"Recording", recordMenuItems};

sample::browser_preview::Mode sampleBrowserPreviewModeMenu{HAVE_OLED ? "Sample preview" : "PREV"};

flash::Status flashStatusMenu{HAVE_OLED ? "Play-cursor" : "CURS"};

#ifdef DBT_FW_VERSION_STRING
char const* firmwareString = DBT_FW_VERSION_STRING;
#else
char const* firmwareString = "4.1.4-alpha3-c";
#endif // ifdef DBT_FW_VERSION_STRING

firmware::Version firmwareVersionMenu{"Firmware version"};

runtime_feature::Settings runtimeFeatureSettingsMenu{HAVE_OLED ? "Community fts." : "FEAT"};

// CV menu

// MIDI
// MIDI thru
midi::Thru midiThruMenu{HAVE_OLED ? "MIDI-thru" : "THRU"};

// MIDI Takeover
midi::Takeover midiTakeoverMenu{HAVE_OLED ? "TAKEOVER" : "TOVR"};

// MIDI commands submenu
midi::Command playbackRestartMidiCommand{"Restart", GLOBAL_MIDI_COMMAND_PLAYBACK_RESTART};
midi::Command playMidiCommand{"PLAY", GLOBAL_MIDI_COMMAND_PLAY};
midi::Command recordMidiCommand{HAVE_OLED ? "Record" : "REC", GLOBAL_MIDI_COMMAND_RECORD};
midi::Command tapMidiCommand{"Tap tempo", GLOBAL_MIDI_COMMAND_TAP};
midi::Command undoMidiCommand{"UNDO", GLOBAL_MIDI_COMMAND_UNDO};
midi::Command redoMidiCommand{"REDO", GLOBAL_MIDI_COMMAND_REDO};
midi::Command loopMidiCommand{"LOOP", GLOBAL_MIDI_COMMAND_LOOP};
midi::Command loopContinuousLayeringMidiCommand{"LAYERING loop", GLOBAL_MIDI_COMMAND_LOOP_CONTINUOUS_LAYERING};
MenuItem* midiCommandsMenuItems[] = {&playMidiCommand,
                                     &playbackRestartMidiCommand,
                                     &recordMidiCommand,
                                     &tapMidiCommand,
                                     &undoMidiCommand,
                                     &redoMidiCommand,
                                     &loopMidiCommand,
                                     &loopContinuousLayeringMidiCommand,
                                     NULL};
Submenu midiCommandsMenu{HAVE_OLED ? "Commands" : "CMD", midiCommandsMenuItems};

// MIDI device submenu - for after we've selected which device we want it for

midi::DefaultVelocityToLevel defaultVelocityToLevelMenu{"VELOCITY"};
MenuItem* midiDeviceMenuItems[] = {&mpe::directionSelectorMenu, &defaultVelocityToLevelMenu, NULL};
Submenu midiDeviceMenu{NULL, midiDeviceMenuItems};

// MIDI input differentiation menu
midi::InputDifferentiation midiInputDifferentiationMenu{"Differentiate inputs"};

// MIDI clock menu
midi::ClockOutStatus midiClockOutStatusMenu{HAVE_OLED ? "Output" : "OUT"};
midi::ClockInStatus midiClockInStatusMenu{HAVE_OLED ? "Input" : "IN"};
tempo::MagnitudeMatching tempoMagnitudeMatchingMenu{HAVE_OLED ? "Tempo magnitude matching" : "MAGN"};
MenuItem* midiClockMenuItems[] = {&midiClockInStatusMenu, &midiClockOutStatusMenu, &tempoMagnitudeMatchingMenu, NULL};
Submenu midiClockMenu{"CLOCK", midiClockMenuItems};

//Midi devices menu
midi::Devices midi::devicesMenu{"Devices"};
mpe::DirectionSelector mpe::directionSelectorMenu{"MPE"};

//MIDI menu
MenuItem* midiMenuItems[] = {
    &midiClockMenu,     &midiThruMenu, &midiTakeoverMenu, &midiCommandsMenu, &midiInputDifferentiationMenu,
    &midi::devicesMenu, NULL};
Submenu midiMenu{"MIDI", midiMenuItems};

// Trigger clock in menu
trigger::in::PPQN triggerInPPQNMenu{"PPQN"};
trigger::in::AutoStart triggerInAutoStartMenu{"Auto-start"};
MenuItem* triggerClockInMenuItems[] = {&triggerInPPQNMenu, &triggerInAutoStartMenu, nullptr};

// Trigger clock out menu
trigger::out::PPQN triggerOutPPQNMenu{"PPQN"};
MenuItem* triggerClockOutMenuItems[] = {&triggerOutPPQNMenu, nullptr};

// Clock menu
Submenu triggerClockInMenu{HAVE_OLED ? "Input" : "IN", triggerClockInMenuItems};
Submenu triggerClockOutMenu{HAVE_OLED ? "Output" : "OUT", triggerClockOutMenuItems};

// Trigger clock menu
MenuItem* triggerClockMenuItems[] = {&triggerClockInMenu, &triggerClockOutMenu, NULL};
Submenu triggerClockMenu{HAVE_OLED ? "Trigger clock" : "TCLOCK", triggerClockMenuItems};

// Defaults menu
IntegerRange defaultTempoMenu{"TEMPO", 60, 240};
IntegerRange defaultSwingMenu{"SWING", 1, 99};
KeyRange defaultKeyMenu{"KEY"};
defaults::Scale defaultScaleMenu{"SCALE"};
defaults::Velocity defaultVelocityMenu{"VELOCITY"};
defaults::Magnitude defaultMagnitudeMenu{"RESOLUTION"};
defaults::BendRange defaultBendRangeMenu{"Bend range"};
MenuItem* defaultsMenuItems[] = {&defaultTempoMenu,    &defaultSwingMenu,     &defaultKeyMenu,       &defaultScaleMenu,
                                 &defaultVelocityMenu, &defaultMagnitudeMenu, &defaultBendRangeMenu, NULL};

Submenu defaultsSubmenu{"DEFAULTS", defaultsMenuItems};

// Sound editor menu -----------------------------------------------------------------------------

// FM only
MenuItem* modulatorMenuItems[] = {&modulatorVolume,   &modulatorTransposeMenu, &modulatorFeedbackMenu,
                                  &modulatorDestMenu, &modulatorPhaseMenu,     NULL};

submenu::Modulator modulator0Menu{HAVE_OLED ? "FM modulator 1" : "MOD1", modulatorMenuItems, 0};
submenu::Modulator modulator1Menu{HAVE_OLED ? "FM modulator 2" : "MOD2", modulatorMenuItems, 1};

// Not FM
patched_param::IntegerNonFM noiseMenu{HAVE_OLED ? "Noise level" : "NOISE", PARAM_LOCAL_NOISE_VOLUME};

MasterTranspose masterTransposeMenu{HAVE_OLED ? "Master transpose" : "TRANSPOSE"};

patch_cable_strength::Fixed vibratoMenu{"VIBRATO", PARAM_LOCAL_PITCH_ADJUST, PATCH_SOURCE_LFO_GLOBAL};

// Drum only
menu_item::DrumName drumNameMenu{"NAME"};

// Synth only
SynthMode synthModeMenu{HAVE_OLED ? "Synth mode" : "MODE"};

Submenu lfo0Menu{"LFO1", lfo1MenuItems};
Submenu lfo1Menu{"LFO2", lfo2MenuItems};

Submenu voiceMenu{"VOICE", voiceMenuItems};
Submenu fxMenu{"FX", fxMenuItems};
submenu::Compressor compressorMenu{"Sidechain compressor", sidechainMenuItemsForSound, false};

submenu::Bend bendMenu{"Bend range", bendMenuItems};   // The submenu
bend_range::PerFinger drumBendRangeMenu{"Bend range"}; // The single option available for Drums
patched_param::Integer volumeMenu{HAVE_OLED ? "Level" : "VOLUME", PARAM_GLOBAL_VOLUME_POST_FX};
patched_param::Pan panMenu{"PAN", PARAM_LOCAL_PAN};

MenuItem* soundEditorRootMenuItems[] = {&source0Menu,
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
                                        NULL};

menu_item::Submenu soundEditorRootMenu{"Sound", soundEditorRootMenuItems};

// Root menu for MIDI / CV
MenuItem* soundEditorRootMenuItemsMIDIOrCV[] = {&midiPGMMenu, &midiBankMenu,          &midiSubMenu, &arpMenu,
                                                &bendMenu,    &sequenceDirectionMenu, NULL};

menu_item::Submenu soundEditorRootMenuMIDIOrCV{"MIDI inst.", soundEditorRootMenuItemsMIDIOrCV};

// Root menu for AudioClips
MenuItem* soundEditorRootMenuItemsAudioClip[] = {&audioClipSampleMenu,
                                                 &audioClipTransposeMenu,
                                                 &audioClipLPFMenu,
                                                 &audioClipHPFMenu,
                                                 &audioClipAttackMenu,
                                                 &priorityMenu,
                                                 &audioClipFXMenu,
                                                 &audioClipCompressorMenu,
                                                 &audioClipLevelMenu,
                                                 &audioClipPanMenu,
                                                 NULL};
menu_item::Submenu soundEditorRootMenuAudioClip{"Audio clip", soundEditorRootMenuItemsAudioClip};

// Root Menu
MenuItem* rootSettingsMenuItems[] = {&cvSelectionMenu,
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
                                     NULL};
Submenu settingsRootMenu{"Settings", rootSettingsMenuItems};

MenuItem* paramShortcutsForSounds[][8] = {
    // Post V3
    {&sampleRepeatMenu, &sampleReverseMenu, &timeStretchMenu, &samplePitchSpeedMenu, &audioRecorderMenu,
     &fileSelectorMenu, &interpolationMenu, &sampleStartMenu},
    {&sampleRepeatMenu, &sampleReverseMenu, &timeStretchMenu, &samplePitchSpeedMenu, &audioRecorderMenu,
     &fileSelectorMenu, &interpolationMenu, &sampleStartMenu},
    {&sourceVolumeMenu, &sourceTransposeMenu, &oscTypeMenu, &pulseWidthMenu, &oscPhaseMenu, &sourceFeedbackMenu,
     &sourceWaveIndexMenu, &noiseMenu},
    {&sourceVolumeMenu, &sourceTransposeMenu, &oscTypeMenu, &pulseWidthMenu, &oscPhaseMenu, &sourceFeedbackMenu,
     &sourceWaveIndexMenu, &oscSyncMenu},
    {&modulatorVolume, &modulatorTransposeMenu, comingSoonMenu, comingSoonMenu, &modulatorPhaseMenu,
     &modulatorFeedbackMenu, comingSoonMenu, &sequenceDirectionMenu},
    {&modulatorVolume, &modulatorTransposeMenu, comingSoonMenu, comingSoonMenu, &modulatorPhaseMenu,
     &modulatorFeedbackMenu, &modulatorDestMenu, NULL},
    {&volumeMenu, &masterTransposeMenu, &vibratoMenu, &panMenu, &synthModeMenu, &srrMenu, &bitcrushMenu, &clippingMenu},
    {&portaMenu, &polyphonyMenu, &priorityMenu, &unisonDetuneMenu, &numUnisonMenu, NULL, NULL, NULL},
    {&envReleaseMenu, &envSustainMenu, &envDecayMenu, &envAttackMenu, NULL, &lpfModeMenu, &lpfResMenu, &lpfFreqMenu},
    {&envReleaseMenu, &envSustainMenu, &envDecayMenu, &envAttackMenu, NULL, comingSoonMenu, &hpfResMenu, &hpfFreqMenu},
    {&compressorReleaseMenu, &sidechainSyncMenu, &compressorVolumeShortcutMenu, &compressorAttackMenu,
     &compressorShapeMenu, &sidechainSendMenu, &bassMenu, &bassFreqMenu},
    {&arpRateMenu, &arpSyncMenu, &arpGateMenu, &arpOctavesMenu, &arpModeMenu, &drumNameMenu, &trebleMenu,
     &trebleFreqMenu},
    {&lfo1RateMenu, &lfo1SyncMenu, &lfo1TypeMenu, &modFXTypeMenu, &modFXOffsetMenu, &modFXFeedbackMenu, &modFXDepthMenu,
     &modFXRateMenu},
    {&lfo2RateMenu, comingSoonMenu, &lfo2TypeMenu, &reverbAmountMenu, &reverbPanMenu, &reverbWidthMenu,
     &reverbDampeningMenu, &reverbRoomSizeMenu},
    {&delayRateMenu, &delaySyncMenu, &delayAnalogMenu, &delayFeedbackMenu, &delayPingPongMenu, NULL, NULL, NULL}};

MenuItem* paramShortcutsForAudioClips[][8] = {
    {&audioClipTimeStretchMenu, &audioClipReverseMenu, NULL, &samplePitchSpeedMenu, NULL, &fileSelectorMenu,
     &interpolationMenu, &audioClipSampleMarkerEditorMenuEnd},
    {NULL, &audioClipReverseMenu, NULL, &samplePitchSpeedMenu, NULL, &fileSelectorMenu, &interpolationMenu,
     &audioClipSampleMarkerEditorMenuEnd},
    {&audioClipLevelMenu, &audioClipTransposeMenu, NULL, NULL, NULL, NULL, NULL, NULL},
    {&audioClipLevelMenu, &audioClipTransposeMenu, NULL, NULL, NULL, NULL, NULL, NULL},
    {NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL},
    {NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL},
    {&audioClipLevelMenu, &audioClipTransposeMenu, NULL, &audioClipPanMenu, NULL, &srrMenu, &bitcrushMenu,
     &clippingMenu},
    {NULL, NULL, &priorityMenu, NULL, NULL, NULL, NULL, NULL},
    {NULL, NULL, NULL, &audioClipAttackMenu, NULL, &lpfModeMenu, &audioClipLPFResMenu, &audioClipLPFFreqMenu},
    {NULL, NULL, NULL, &audioClipAttackMenu, NULL, comingSoonMenu, &audioClipHPFResMenu, &audioClipHPFFreqMenu},
    {&compressorReleaseMenu, &sidechainSyncMenu, &audioClipCompressorVolumeMenu, &compressorAttackMenu,
     &compressorShapeMenu, NULL, &bassMenu, &bassFreqMenu},
    {NULL, NULL, NULL, NULL, NULL, NULL, &trebleMenu, &trebleFreqMenu},
    {NULL, NULL, NULL, &audioClipModFXTypeMenu, &modFXOffsetMenu, &modFXFeedbackMenu, &audioClipModFXDepthMenu,
     &audioClipModFXRateMenu},
    {NULL, NULL, NULL, &audioClipReverbSendAmountMenu, &reverbPanMenu, &reverbWidthMenu, &reverbDampeningMenu,
     &reverbRoomSizeMenu},
    {&audioClipDelayRateMenu, &delaySyncMenu, &delayAnalogMenu, &audioClipDelayFeedbackMenu, &delayPingPongMenu, NULL,
     NULL, NULL}};

#if HAVE_OLED
void init_menu_titles() {
	triggerClockInMenu.basicTitle = "T. clock input";
	triggerClockOutMenu.basicTitle = "T. clock out";
	triggerInPPQNMenu.basicTitle = "Input PPQN";
	triggerOutPPQNMenu.basicTitle = "Output PPQN";

	midiClockMenu.basicTitle = "MIDI clock";
	midiClockInStatusMenu.basicTitle = "MIDI clock in";
	midiClockOutStatusMenu.basicTitle = "MIDI clock out";
	tempoMagnitudeMatchingMenu.basicTitle = "Tempo m. match";
	midiCommandsMenu.basicTitle = "MIDI commands";
	midi::devicesMenu.basicTitle = "MIDI devices";

	defaultTempoMenu.basicTitle = "Default tempo";
	defaultSwingMenu.basicTitle = "Default swing";
	defaultKeyMenu.basicTitle = "Default key";
	defaultScaleMenu.basicTitle = "Default scale";
	defaultVelocityMenu.basicTitle = "Default veloc.";
	defaultMagnitudeMenu.basicTitle = "Default resol.";
	defaultBendRangeMenu.basicTitle = "Default bend r";

	shortcutsVersionMenu.basicTitle = "Shortcuts ver.";
	keyboardLayoutMenu.basicTitle = "Key layout";

	recordCountInMenu.basicTitle = "Rec count-in";
	monitorModeMenu.basicTitle = "Monitoring";
	runtimeFeatureSettingsMenu.basicTitle = "Community fts.";
	firmwareVersionMenu.basicTitle = "Firmware ver.";

	reverbAmountMenu.basicTitle = "Reverb amount";
	reverbWidthMenu.basicTitle = "Reverb width";
	reverbPanMenu.basicTitle = "Reverb pan";
	reverbCompressorMenu.basicTitle = "Reverb sidech.";

	sidechainSendMenu.basicTitle = "Send to sidech";
	sidechainSyncMenu.basicTitle = "Sidechain sync";
	compressorAttackMenu.basicTitle = "Sidech. attack";
	compressorReleaseMenu.basicTitle = "Sidech release";
	compressorShapeMenu.basicTitle = "Sidech. shape";
	reverbCompressorShapeMenu.basicTitle = "Sidech. shape";

	modFXTypeMenu.basicTitle = "MOD FX type";
	modFXRateMenu.basicTitle = "MOD FX rate";
	modFXFeedbackMenu.basicTitle = "MODFX feedback";
	modFXDepthMenu.basicTitle = "MOD FX depth";
	modFXOffsetMenu.basicTitle = "MOD FX offset";

	delayFeedbackMenu.basicTitle = "Delay amount";
	delayRateMenu.basicTitle = "Delay rate";
	delayPingPongMenu.basicTitle = "Delay pingpong";
	delayAnalogMenu.basicTitle = "Delay type";
	delaySyncMenu.basicTitle = "Delay sync";

	lfo1TypeMenu.basicTitle = "LFO1 type";
	lfo1RateMenu.basicTitle = "LFO1 rate";
	lfo1SyncMenu.basicTitle = "LFO1 sync";

	lfo2TypeMenu.basicTitle = "LFO2 type";
	lfo2RateMenu.basicTitle = "LFO2 rate";

	oscTypeMenu.basicTitle = oscTypeTitle;
	sourceVolumeMenu.basicTitle = oscLevelTitle;
	sourceWaveIndexMenu.basicTitle = waveIndexTitle;
	sourceFeedbackMenu.basicTitle = carrierFeedback;
	sampleReverseMenu.basicTitle = sampleReverseTitle;
	sampleRepeatMenu.basicTitle = sampleModeTitle;
	sourceTransposeMenu.basicTitle = oscTransposeTitle;
	timeStretchMenu.basicTitle = sampleSpeedTitle;
	interpolationMenu.basicTitle = sampleInterpolationTitle;
	pulseWidthMenu.basicTitle = pulseWidthTitle;
	oscPhaseMenu.basicTitle = retriggerPhaseTitle;

	modulatorTransposeMenu.basicTitle = modulatorTransposeTitle;
	modulatorDestMenu.basicTitle = "FM Mod2 dest.";
	modulatorVolume.basicTitle = modulatorLevelTitle;
	modulatorFeedbackMenu.basicTitle = modulatorFeedbackTitle;
	modulatorPhaseMenu.basicTitle = modulatorRetriggerPhaseTitle;

	lpfFreqMenu.basicTitle = "LPF frequency";
	lpfResMenu.basicTitle = "LPF resonance";
	lpfModeMenu.basicTitle = "LPF mode";

	hpfFreqMenu.basicTitle = "HPF frequency";
	hpfResMenu.basicTitle = "HPF resonance";

	envAttackMenu.basicTitle = attackTitle;
	envDecayMenu.basicTitle = decayTitle;
	envSustainMenu.basicTitle = sustainTitle;
	envReleaseMenu.basicTitle = releaseTitle;

	arpModeMenu.basicTitle = "Arp. mode";
	arpSyncMenu.basicTitle = "Arp. sync";
	arpOctavesMenu.basicTitle = "Arp. octaves";
	arpGateMenu.basicTitle = "Arp. gate";
	arpGateMenuMIDIOrCV.basicTitle = "Arp. gate";
	arpRateMenu.basicTitle = "Arp. rate";
	arpRateMenuMIDIOrCV.basicTitle = "Arp. rate";

	masterTransposeMenu.basicTitle = "Master tran.";
	compressorMenu.basicTitle = "Sidechain comp";
	volumeMenu.basicTitle = "Master level";

	midiBankMenu.basicTitle = "MIDI bank";
	midiSubMenu.basicTitle = "MIDI sub-bank";
	midiPGMMenu.basicTitle = "MIDI PGM numb.";

	audioClipReverbSendAmountMenu.basicTitle = "Reverb amount";

	audioClipDelayFeedbackMenu.basicTitle = "Delay amount";
	audioClipDelayRateMenu.basicTitle = "Delay rate";

	audioClipModFXTypeMenu.basicTitle = "MOD FX type";
	audioClipModFXRateMenu.basicTitle = "MOD FX rate";
	audioClipModFXDepthMenu.basicTitle = "MOD FX depth";

	audioClipLPFFreqMenu.basicTitle = "LPF frequency";
	audioClipLPFResMenu.basicTitle = "LPF resonance";

	audioClipHPFFreqMenu.basicTitle = "HPF frequency";
	audioClipHPFResMenu.basicTitle = "HPF resonance";

	cvVoltsMenu.basicTitle = cvVoltsTitle;
	cvTransposeMenu.basicTitle = cvTransposeTitle;
}
#endif
