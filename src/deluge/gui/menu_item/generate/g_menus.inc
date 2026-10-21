// clang-format off
PLACE_SDRAM_BSS filter::FilterModeSelection lpfModeMenu{STRING_FOR_MODE, STRING_FOR_LPF_MODE, filter::FilterSlot::LPF};
PLACE_SDRAM_BSS filter::FilterParam lpfFreqMenu{STRING_FOR_FREQUENCY, STRING_FOR_LPF_FREQUENCY, params::LOCAL_LPF_FREQ, filter::FilterSlot::LPF, filter::FilterParamType::FREQUENCY};
PLACE_SDRAM_BSS filter::FilterParam lpfResMenu{STRING_FOR_RESONANCE, STRING_FOR_LPF_RESONANCE, params::LOCAL_LPF_RESONANCE, filter::FilterSlot::LPF, filter::FilterParamType::RESONANCE};
PLACE_SDRAM_BSS filter::FilterParam lpfMorphMenu{STRING_FOR_LPF_MORPH, params::LOCAL_LPF_MORPH, filter::FilterSlot::LPF, filter::FilterParamType::MORPH};
PLACE_SDRAM_DATA std::array<MenuItem*, 4> child0 = {
    &lpfModeMenu,
    &lpfFreqMenu,
    &lpfResMenu,
    &lpfMorphMenu,
};
PLACE_SDRAM_BSS HorizontalMenu lpfMenu{STRING_FOR_LPF, child0};
PLACE_SDRAM_BSS filter::FilterModeSelection hpfModeMenu{STRING_FOR_MODE, STRING_FOR_HPF_MODE, filter::FilterSlot::HPF};
PLACE_SDRAM_BSS filter::FilterParam hpfFreqMenu{STRING_FOR_FREQUENCY, STRING_FOR_HPF_FREQUENCY, params::LOCAL_HPF_FREQ, filter::FilterSlot::HPF, filter::FilterParamType::FREQUENCY};
PLACE_SDRAM_BSS filter::FilterParam hpfResMenu{STRING_FOR_RESONANCE, STRING_FOR_HPF_RESONANCE, params::LOCAL_HPF_RESONANCE, filter::FilterSlot::HPF, filter::FilterParamType::RESONANCE};
PLACE_SDRAM_BSS filter::FilterParam hpfMorphMenu{STRING_FOR_HPF_MORPH, params::LOCAL_HPF_MORPH, filter::FilterSlot::HPF, filter::FilterParamType::MORPH};
PLACE_SDRAM_DATA std::array<MenuItem*, 4> child1 = {
    &hpfModeMenu,
    &hpfFreqMenu,
    &hpfResMenu,
    &hpfMorphMenu,
};
PLACE_SDRAM_BSS HorizontalMenu hpfMenu{STRING_FOR_HPF, child1};
PLACE_SDRAM_BSS FilterRouting filterRoutingMenu{STRING_FOR_FILTER_ROUTE};
PLACE_SDRAM_DATA std::array<MenuItem*, 3> child2 = {
    &lpfMenu,
    &hpfMenu,
    &filterRoutingMenu,
};
PLACE_SDRAM_BSS Submenu soundFiltersMenu{STRING_FOR_FILTERS, child2};
PLACE_SDRAM_BSS audio_compressor::CompParam threshold{STRING_FOR_THRESHOLD, STRING_FOR_COMP_THRESHOLD_MENU_TITLE, params::UNPATCHED_COMPRESSOR_THRESHOLD, BAR};
PLACE_SDRAM_BSS audio_compressor::Ratio compRatio{STRING_FOR_RATIO, STRING_FOR_RATIO};
PLACE_SDRAM_BSS audio_compressor::Blend compBlend{STRING_FOR_BLEND, STRING_FOR_BLEND};
PLACE_SDRAM_BSS audio_compressor::Attack compAttack{STRING_FOR_ATTACK, STRING_FOR_ATTACK};
PLACE_SDRAM_BSS audio_compressor::Release compRelease{STRING_FOR_RELEASE, STRING_FOR_RELEASE};
PLACE_SDRAM_BSS audio_compressor::SideHPF compHPF{STRING_FOR_HPF, STRING_FOR_HPF};
PLACE_SDRAM_DATA std::array<MenuItem*, 6> child3 = {
    &threshold,
    &compRatio,
    &compBlend,
    &compAttack,
    &compRelease,
    &compHPF,
};
PLACE_SDRAM_BSS HorizontalMenu audioCompMenu{STRING_FOR_COMMUNITY_FEATURE_MASTER_COMPRESSOR, child3};
PLACE_SDRAM_BSS unison::CountToStereoSpread numUnisonMenu{STRING_FOR_UNISON_NUMBER, STRING_FOR_UNISON_NUMBER_MENU_TITLE};
PLACE_SDRAM_BSS unison::Detune unisonDetuneMenu{STRING_FOR_UNISON_DETUNE, STRING_FOR_UNISON_DETUNE_MENU_TITLE};
PLACE_SDRAM_BSS unison::StereoSpread unison::stereoSpreadMenu{STRING_FOR_UNISON_STEREO_SPREAD, STRING_FOR_UNISON_STEREO_SPREAD_MENU_TITLE};
PLACE_SDRAM_DATA std::array<MenuItem*, 3> child4 = {
    &numUnisonMenu,
    &unisonDetuneMenu,
    &unison::stereoSpreadMenu,
};
PLACE_SDRAM_BSS HorizontalMenu unisonMenu{STRING_FOR_UNISON, child4, HorizontalMenu::Layout::FIXED};
PLACE_SDRAM_BSS osc::AudioRecorder sample0RecorderMenu{STRING_FOR_RECORD_TO_SAMPLE, 0};
PLACE_SDRAM_BSS osc::AudioRecorder sample1RecorderMenu{STRING_FOR_RECORD_TO_SAMPLE, 1};
PLACE_SDRAM_DATA std::array<MenuItem*, 2> child5 = {
    &sample0RecorderMenu,
    &sample1RecorderMenu,
};
PLACE_SDRAM_BSS HorizontalMenu recorderMenu{STRING_FOR_SAMPLE_RECORDER, child5};
PLACE_SDRAM_BSS osc::Type osc0TypeMenu{STRING_FOR_TYPE, STRING_FOR_OSC_TYPE_MENU_TITLE, 0};
PLACE_SDRAM_BSS FileSelector deluge::gui::menu_item::file0SelectorMenu{STRING_FOR_FILE_BROWSER, 0};
PLACE_SDRAM_BSS sample::Transpose source0TransposeMenu{STRING_FOR_TRANSPOSE, STRING_FOR_OSC_TRANSPOSE_MENU_TITLE, params::LOCAL_OSC_A_PITCH_ADJUST, 0};
PLACE_SDRAM_BSS osc::source::WaveIndex source0WaveIndexMenu{STRING_FOR_WAVE_INDEX, STRING_FOR_OSC_WAVE_IND_MENU_TITLE, params::LOCAL_OSC_A_WAVE_INDEX, 0};
PLACE_SDRAM_BSS osc::PulseWidth osc0PulseWidthMenu{STRING_FOR_PULSE_WIDTH, STRING_FOR_OSC_P_WIDTH_MENU_TITLE, params::LOCAL_OSC_A_PHASE_WIDTH, 0};
PLACE_SDRAM_BSS osc::source::Feedback source0FeedbackMenu{STRING_FOR_FEEDBACK, STRING_FOR_CARRIER_FEED_MENU_TITLE, params::LOCAL_CARRIER_0_FEEDBACK, 0};
PLACE_SDRAM_BSS osc::RetriggerPhase osc0PhaseMenu{STRING_FOR_RETRIGGER_PHASE, STRING_FOR_OSC_R_PHASE_MENU_TITLE, 0, false};
PLACE_SDRAM_BSS sample::Reverse sample0ReverseMenu{STRING_FOR_REVERSE, STRING_FOR_SAMP_REVERSE_MENU_TITLE, 0};
PLACE_SDRAM_BSS sample::Repeat sample0RepeatMenu{STRING_FOR_REPEAT_MODE, STRING_FOR_SAMP_REPEAT_MENU_TITLE, 0};
PLACE_SDRAM_BSS sample::TimeStretch sample0TimeStretchMenu{STRING_FOR_SPEED, STRING_FOR_SAMP_SPEED_MENU_TITLE, 0};
PLACE_SDRAM_BSS sample::PitchSpeed sample0PitchSpeedMenu{STRING_FOR_PITCH_SPEED, 0};
PLACE_SDRAM_BSS sample::Interpolation sample0InterpolationMenu{STRING_FOR_INTERPOLATION, STRING_FOR_SAMP_INTERP_MENU_TITLE, 0};
PLACE_SDRAM_BSS sample::Start sample0StartMenu{STRING_FOR_START_POINT, 0};
PLACE_SDRAM_BSS sample::End sample0EndMenu{STRING_FOR_END_POINT, 0};
PLACE_SDRAM_DATA std::array<MenuItem*, 14> child6 = {
    &osc0TypeMenu,
    &deluge::gui::menu_item::file0SelectorMenu,
    &source0TransposeMenu,
    &source0WaveIndexMenu,
    &osc0PulseWidthMenu,
    &source0FeedbackMenu,
    &osc0PhaseMenu,
    &sample0ReverseMenu,
    &sample0RepeatMenu,
    &sample0TimeStretchMenu,
    &sample0PitchSpeedMenu,
    &sample0InterpolationMenu,
    &sample0StartMenu,
    &sample0EndMenu,
};
PLACE_SDRAM_BSS submenu::ActualSource source0Menu{STRING_FOR_OSCILLATOR_1, child6, 0};
PLACE_SDRAM_BSS osc::Type osc1TypeMenu{STRING_FOR_TYPE, STRING_FOR_OSC_TYPE_MENU_TITLE, 1};
PLACE_SDRAM_BSS FileSelector deluge::gui::menu_item::file1SelectorMenu{STRING_FOR_FILE_BROWSER, 1};
PLACE_SDRAM_BSS sample::Transpose source1TransposeMenu{STRING_FOR_TRANSPOSE, STRING_FOR_OSC_TRANSPOSE_MENU_TITLE, params::LOCAL_OSC_A_PITCH_ADJUST, 1};
PLACE_SDRAM_BSS osc::source::WaveIndex source1WaveIndexMenu{STRING_FOR_WAVE_INDEX, STRING_FOR_OSC_WAVE_IND_MENU_TITLE, params::LOCAL_OSC_A_WAVE_INDEX, 1};
PLACE_SDRAM_BSS osc::PulseWidth osc1PulseWidthMenu{STRING_FOR_PULSE_WIDTH, STRING_FOR_OSC_P_WIDTH_MENU_TITLE, params::LOCAL_OSC_A_PHASE_WIDTH, 1};
PLACE_SDRAM_BSS osc::source::Feedback source1FeedbackMenu{STRING_FOR_FEEDBACK, STRING_FOR_CARRIER_FEED_MENU_TITLE, params::LOCAL_CARRIER_0_FEEDBACK, 1};
PLACE_SDRAM_BSS osc::RetriggerPhase osc1PhaseMenu{STRING_FOR_RETRIGGER_PHASE, STRING_FOR_OSC_R_PHASE_MENU_TITLE, 1, false};
PLACE_SDRAM_BSS sample::Reverse sample1ReverseMenu{STRING_FOR_REVERSE, STRING_FOR_SAMP_REVERSE_MENU_TITLE, 1};
PLACE_SDRAM_BSS sample::Repeat sample1RepeatMenu{STRING_FOR_REPEAT_MODE, STRING_FOR_SAMP_REPEAT_MENU_TITLE, 1};
PLACE_SDRAM_BSS sample::TimeStretch sample1TimeStretchMenu{STRING_FOR_SPEED, STRING_FOR_SAMP_SPEED_MENU_TITLE, 1};
PLACE_SDRAM_BSS sample::PitchSpeed sample1PitchSpeedMenu{STRING_FOR_PITCH_SPEED, 1};
PLACE_SDRAM_BSS sample::Interpolation sample1InterpolationMenu{STRING_FOR_INTERPOLATION, STRING_FOR_SAMP_INTERP_MENU_TITLE, 1};
PLACE_SDRAM_BSS sample::Start sample1StartMenu{STRING_FOR_START_POINT, 1};
PLACE_SDRAM_BSS sample::End sample1EndMenu{STRING_FOR_END_POINT, 1};
PLACE_SDRAM_DATA std::array<MenuItem*, 14> child7 = {
    &osc1TypeMenu,
    &deluge::gui::menu_item::file1SelectorMenu,
    &source1TransposeMenu,
    &source1WaveIndexMenu,
    &osc1PulseWidthMenu,
    &source1FeedbackMenu,
    &osc1PhaseMenu,
    &sample1ReverseMenu,
    &sample1RepeatMenu,
    &sample1TimeStretchMenu,
    &sample1PitchSpeedMenu,
    &sample1InterpolationMenu,
    &sample1StartMenu,
    &sample1EndMenu,
};
PLACE_SDRAM_BSS submenu::ActualSource source1Menu{STRING_FOR_OSCILLATOR_2, child7, 1};
PLACE_SDRAM_BSS modulator::Transpose modulator0TransposeMenu{STRING_FOR_TRANSPOSE, STRING_FOR_FM_MOD_TRAN_MENU_TITLE, params::LOCAL_MODULATOR_0_PITCH_ADJUST, 0};
PLACE_SDRAM_BSS source::patched_param::ModulatorFeedback modulator0FeedbackMenu{STRING_FOR_FEEDBACK, STRING_FOR_FM_MOD_FBACK_MENU_TITLE, params::LOCAL_MODULATOR_0_FEEDBACK, 0};
PLACE_SDRAM_BSS osc::RetriggerPhase modulator0PhaseMenu{STRING_FOR_RETRIGGER_PHASE, STRING_FOR_FM_MOD_RETRIG_MENU_TITLE, 0, true};
PLACE_SDRAM_DATA std::array<MenuItem*, 3> child8 = {
    &modulator0TransposeMenu,
    &modulator0FeedbackMenu,
    &modulator0PhaseMenu,
};
PLACE_SDRAM_BSS submenu::Modulator modulator0Menu{STRING_FOR_FM_MODULATOR_1, child8};
PLACE_SDRAM_BSS modulator::Transpose modulator1TransposeMenu{STRING_FOR_TRANSPOSE, STRING_FOR_FM_MOD_TRAN_MENU_TITLE, params::LOCAL_MODULATOR_0_PITCH_ADJUST, 1};
PLACE_SDRAM_BSS source::patched_param::ModulatorFeedback modulator1FeedbackMenu{STRING_FOR_FEEDBACK, STRING_FOR_FM_MOD_FBACK_MENU_TITLE, params::LOCAL_MODULATOR_0_FEEDBACK, 1};
PLACE_SDRAM_BSS osc::RetriggerPhase modulator1PhaseMenu{STRING_FOR_RETRIGGER_PHASE, STRING_FOR_FM_MOD_RETRIG_MENU_TITLE, 1, true};
PLACE_SDRAM_BSS modulator::Destination modulatorDestMenu{STRING_FOR_DESTINATION, STRING_FOR_FM_MOD2_DEST_MENU_TITLE};
PLACE_SDRAM_DATA std::array<MenuItem*, 4> child9 = {
    &modulator1TransposeMenu,
    &modulator1FeedbackMenu,
    &modulator1PhaseMenu,
    &modulatorDestMenu,
};
PLACE_SDRAM_BSS submenu::Modulator modulator1Menu{STRING_FOR_FM_MODULATOR_2, child9};
PLACE_SDRAM_BSS osc::source::Volume source0VolumeMenu{STRING_FOR_OSC_LEVEL_MENU_TITLE, params::LOCAL_OSC_A_VOLUME, 0};
PLACE_SDRAM_BSS osc::source::Volume source1VolumeMenu{STRING_FOR_OSC_LEVEL_MENU_TITLE, params::LOCAL_OSC_A_VOLUME, 1};
PLACE_SDRAM_BSS source::patched_param::ModulatorLevel modulator0Volume{STRING_FOR_FM_MOD_LEVEL_MENU_TITLE, params::LOCAL_MODULATOR_0_VOLUME, 0};
PLACE_SDRAM_BSS source::patched_param::ModulatorLevel modulator1Volume{STRING_FOR_FM_MOD_LEVEL_MENU_TITLE, params::LOCAL_MODULATOR_0_VOLUME, 1};
PLACE_SDRAM_BSS patched_param::IntegerNonFM noiseMenu{STRING_FOR_NOISE_LEVEL, params::LOCAL_NOISE_VOLUME};
PLACE_SDRAM_BSS osc::Sync oscSyncMenu{STRING_FOR_OSCILLATOR_SYNC};
PLACE_SDRAM_DATA std::array<MenuItem*, 6> child10 = {
    &source0VolumeMenu,
    &source1VolumeMenu,
    &modulator0Volume,
    &modulator1Volume,
    &noiseMenu,
    &oscSyncMenu,
};
PLACE_SDRAM_BSS HorizontalMenu oscMixerMenu{STRING_FOR_OSCILLATOR_MIXER, child10};
PLACE_SDRAM_BSS envelope::Segment env1AttackMenu{STRING_FOR_ATTACK, STRING_FOR_ENV_ATTACK_MENU_TITLE, params::LOCAL_ENV_0_ATTACK, 0};
PLACE_SDRAM_BSS envelope::Segment env1DecayMenu{STRING_FOR_DECAY, STRING_FOR_ENV_DECAY_MENU_TITLE, params::LOCAL_ENV_0_DECAY, 0};
PLACE_SDRAM_BSS envelope::Segment env1SustainMenu{STRING_FOR_SUSTAIN, STRING_FOR_ENV_SUSTAIN_MENU_TITLE, params::LOCAL_ENV_0_SUSTAIN, 0};
PLACE_SDRAM_BSS envelope::Segment env1ReleaseMenu{STRING_FOR_RELEASE, STRING_FOR_ENV_RELEASE_MENU_TITLE, params::LOCAL_ENV_0_RELEASE, 0};
PLACE_SDRAM_DATA std::array<MenuItem*, 4> child11 = {
    &env1AttackMenu,
    &env1DecayMenu,
    &env1SustainMenu,
    &env1ReleaseMenu,
};
PLACE_SDRAM_BSS envelope::EnvelopeMenu env1Menu{STRING_FOR_ENVELOPE_MENU_TITLE, child11, 0};
PLACE_SDRAM_BSS envelope::Segment env2AttackMenu{STRING_FOR_ATTACK, STRING_FOR_ENV_ATTACK_MENU_TITLE, params::LOCAL_ENV_0_ATTACK, 1};
PLACE_SDRAM_BSS envelope::Segment env2DecayMenu{STRING_FOR_DECAY, STRING_FOR_ENV_DECAY_MENU_TITLE, params::LOCAL_ENV_0_DECAY, 1};
PLACE_SDRAM_BSS envelope::Segment env2SustainMenu{STRING_FOR_SUSTAIN, STRING_FOR_ENV_SUSTAIN_MENU_TITLE, params::LOCAL_ENV_0_SUSTAIN, 1};
PLACE_SDRAM_BSS envelope::Segment env2ReleaseMenu{STRING_FOR_RELEASE, STRING_FOR_ENV_RELEASE_MENU_TITLE, params::LOCAL_ENV_0_RELEASE, 1};
PLACE_SDRAM_DATA std::array<MenuItem*, 4> child12 = {
    &env2AttackMenu,
    &env2DecayMenu,
    &env2SustainMenu,
    &env2ReleaseMenu,
};
PLACE_SDRAM_BSS envelope::EnvelopeMenu env2Menu{STRING_FOR_ENVELOPE_MENU_TITLE, child12, 1};
PLACE_SDRAM_BSS envelope::Segment env3AttackMenu{STRING_FOR_ATTACK, STRING_FOR_ENV_ATTACK_MENU_TITLE, params::LOCAL_ENV_0_ATTACK, 2};
PLACE_SDRAM_BSS envelope::Segment env3DecayMenu{STRING_FOR_DECAY, STRING_FOR_ENV_DECAY_MENU_TITLE, params::LOCAL_ENV_0_DECAY, 2};
PLACE_SDRAM_BSS envelope::Segment env3SustainMenu{STRING_FOR_SUSTAIN, STRING_FOR_ENV_SUSTAIN_MENU_TITLE, params::LOCAL_ENV_0_SUSTAIN, 2};
PLACE_SDRAM_BSS envelope::Segment env3ReleaseMenu{STRING_FOR_RELEASE, STRING_FOR_ENV_RELEASE_MENU_TITLE, params::LOCAL_ENV_0_RELEASE, 2};
PLACE_SDRAM_DATA std::array<MenuItem*, 4> child13 = {
    &env3AttackMenu,
    &env3DecayMenu,
    &env3SustainMenu,
    &env3ReleaseMenu,
};
PLACE_SDRAM_BSS envelope::EnvelopeMenu env3Menu{STRING_FOR_ENVELOPE_MENU_TITLE, child13, 2};
PLACE_SDRAM_BSS envelope::Segment env4AttackMenu{STRING_FOR_ATTACK, STRING_FOR_ENV_ATTACK_MENU_TITLE, params::LOCAL_ENV_0_ATTACK, 3};
PLACE_SDRAM_BSS envelope::Segment env4DecayMenu{STRING_FOR_DECAY, STRING_FOR_ENV_DECAY_MENU_TITLE, params::LOCAL_ENV_0_DECAY, 3};
PLACE_SDRAM_BSS envelope::Segment env4SustainMenu{STRING_FOR_SUSTAIN, STRING_FOR_ENV_SUSTAIN_MENU_TITLE, params::LOCAL_ENV_0_SUSTAIN, 3};
PLACE_SDRAM_BSS envelope::Segment env4ReleaseMenu{STRING_FOR_RELEASE, STRING_FOR_ENV_RELEASE_MENU_TITLE, params::LOCAL_ENV_0_RELEASE, 3};
PLACE_SDRAM_DATA std::array<MenuItem*, 4> child14 = {
    &env4AttackMenu,
    &env4DecayMenu,
    &env4SustainMenu,
    &env4ReleaseMenu,
};
PLACE_SDRAM_BSS envelope::EnvelopeMenu env4Menu{STRING_FOR_ENVELOPE_MENU_TITLE, child14, 3};
PLACE_SDRAM_BSS lfo::Type lfo1TypeMenu{STRING_FOR_SHAPE, STRING_FOR_LFO_TYPE, LFO1_ID};
PLACE_SDRAM_BSS lfo::Sync lfo1SyncMenu{STRING_FOR_SYNC, STRING_FOR_LFO_SYNC, LFO1_ID};
PLACE_SDRAM_BSS patched_param::Integer lfo1RateMenu{STRING_FOR_RATE, STRING_FOR_LFO1_RATE, params::GLOBAL_LFO_FREQ_1};
PLACE_SDRAM_DATA std::array<MenuItem*, 3> child15 = {
    &lfo1TypeMenu,
    &lfo1SyncMenu,
    &lfo1RateMenu,
};
PLACE_SDRAM_BSS HorizontalMenu lfo1Menu{STRING_FOR_LFO1, child15};
PLACE_SDRAM_BSS lfo::Type lfo2TypeMenu{STRING_FOR_SHAPE, STRING_FOR_LFO_TYPE, LFO2_ID};
PLACE_SDRAM_BSS lfo::Sync lfo2SyncMenu{STRING_FOR_SYNC, STRING_FOR_LFO_SYNC, LFO2_ID};
PLACE_SDRAM_BSS patched_param::Integer lfo2RateMenu{STRING_FOR_RATE, STRING_FOR_LFO2_RATE, params::LOCAL_LFO_LOCAL_FREQ_1};
PLACE_SDRAM_DATA std::array<MenuItem*, 3> child16 = {
    &lfo2TypeMenu,
    &lfo2SyncMenu,
    &lfo2RateMenu,
};
PLACE_SDRAM_BSS HorizontalMenu lfo2Menu{STRING_FOR_LFO2, child16};
PLACE_SDRAM_BSS lfo::Type lfo3TypeMenu{STRING_FOR_SHAPE, STRING_FOR_LFO_TYPE, LFO3_ID};
PLACE_SDRAM_BSS lfo::Sync lfo3SyncMenu{STRING_FOR_SYNC, STRING_FOR_LFO_SYNC, LFO3_ID};
PLACE_SDRAM_BSS patched_param::Integer lfo3RateMenu{STRING_FOR_RATE, STRING_FOR_LFO3_RATE, params::GLOBAL_LFO_FREQ_2};
PLACE_SDRAM_DATA std::array<MenuItem*, 3> child17 = {
    &lfo3TypeMenu,
    &lfo3SyncMenu,
    &lfo3RateMenu,
};
PLACE_SDRAM_BSS HorizontalMenu lfo3Menu{STRING_FOR_LFO3, child17};
PLACE_SDRAM_BSS lfo::Type lfo4TypeMenu{STRING_FOR_SHAPE, STRING_FOR_LFO_TYPE, LFO4_ID};
PLACE_SDRAM_BSS lfo::Sync lfo4SyncMenu{STRING_FOR_SYNC, STRING_FOR_LFO_SYNC, LFO4_ID};
PLACE_SDRAM_BSS patched_param::Integer lfo4RateMenu{STRING_FOR_RATE, STRING_FOR_LFO4_RATE, params::LOCAL_LFO_LOCAL_FREQ_2};
PLACE_SDRAM_DATA std::array<MenuItem*, 3> child18 = {
    &lfo4TypeMenu,
    &lfo4SyncMenu,
    &lfo4RateMenu,
};
PLACE_SDRAM_BSS HorizontalMenu lfo4Menu{STRING_FOR_LFO4, child18};
// clang-format on
