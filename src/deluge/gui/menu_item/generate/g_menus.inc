// clang-format off
filter::FilterModeSelection lpfModeMenu{STRING_FOR_MODE, STRING_FOR_LPF_MODE, filter::FilterSlot::LPF};
filter::FilterParam lpfFreqMenu{STRING_FOR_FREQUENCY, STRING_FOR_LPF_FREQUENCY, params::LOCAL_LPF_FREQ, filter::FilterSlot::LPF, filter::FilterParamType::FREQUENCY};
filter::FilterParam lpfResMenu{STRING_FOR_RESONANCE, STRING_FOR_LPF_RESONANCE, params::LOCAL_LPF_RESONANCE, filter::FilterSlot::LPF, filter::FilterParamType::RESONANCE};
filter::FilterParam lpfMorphMenu{STRING_FOR_LPF_MORPH, params::LOCAL_LPF_MORPH, filter::FilterSlot::LPF, filter::FilterParamType::MORPH};
std::array<MenuItem*, 4> child0 = {
    &lpfModeMenu,
    &lpfFreqMenu,
    &lpfResMenu,
    &lpfMorphMenu,
};
HorizontalMenu lpfMenu{STRING_FOR_LPF, child0};
filter::FilterModeSelection hpfModeMenu{STRING_FOR_MODE, STRING_FOR_HPF_MODE, filter::FilterSlot::HPF};
filter::FilterParam hpfFreqMenu{STRING_FOR_FREQUENCY, STRING_FOR_HPF_FREQUENCY, params::LOCAL_HPF_FREQ, filter::FilterSlot::HPF, filter::FilterParamType::FREQUENCY};
filter::FilterParam hpfResMenu{STRING_FOR_RESONANCE, STRING_FOR_HPF_RESONANCE, params::LOCAL_HPF_RESONANCE, filter::FilterSlot::HPF, filter::FilterParamType::RESONANCE};
filter::FilterParam hpfMorphMenu{STRING_FOR_HPF_MORPH, params::LOCAL_HPF_MORPH, filter::FilterSlot::HPF, filter::FilterParamType::MORPH};
std::array<MenuItem*, 4> child1 = {
    &hpfModeMenu,
    &hpfFreqMenu,
    &hpfResMenu,
    &hpfMorphMenu,
};
HorizontalMenu hpfMenu{STRING_FOR_HPF, child1};
FilterRouting filterRoutingMenu{STRING_FOR_FILTER_ROUTE};
std::array<MenuItem*, 3> child2 = {
    &lpfMenu,
    &hpfMenu,
    &filterRoutingMenu,
};
Submenu soundFiltersMenu{STRING_FOR_FILTERS, child2};
audio_compressor::CompParam threshold{STRING_FOR_THRESHOLD, STRING_FOR_COMP_THRESHOLD_MENU_TITLE, params::UNPATCHED_COMPRESSOR_THRESHOLD, BAR};
audio_compressor::Ratio compRatio{STRING_FOR_RATIO, STRING_FOR_RATIO};
audio_compressor::Blend compBlend{STRING_FOR_BLEND, STRING_FOR_BLEND};
audio_compressor::Attack compAttack{STRING_FOR_ATTACK, STRING_FOR_ATTACK};
audio_compressor::Release compRelease{STRING_FOR_RELEASE, STRING_FOR_RELEASE};
audio_compressor::SideHPF compHPF{STRING_FOR_HPF, STRING_FOR_HPF};
std::array<MenuItem*, 6> child3 = {
    &threshold,
    &compRatio,
    &compBlend,
    &compAttack,
    &compRelease,
    &compHPF,
};
HorizontalMenu audioCompMenu{STRING_FOR_COMMUNITY_FEATURE_MASTER_COMPRESSOR, child3};
unison::CountToStereoSpread numUnisonMenu{STRING_FOR_UNISON_NUMBER, STRING_FOR_UNISON_NUMBER_MENU_TITLE};
unison::Detune unisonDetuneMenu{STRING_FOR_UNISON_DETUNE, STRING_FOR_UNISON_DETUNE_MENU_TITLE};
unison::StereoSpread unison::stereoSpreadMenu{STRING_FOR_UNISON_STEREO_SPREAD, STRING_FOR_UNISON_STEREO_SPREAD_MENU_TITLE};
std::array<MenuItem*, 3> child4 = {
    &numUnisonMenu,
    &unisonDetuneMenu,
    &unison::stereoSpreadMenu,
};
HorizontalMenu unisonMenu{STRING_FOR_UNISON, child4, HorizontalMenu::Layout::FIXED};
osc::AudioRecorder sample0RecorderMenu{STRING_FOR_RECORD_TO_SAMPLE, 0};
osc::AudioRecorder sample1RecorderMenu{STRING_FOR_RECORD_TO_SAMPLE, 1};
std::array<MenuItem*, 2> child5 = {
    &sample0RecorderMenu,
    &sample1RecorderMenu,
};
HorizontalMenu recorderMenu{STRING_FOR_SAMPLE_RECORDER, child5};
osc::Type osc0TypeMenu{STRING_FOR_TYPE, STRING_FOR_OSC_TYPE_MENU_TITLE, 0};
FileSelector deluge::gui::menu_item::file0SelectorMenu{STRING_FOR_FILE_BROWSER, 0};
sample::RoundRobinMode sample0RoundRobinModeMenu{STRING_FOR_MODE, 0};
sample::RoundRobinFileItem sample0RRFile1{STRING_FOR_FILE, 0, 0};
sample::VariantMarkers sample0RRMarkers1{STRING_FOR_MARKERS, 0, 0};
sample::VariantTranspose sample0RRTranspose1{STRING_FOR_TRANSPOSE, 0, 0};
sample::VariantCents sample0RRCents1{STRING_FOR_CENTS, 0, 0};
std::array<MenuItem*, 4> child6 = {
    &sample0RRFile1,
    &sample0RRMarkers1,
    &sample0RRTranspose1,
    &sample0RRCents1,
};
sample::RoundRobinSlot sample0RoundRobinSlot1Menu{STRING_FOR_VARIANT_SLOT_1, child6, 0, 0};
sample::RoundRobinFileItem sample0RRFile2{STRING_FOR_FILE, 0, 1};
sample::VariantMarkers sample0RRMarkers2{STRING_FOR_MARKERS, 0, 1};
sample::VariantTranspose sample0RRTranspose2{STRING_FOR_TRANSPOSE, 0, 1};
sample::VariantCents sample0RRCents2{STRING_FOR_CENTS, 0, 1};
std::array<MenuItem*, 4> child7 = {
    &sample0RRFile2,
    &sample0RRMarkers2,
    &sample0RRTranspose2,
    &sample0RRCents2,
};
sample::RoundRobinSlot sample0RoundRobinSlot2Menu{STRING_FOR_VARIANT_SLOT_2, child7, 0, 1};
sample::RoundRobinFileItem sample0RRFile3{STRING_FOR_FILE, 0, 2};
sample::VariantMarkers sample0RRMarkers3{STRING_FOR_MARKERS, 0, 2};
sample::VariantTranspose sample0RRTranspose3{STRING_FOR_TRANSPOSE, 0, 2};
sample::VariantCents sample0RRCents3{STRING_FOR_CENTS, 0, 2};
std::array<MenuItem*, 4> child8 = {
    &sample0RRFile3,
    &sample0RRMarkers3,
    &sample0RRTranspose3,
    &sample0RRCents3,
};
sample::RoundRobinSlot sample0RoundRobinSlot3Menu{STRING_FOR_VARIANT_SLOT_3, child8, 0, 2};
sample::RoundRobinFileItem sample0RRFile4{STRING_FOR_FILE, 0, 3};
sample::VariantMarkers sample0RRMarkers4{STRING_FOR_MARKERS, 0, 3};
sample::VariantTranspose sample0RRTranspose4{STRING_FOR_TRANSPOSE, 0, 3};
sample::VariantCents sample0RRCents4{STRING_FOR_CENTS, 0, 3};
std::array<MenuItem*, 4> child9 = {
    &sample0RRFile4,
    &sample0RRMarkers4,
    &sample0RRTranspose4,
    &sample0RRCents4,
};
sample::RoundRobinSlot sample0RoundRobinSlot4Menu{STRING_FOR_VARIANT_SLOT_4, child9, 0, 3};
std::array<MenuItem*, 5> child10 = {
    &sample0RoundRobinModeMenu,
    &sample0RoundRobinSlot1Menu,
    &sample0RoundRobinSlot2Menu,
    &sample0RoundRobinSlot3Menu,
    &sample0RoundRobinSlot4Menu,
};
sample::RoundRobinSubmenu sample0RoundRobinMenu{STRING_FOR_VARIANTS, child10, 0};
sample::Transpose source0TransposeMenu{STRING_FOR_TRANSPOSE, STRING_FOR_OSC_TRANSPOSE_MENU_TITLE, params::LOCAL_OSC_A_PITCH_ADJUST, 0};
osc::source::WaveIndex source0WaveIndexMenu{STRING_FOR_WAVE_INDEX, STRING_FOR_OSC_WAVE_IND_MENU_TITLE, params::LOCAL_OSC_A_WAVE_INDEX, 0};
osc::PulseWidth osc0PulseWidthMenu{STRING_FOR_PULSE_WIDTH, STRING_FOR_OSC_P_WIDTH_MENU_TITLE, params::LOCAL_OSC_A_PHASE_WIDTH, 0};
osc::source::Feedback source0FeedbackMenu{STRING_FOR_FEEDBACK, STRING_FOR_CARRIER_FEED_MENU_TITLE, params::LOCAL_CARRIER_0_FEEDBACK, 0};
osc::RetriggerPhase osc0PhaseMenu{STRING_FOR_RETRIGGER_PHASE, STRING_FOR_OSC_R_PHASE_MENU_TITLE, 0, false};
sample::Reverse sample0ReverseMenu{STRING_FOR_REVERSE, STRING_FOR_SAMP_REVERSE_MENU_TITLE, 0};
sample::Repeat sample0RepeatMenu{STRING_FOR_REPEAT_MODE, STRING_FOR_SAMP_REPEAT_MENU_TITLE, 0};
sample::TimeStretch sample0TimeStretchMenu{STRING_FOR_SPEED, STRING_FOR_SAMP_SPEED_MENU_TITLE, 0};
sample::PitchSpeed sample0PitchSpeedMenu{STRING_FOR_PITCH_SPEED, 0};
sample::Interpolation sample0InterpolationMenu{STRING_FOR_INTERPOLATION, STRING_FOR_SAMP_INTERP_MENU_TITLE, 0};
sample::Start sample0StartMenu{STRING_FOR_START_POINT, 0};
sample::End sample0EndMenu{STRING_FOR_END_POINT, 0};
std::array<MenuItem*, 15> child11 = {
    &osc0TypeMenu,
    &deluge::gui::menu_item::file0SelectorMenu,
    &sample0RoundRobinMenu,
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
submenu::ActualSource source0Menu{STRING_FOR_OSCILLATOR_1, child11, 0};
osc::Type osc1TypeMenu{STRING_FOR_TYPE, STRING_FOR_OSC_TYPE_MENU_TITLE, 1};
FileSelector deluge::gui::menu_item::file1SelectorMenu{STRING_FOR_FILE_BROWSER, 1};
sample::RoundRobinMode sample1RoundRobinModeMenu{STRING_FOR_MODE, 1};
sample::RoundRobinFileItem sample1RRFile1{STRING_FOR_FILE, 1, 0};
sample::VariantMarkers sample1RRMarkers1{STRING_FOR_MARKERS, 1, 0};
sample::VariantTranspose sample1RRTranspose1{STRING_FOR_TRANSPOSE, 1, 0};
sample::VariantCents sample1RRCents1{STRING_FOR_CENTS, 1, 0};
std::array<MenuItem*, 4> child12 = {
    &sample1RRFile1,
    &sample1RRMarkers1,
    &sample1RRTranspose1,
    &sample1RRCents1,
};
sample::RoundRobinSlot sample1RoundRobinSlot1Menu{STRING_FOR_VARIANT_SLOT_1, child12, 1, 0};
sample::RoundRobinFileItem sample1RRFile2{STRING_FOR_FILE, 1, 1};
sample::VariantMarkers sample1RRMarkers2{STRING_FOR_MARKERS, 1, 1};
sample::VariantTranspose sample1RRTranspose2{STRING_FOR_TRANSPOSE, 1, 1};
sample::VariantCents sample1RRCents2{STRING_FOR_CENTS, 1, 1};
std::array<MenuItem*, 4> child13 = {
    &sample1RRFile2,
    &sample1RRMarkers2,
    &sample1RRTranspose2,
    &sample1RRCents2,
};
sample::RoundRobinSlot sample1RoundRobinSlot2Menu{STRING_FOR_VARIANT_SLOT_2, child13, 1, 1};
sample::RoundRobinFileItem sample1RRFile3{STRING_FOR_FILE, 1, 2};
sample::VariantMarkers sample1RRMarkers3{STRING_FOR_MARKERS, 1, 2};
sample::VariantTranspose sample1RRTranspose3{STRING_FOR_TRANSPOSE, 1, 2};
sample::VariantCents sample1RRCents3{STRING_FOR_CENTS, 1, 2};
std::array<MenuItem*, 4> child14 = {
    &sample1RRFile3,
    &sample1RRMarkers3,
    &sample1RRTranspose3,
    &sample1RRCents3,
};
sample::RoundRobinSlot sample1RoundRobinSlot3Menu{STRING_FOR_VARIANT_SLOT_3, child14, 1, 2};
sample::RoundRobinFileItem sample1RRFile4{STRING_FOR_FILE, 1, 3};
sample::VariantMarkers sample1RRMarkers4{STRING_FOR_MARKERS, 1, 3};
sample::VariantTranspose sample1RRTranspose4{STRING_FOR_TRANSPOSE, 1, 3};
sample::VariantCents sample1RRCents4{STRING_FOR_CENTS, 1, 3};
std::array<MenuItem*, 4> child15 = {
    &sample1RRFile4,
    &sample1RRMarkers4,
    &sample1RRTranspose4,
    &sample1RRCents4,
};
sample::RoundRobinSlot sample1RoundRobinSlot4Menu{STRING_FOR_VARIANT_SLOT_4, child15, 1, 3};
std::array<MenuItem*, 5> child16 = {
    &sample1RoundRobinModeMenu,
    &sample1RoundRobinSlot1Menu,
    &sample1RoundRobinSlot2Menu,
    &sample1RoundRobinSlot3Menu,
    &sample1RoundRobinSlot4Menu,
};
sample::RoundRobinSubmenu sample1RoundRobinMenu{STRING_FOR_VARIANTS, child16, 1};
sample::Transpose source1TransposeMenu{STRING_FOR_TRANSPOSE, STRING_FOR_OSC_TRANSPOSE_MENU_TITLE, params::LOCAL_OSC_A_PITCH_ADJUST, 1};
osc::source::WaveIndex source1WaveIndexMenu{STRING_FOR_WAVE_INDEX, STRING_FOR_OSC_WAVE_IND_MENU_TITLE, params::LOCAL_OSC_A_WAVE_INDEX, 1};
osc::PulseWidth osc1PulseWidthMenu{STRING_FOR_PULSE_WIDTH, STRING_FOR_OSC_P_WIDTH_MENU_TITLE, params::LOCAL_OSC_A_PHASE_WIDTH, 1};
osc::source::Feedback source1FeedbackMenu{STRING_FOR_FEEDBACK, STRING_FOR_CARRIER_FEED_MENU_TITLE, params::LOCAL_CARRIER_0_FEEDBACK, 1};
osc::RetriggerPhase osc1PhaseMenu{STRING_FOR_RETRIGGER_PHASE, STRING_FOR_OSC_R_PHASE_MENU_TITLE, 1, false};
sample::Reverse sample1ReverseMenu{STRING_FOR_REVERSE, STRING_FOR_SAMP_REVERSE_MENU_TITLE, 1};
sample::Repeat sample1RepeatMenu{STRING_FOR_REPEAT_MODE, STRING_FOR_SAMP_REPEAT_MENU_TITLE, 1};
sample::TimeStretch sample1TimeStretchMenu{STRING_FOR_SPEED, STRING_FOR_SAMP_SPEED_MENU_TITLE, 1};
sample::PitchSpeed sample1PitchSpeedMenu{STRING_FOR_PITCH_SPEED, 1};
sample::Interpolation sample1InterpolationMenu{STRING_FOR_INTERPOLATION, STRING_FOR_SAMP_INTERP_MENU_TITLE, 1};
sample::Start sample1StartMenu{STRING_FOR_START_POINT, 1};
sample::End sample1EndMenu{STRING_FOR_END_POINT, 1};
std::array<MenuItem*, 15> child17 = {
    &osc1TypeMenu,
    &deluge::gui::menu_item::file1SelectorMenu,
    &sample1RoundRobinMenu,
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
submenu::ActualSource source1Menu{STRING_FOR_OSCILLATOR_2, child17, 1};
modulator::Transpose modulator0TransposeMenu{STRING_FOR_TRANSPOSE, STRING_FOR_FM_MOD_TRAN_MENU_TITLE, params::LOCAL_MODULATOR_0_PITCH_ADJUST, 0};
source::patched_param::ModulatorFeedback modulator0FeedbackMenu{STRING_FOR_FEEDBACK, STRING_FOR_FM_MOD_FBACK_MENU_TITLE, params::LOCAL_MODULATOR_0_FEEDBACK, 0};
osc::RetriggerPhase modulator0PhaseMenu{STRING_FOR_RETRIGGER_PHASE, STRING_FOR_FM_MOD_RETRIG_MENU_TITLE, 0, true};
std::array<MenuItem*, 3> child18 = {
    &modulator0TransposeMenu,
    &modulator0FeedbackMenu,
    &modulator0PhaseMenu,
};
submenu::Modulator modulator0Menu{STRING_FOR_FM_MODULATOR_1, child18};
modulator::Transpose modulator1TransposeMenu{STRING_FOR_TRANSPOSE, STRING_FOR_FM_MOD_TRAN_MENU_TITLE, params::LOCAL_MODULATOR_0_PITCH_ADJUST, 1};
source::patched_param::ModulatorFeedback modulator1FeedbackMenu{STRING_FOR_FEEDBACK, STRING_FOR_FM_MOD_FBACK_MENU_TITLE, params::LOCAL_MODULATOR_0_FEEDBACK, 1};
osc::RetriggerPhase modulator1PhaseMenu{STRING_FOR_RETRIGGER_PHASE, STRING_FOR_FM_MOD_RETRIG_MENU_TITLE, 1, true};
modulator::Destination modulatorDestMenu{STRING_FOR_DESTINATION, STRING_FOR_FM_MOD2_DEST_MENU_TITLE};
std::array<MenuItem*, 4> child19 = {
    &modulator1TransposeMenu,
    &modulator1FeedbackMenu,
    &modulator1PhaseMenu,
    &modulatorDestMenu,
};
submenu::Modulator modulator1Menu{STRING_FOR_FM_MODULATOR_2, child19};
osc::source::Volume source0VolumeMenu{STRING_FOR_OSC_LEVEL_MENU_TITLE, params::LOCAL_OSC_A_VOLUME, 0};
osc::source::Volume source1VolumeMenu{STRING_FOR_OSC_LEVEL_MENU_TITLE, params::LOCAL_OSC_A_VOLUME, 1};
source::patched_param::ModulatorLevel modulator0Volume{STRING_FOR_FM_MOD_LEVEL_MENU_TITLE, params::LOCAL_MODULATOR_0_VOLUME, 0};
source::patched_param::ModulatorLevel modulator1Volume{STRING_FOR_FM_MOD_LEVEL_MENU_TITLE, params::LOCAL_MODULATOR_0_VOLUME, 1};
patched_param::IntegerNonFM noiseMenu{STRING_FOR_NOISE_LEVEL, params::LOCAL_NOISE_VOLUME};
osc::Sync oscSyncMenu{STRING_FOR_OSCILLATOR_SYNC};
std::array<MenuItem*, 6> child20 = {
    &source0VolumeMenu,
    &source1VolumeMenu,
    &modulator0Volume,
    &modulator1Volume,
    &noiseMenu,
    &oscSyncMenu,
};
HorizontalMenu oscMixerMenu{STRING_FOR_OSCILLATOR_MIXER, child20};
envelope::Segment env1AttackMenu{STRING_FOR_ATTACK, STRING_FOR_ENV_ATTACK_MENU_TITLE, params::LOCAL_ENV_0_ATTACK, 0};
envelope::Segment env1DecayMenu{STRING_FOR_DECAY, STRING_FOR_ENV_DECAY_MENU_TITLE, params::LOCAL_ENV_0_DECAY, 0};
envelope::Segment env1SustainMenu{STRING_FOR_SUSTAIN, STRING_FOR_ENV_SUSTAIN_MENU_TITLE, params::LOCAL_ENV_0_SUSTAIN, 0};
envelope::Segment env1ReleaseMenu{STRING_FOR_RELEASE, STRING_FOR_ENV_RELEASE_MENU_TITLE, params::LOCAL_ENV_0_RELEASE, 0};
std::array<MenuItem*, 4> child21 = {
    &env1AttackMenu,
    &env1DecayMenu,
    &env1SustainMenu,
    &env1ReleaseMenu,
};
envelope::EnvelopeMenu env1Menu{STRING_FOR_ENVELOPE_MENU_TITLE, child21, 0};
envelope::Segment env2AttackMenu{STRING_FOR_ATTACK, STRING_FOR_ENV_ATTACK_MENU_TITLE, params::LOCAL_ENV_0_ATTACK, 1};
envelope::Segment env2DecayMenu{STRING_FOR_DECAY, STRING_FOR_ENV_DECAY_MENU_TITLE, params::LOCAL_ENV_0_DECAY, 1};
envelope::Segment env2SustainMenu{STRING_FOR_SUSTAIN, STRING_FOR_ENV_SUSTAIN_MENU_TITLE, params::LOCAL_ENV_0_SUSTAIN, 1};
envelope::Segment env2ReleaseMenu{STRING_FOR_RELEASE, STRING_FOR_ENV_RELEASE_MENU_TITLE, params::LOCAL_ENV_0_RELEASE, 1};
std::array<MenuItem*, 4> child22 = {
    &env2AttackMenu,
    &env2DecayMenu,
    &env2SustainMenu,
    &env2ReleaseMenu,
};
envelope::EnvelopeMenu env2Menu{STRING_FOR_ENVELOPE_MENU_TITLE, child22, 1};
envelope::Segment env3AttackMenu{STRING_FOR_ATTACK, STRING_FOR_ENV_ATTACK_MENU_TITLE, params::LOCAL_ENV_0_ATTACK, 2};
envelope::Segment env3DecayMenu{STRING_FOR_DECAY, STRING_FOR_ENV_DECAY_MENU_TITLE, params::LOCAL_ENV_0_DECAY, 2};
envelope::Segment env3SustainMenu{STRING_FOR_SUSTAIN, STRING_FOR_ENV_SUSTAIN_MENU_TITLE, params::LOCAL_ENV_0_SUSTAIN, 2};
envelope::Segment env3ReleaseMenu{STRING_FOR_RELEASE, STRING_FOR_ENV_RELEASE_MENU_TITLE, params::LOCAL_ENV_0_RELEASE, 2};
std::array<MenuItem*, 4> child23 = {
    &env3AttackMenu,
    &env3DecayMenu,
    &env3SustainMenu,
    &env3ReleaseMenu,
};
envelope::EnvelopeMenu env3Menu{STRING_FOR_ENVELOPE_MENU_TITLE, child23, 2};
envelope::Segment env4AttackMenu{STRING_FOR_ATTACK, STRING_FOR_ENV_ATTACK_MENU_TITLE, params::LOCAL_ENV_0_ATTACK, 3};
envelope::Segment env4DecayMenu{STRING_FOR_DECAY, STRING_FOR_ENV_DECAY_MENU_TITLE, params::LOCAL_ENV_0_DECAY, 3};
envelope::Segment env4SustainMenu{STRING_FOR_SUSTAIN, STRING_FOR_ENV_SUSTAIN_MENU_TITLE, params::LOCAL_ENV_0_SUSTAIN, 3};
envelope::Segment env4ReleaseMenu{STRING_FOR_RELEASE, STRING_FOR_ENV_RELEASE_MENU_TITLE, params::LOCAL_ENV_0_RELEASE, 3};
std::array<MenuItem*, 4> child24 = {
    &env4AttackMenu,
    &env4DecayMenu,
    &env4SustainMenu,
    &env4ReleaseMenu,
};
envelope::EnvelopeMenu env4Menu{STRING_FOR_ENVELOPE_MENU_TITLE, child24, 3};
lfo::Type lfo1TypeMenu{STRING_FOR_SHAPE, STRING_FOR_LFO_TYPE, LFO1_ID};
lfo::Sync lfo1SyncMenu{STRING_FOR_SYNC, STRING_FOR_LFO_SYNC, LFO1_ID};
patched_param::Integer lfo1RateMenu{STRING_FOR_RATE, STRING_FOR_LFO1_RATE, params::GLOBAL_LFO_FREQ_1};
std::array<MenuItem*, 3> child25 = {
    &lfo1TypeMenu,
    &lfo1SyncMenu,
    &lfo1RateMenu,
};
HorizontalMenu lfo1Menu{STRING_FOR_LFO1, child25};
lfo::Type lfo2TypeMenu{STRING_FOR_SHAPE, STRING_FOR_LFO_TYPE, LFO2_ID};
lfo::Sync lfo2SyncMenu{STRING_FOR_SYNC, STRING_FOR_LFO_SYNC, LFO2_ID};
patched_param::Integer lfo2RateMenu{STRING_FOR_RATE, STRING_FOR_LFO2_RATE, params::LOCAL_LFO_LOCAL_FREQ_1};
std::array<MenuItem*, 3> child26 = {
    &lfo2TypeMenu,
    &lfo2SyncMenu,
    &lfo2RateMenu,
};
HorizontalMenu lfo2Menu{STRING_FOR_LFO2, child26};
lfo::Type lfo3TypeMenu{STRING_FOR_SHAPE, STRING_FOR_LFO_TYPE, LFO3_ID};
lfo::Sync lfo3SyncMenu{STRING_FOR_SYNC, STRING_FOR_LFO_SYNC, LFO3_ID};
patched_param::Integer lfo3RateMenu{STRING_FOR_RATE, STRING_FOR_LFO3_RATE, params::GLOBAL_LFO_FREQ_2};
std::array<MenuItem*, 3> child27 = {
    &lfo3TypeMenu,
    &lfo3SyncMenu,
    &lfo3RateMenu,
};
HorizontalMenu lfo3Menu{STRING_FOR_LFO3, child27};
lfo::Type lfo4TypeMenu{STRING_FOR_SHAPE, STRING_FOR_LFO_TYPE, LFO4_ID};
lfo::Sync lfo4SyncMenu{STRING_FOR_SYNC, STRING_FOR_LFO_SYNC, LFO4_ID};
patched_param::Integer lfo4RateMenu{STRING_FOR_RATE, STRING_FOR_LFO4_RATE, params::LOCAL_LFO_LOCAL_FREQ_2};
std::array<MenuItem*, 3> child28 = {
    &lfo4TypeMenu,
    &lfo4SyncMenu,
    &lfo4RateMenu,
};
HorizontalMenu lfo4Menu{STRING_FOR_LFO4, child28};
// clang-format on
