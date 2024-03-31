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
// clang-format on
