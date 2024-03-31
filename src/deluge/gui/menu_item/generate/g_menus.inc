// clang-format off
filter::LPFFreq lpfFreqMenu{STRING_FOR_FREQUENCY, STRING_FOR_LPF_FREQUENCY, params::LOCAL_LPF_FREQ};
patched_param::IntegerNonFM lpfResMenu{STRING_FOR_RESONANCE, STRING_FOR_LPF_RESONANCE, params::LOCAL_LPF_RESONANCE};
filter::LPFMode lpfModeMenu{STRING_FOR_MODE, STRING_FOR_LPF_MODE};
filter::FilterMorph lpfMorphMenu{STRING_FOR_MORPH, params::LOCAL_LPF_MORPH, false};
submenu::Filter lpfMenu{
    STRING_FOR_LPF,
    {
        &lpfFreqMenu,
        &lpfResMenu,
        &lpfModeMenu,
        &lpfMorphMenu,
    },
};
filter::HPFFreq hpfFreqMenu{STRING_FOR_FREQUENCY, STRING_FOR_HPF_FREQUENCY, params::LOCAL_HPF_FREQ};
patched_param::IntegerNonFM hpfResMenu{STRING_FOR_RESONANCE, STRING_FOR_HPF_RESONANCE, params::LOCAL_HPF_RESONANCE};
filter::HPFMode hpfModeMenu{STRING_FOR_MODE, STRING_FOR_HPF_MODE};
filter::FilterMorph hpfMorphMenu{STRING_FOR_MORPH, params::LOCAL_HPF_MORPH, true};
submenu::Filter hpfMenu{
    STRING_FOR_HPF,
    {
        &hpfFreqMenu,
        &hpfResMenu,
        &hpfModeMenu,
        &hpfMorphMenu,
    },
};
FilterRouting filterRoutingMenu{STRING_FOR_FILTER_ROUTE};
submenu::Filter soundFiltersMenu{
    STRING_FOR_FILTERS,
    {
        &lpfMenu,
        &hpfMenu,
        &filterRoutingMenu,
    },
};
audio_compressor::CompParam threshold{STRING_FOR_THRESHOLD, STRING_FOR_THRESHOLD, params::UNPATCHED_COMPRESSOR_THRESHOLD};
audio_compressor::Ratio compRatio{STRING_FOR_RATIO, STRING_FOR_RATIO};
audio_compressor::Attack compAttack{STRING_FOR_ATTACK, STRING_FOR_ATTACK};
audio_compressor::Release compRelease{STRING_FOR_RELEASE, STRING_FOR_RELEASE};
audio_compressor::SideHPF compHPF{STRING_FOR_HPF, STRING_FOR_HPF};
Submenu audioCompMenu{
    STRING_FOR_COMMUNITY_FEATURE_MASTER_COMPRESSOR,
    {
        &threshold,
        &compRatio,
        &compAttack,
        &compRelease,
        &compHPF,
    },
};
// clang-format on
