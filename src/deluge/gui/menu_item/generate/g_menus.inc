// clang-format off
filter::LPFFreq lpfFreqMenu{STRING_FOR_FREQUENCY, STRING_FOR_LPF_FREQUENCY, params::LOCAL_LPF_FREQ};
patched_param::IntegerNonFM lpfResMenu{STRING_FOR_RESONANCE, STRING_FOR_LPF_RESONANCE, params::LOCAL_LPF_RESONANCE};
filter::LPFMode lpfModeMenu{STRING_FOR_MODE, STRING_FOR_LPF_MODE};
filter::FilterMorph lpfMorphMenu{STRING_FOR_MORPH, params::LOCAL_LPF_MORPH, false};
std::array<MenuItem*, 4> child0 = {
    &lpfFreqMenu,
    &lpfResMenu,
    &lpfModeMenu,
    &lpfMorphMenu,
};
submenu::Filter lpfMenu{STRING_FOR_LPF, child0};
filter::HPFFreq hpfFreqMenu{STRING_FOR_FREQUENCY, STRING_FOR_HPF_FREQUENCY, params::LOCAL_HPF_FREQ};
patched_param::IntegerNonFM hpfResMenu{STRING_FOR_RESONANCE, STRING_FOR_HPF_RESONANCE, params::LOCAL_HPF_RESONANCE};
filter::HPFMode hpfModeMenu{STRING_FOR_MODE, STRING_FOR_HPF_MODE};
filter::FilterMorph hpfMorphMenu{STRING_FOR_MORPH, params::LOCAL_HPF_MORPH, true};
std::array<MenuItem*, 4> child1 = {
    &hpfFreqMenu,
    &hpfResMenu,
    &hpfModeMenu,
    &hpfMorphMenu,
};
submenu::Filter hpfMenu{STRING_FOR_HPF, child1};
FilterRouting filterRoutingMenu{STRING_FOR_FILTER_ROUTE};
std::array<MenuItem*, 3> child2 = {
    &lpfMenu,
    &hpfMenu,
    &filterRoutingMenu,
};
submenu::Filter soundFiltersMenu{STRING_FOR_FILTERS, child2};
audio_compressor::CompParam threshold{STRING_FOR_THRESHOLD, STRING_FOR_THRESHOLD, params::UNPATCHED_COMPRESSOR_THRESHOLD};
audio_compressor::Ratio compRatio{STRING_FOR_RATIO, STRING_FOR_RATIO};
audio_compressor::Attack compAttack{STRING_FOR_ATTACK, STRING_FOR_ATTACK};
audio_compressor::Release compRelease{STRING_FOR_RELEASE, STRING_FOR_RELEASE};
audio_compressor::SideHPF compHPF{STRING_FOR_HPF, STRING_FOR_HPF};
std::array<MenuItem*, 5> child3 = {
    &threshold,
    &compRatio,
    &compAttack,
    &compRelease,
    &compHPF,
};
Submenu audioCompMenu{STRING_FOR_COMMUNITY_FEATURE_MASTER_COMPRESSOR, child3};
envelope::Segment envAttackMenu{STRING_FOR_ATTACK, STRING_FOR_ENV_ATTACK_MENU_TITLE, params::LOCAL_ENV_0_ATTACK};
envelope::Segment envDecayMenu{STRING_FOR_DECAY, STRING_FOR_ENV_DECAY_MENU_TITLE, params::LOCAL_ENV_0_DECAY};
envelope::Segment envSustainMenu{STRING_FOR_SUSTAIN, STRING_FOR_ENV_SUSTAIN_MENU_TITLE, params::LOCAL_ENV_0_SUSTAIN};
envelope::Segment envReleaseMenu{STRING_FOR_RELEASE, STRING_FOR_ENV_RELEASE_MENU_TITLE, params::LOCAL_ENV_0_RELEASE};
std::array<MenuItem*, 4> child4 = {
    &envAttackMenu,
    &envDecayMenu,
    &envSustainMenu,
    &envReleaseMenu,
};
submenu::Envelope env0Menu{STRING_FOR_ENVELOPE_1, child4, 0};
submenu::Envelope env1Menu{STRING_FOR_ENVELOPE_2, child4, 1};
osc::Type oscTypeMenu{STRING_FOR_TYPE, STRING_FOR_OSC_TYPE_MENU_TITLE};
osc::source::Volume sourceVolumeMenu{STRING_FOR_VOLUME_LEVEL, STRING_FOR_OSC_LEVEL_MENU_TITLE, params::LOCAL_OSC_A_VOLUME};
osc::source::WaveIndex sourceWaveIndexMenu{STRING_FOR_WAVE_INDEX, STRING_FOR_OSC_WAVE_IND_MENU_TITLE, params::LOCAL_OSC_A_WAVE_INDEX};
osc::source::Feedback sourceFeedbackMenu{STRING_FOR_FEEDBACK, STRING_FOR_CARRIER_FEED_MENU_TITLE, params::LOCAL_CARRIER_0_FEEDBACK};
FileSelector deluge::gui::menu_item::fileSelectorMenu{STRING_FOR_FILE_BROWSER};
osc::AudioRecorder audioRecorderMenu{STRING_FOR_RECORD_AUDIO};
sample::Reverse sampleReverseMenu{STRING_FOR_REVERSE, STRING_FOR_SAMP_REVERSE_MENU_TITLE};
sample::Repeat sampleRepeatMenu{STRING_FOR_REPEAT_MODE, STRING_FOR_SAMP_REPEAT_MENU_TITLE};
sample::Start sampleStartMenu{STRING_FOR_START_POINT};
sample::End sampleEndMenu{STRING_FOR_END_POINT};
sample::Transpose sourceTransposeMenu{STRING_FOR_TRANSPOSE, STRING_FOR_OSC_TRANSPOSE_MENU_TITLE, params::LOCAL_OSC_A_PITCH_ADJUST};
sample::PitchSpeed samplePitchSpeedMenu{STRING_FOR_PITCH_SPEED};
sample::TimeStretch timeStretchMenu{STRING_FOR_SPEED, STRING_FOR_SAMP_SPEED_MENU_TITLE};
sample::Interpolation interpolationMenu{STRING_FOR_INTERPOLATION, STRING_FOR_SAMP_INTERP_MENU_TITLE};
osc::PulseWidth pulseWidthMenu{STRING_FOR_PULSE_WIDTH, STRING_FOR_OSC_P_WIDTH_MENU_TITLE, params::LOCAL_OSC_A_PHASE_WIDTH};
osc::Sync oscSyncMenu{STRING_FOR_OSCILLATOR_SYNC};
osc::RetriggerPhase oscPhaseMenu{STRING_FOR_RETRIGGER_PHASE, STRING_FOR_OSC_R_PHASE_MENU_TITLE, false};
std::array<MenuItem*, 17> child5 = {
    &oscTypeMenu,
    &sourceVolumeMenu,
    &sourceWaveIndexMenu,
    &sourceFeedbackMenu,
    &deluge::gui::menu_item::fileSelectorMenu,
    &audioRecorderMenu,
    &sampleReverseMenu,
    &sampleRepeatMenu,
    &sampleStartMenu,
    &sampleEndMenu,
    &sourceTransposeMenu,
    &samplePitchSpeedMenu,
    &timeStretchMenu,
    &interpolationMenu,
    &pulseWidthMenu,
    &oscSyncMenu,
    &oscPhaseMenu,
};
submenu::ActualSource source0Menu{STRING_FOR_OSCILLATOR_1, child5, 0};
submenu::ActualSource source1Menu{STRING_FOR_OSCILLATOR_2, child5, 1};
// clang-format on
