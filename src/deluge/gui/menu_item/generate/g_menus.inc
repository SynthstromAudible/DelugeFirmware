// clang-format off
audio_compressor::CompParam threshold{STRING_FOR_THRESHOLD, STRING_FOR_THRESHOLD, params::UNPATCHED_COMPRESSOR_THRESHOLD};
audio_compressor::Ratio compRatio{STRING_FOR_RATIO, STRING_FOR_RATIO};
audio_compressor::Attack compAttack{STRING_FOR_ATTACK, STRING_FOR_ATTACK};
audio_compressor::Release compRelease{STRING_FOR_RELEASE, STRING_FOR_RELEASE};
audio_compressor::SideHPF compHPF{STRING_FOR_HPF, STRING_FOR_HPF};
audio_compressor::Blend compBlend{STRING_FOR_BLEND, STRING_FOR_BLEND};
std::array<MenuItem*, 6> child0 = {
    &threshold,
    &compRatio,
    &compAttack,
    &compRelease,
    &compHPF,
    &compBlend,
};
Submenu audioCompMenu{STRING_FOR_COMMUNITY_FEATURE_MASTER_COMPRESSOR, child0};
unison::Count numUnisonMenu{STRING_FOR_NUMBER};
unison::Detune unisonDetuneMenu{STRING_FOR_DETUNE};
unison::StereoSpread unison::stereoSpreadMenu{STRING_FOR_SPREAD};
std::array<MenuItem*, 3> child1 = {
    &numUnisonMenu,
    &unisonDetuneMenu,
    &unison::stereoSpreadMenu,
};
HorizontalMenu unisonMenu{STRING_FOR_UNISON, child1};
osc::Type oscTypeMenu{STRING_FOR_TYPE, STRING_FOR_OSC_TYPE_MENU_TITLE};
osc::source::WaveIndex sourceWaveIndexMenu{STRING_FOR_WAVE_INDEX, STRING_FOR_OSC_WAVE_IND_MENU_TITLE, params::LOCAL_OSC_A_WAVE_INDEX};
osc::source::Feedback sourceFeedbackMenu{STRING_FOR_FEEDBACK, STRING_FOR_CARRIER_FEED_MENU_TITLE, params::LOCAL_CARRIER_0_FEEDBACK};
sample::Transpose sourceTransposeMenu{STRING_FOR_PITCH, STRING_FOR_OSC_TRANSPOSE_MENU_TITLE, params::LOCAL_OSC_A_PITCH_ADJUST};
osc::PulseWidth pulseWidthMenu{STRING_FOR_PW, STRING_FOR_OSC_P_WIDTH_MENU_TITLE, params::LOCAL_OSC_A_PHASE_WIDTH};
osc::RetriggerPhase oscPhaseMenu{STRING_FOR_PHASE, STRING_FOR_OSC_R_PHASE_MENU_TITLE, false};
osc::Sync oscSyncMenu{STRING_FOR_SYNC};
std::array<MenuItem*, 7> child2 = {
    &oscTypeMenu,
    &sourceWaveIndexMenu,
    &sourceFeedbackMenu,
    &sourceTransposeMenu,
    &pulseWidthMenu,
    &oscPhaseMenu,
    &oscSyncMenu,
};
submenu::ActualSource osc0Menu{STRING_FOR_OSCILLATOR_1, child2, 0};
submenu::ActualSource osc1Menu{STRING_FOR_OSCILLATOR_2, child2, 1};
sample::Repeat sampleRepeatMenu{STRING_FOR_REPEAT_MODE, STRING_FOR_SAMP_REPEAT_MENU_TITLE};
sample::Reverse sampleReverseMenu{STRING_FOR_REVERSE, STRING_FOR_SAMP_REVERSE_MENU_TITLE};
sample::TimeStretch timeStretchMenu{STRING_FOR_SPEED, STRING_FOR_SAMP_SPEED_MENU_TITLE};
sample::PitchSpeed samplePitchSpeedMenu{STRING_FOR_PITCH_SPEED};
sample::Interpolation interpolationMenu{STRING_FOR_INTERPOLATION, STRING_FOR_SAMP_INTERP_MENU_TITLE};
FileSelector deluge::gui::menu_item::fileSelectorMenu{STRING_FOR_FILE_BROWSER};
osc::AudioRecorder audioRecorderMenu{STRING_FOR_RECORD_AUDIO};
sample::Start sampleStartMenu{STRING_FOR_START_POINT};
sample::End sampleEndMenu{STRING_FOR_END_POINT};
std::array<MenuItem*, 9> child3 = {
    &sampleRepeatMenu,
    &sampleReverseMenu,
    &timeStretchMenu,
    &samplePitchSpeedMenu,
    &interpolationMenu,
    &deluge::gui::menu_item::fileSelectorMenu,
    &audioRecorderMenu,
    &sampleStartMenu,
    &sampleEndMenu,
};
submenu::ActualSource sample0Menu{STRING_FOR_SAMPLE_1, child3, 0};
submenu::ActualSource sample1Menu{STRING_FOR_SAMPLE_2, child3, 1};
// clang-format on
