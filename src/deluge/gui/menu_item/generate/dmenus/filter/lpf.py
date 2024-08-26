from dmui.dsl import *

freq = Menu(
    "filter::FilterParam",
    "lpfFreqMenu",
    [
        "{name}",
        "{title}",
        "params::LOCAL_LPF_FREQ",
        "filter::FilterSlot::LPF",
        "filter::FilterParamType::FREQUENCY",
    ],
    "filter/lpf/frequency.md",
    name="STRING_FOR_FREQUENCY",
    title="STRING_FOR_LPF_FREQUENCY",
)

res = Menu(
    "filter::FilterParam",
    "lpfResMenu",
    [
        "{name}",
        "{title}",
        "params::LOCAL_LPF_RESONANCE",
        "filter::FilterSlot::LPF",
        "filter::FilterParamType::RESONANCE",
    ],
    "filter/lpf/resonance.md",
    name="STRING_FOR_RESONANCE",
    title="STRING_FOR_LPF_RESONANCE",
)

mode = Menu(
    "filter::FilterModeSelection",
    "lpfModeMenu",
    [
        "{name}",
        "{title}",
        "filter::FilterSlot::LPF",
    ],
    "filter/lpf/resonance.md",
    name="STRING_FOR_MODE",
    title="STRING_FOR_LPF_MODE",
)

morph = MultiModeMenu(
    "filter::FilterParam",
    "lpfMorphMenu",
    [
        "{title}",
        "params::LOCAL_LPF_MORPH",
        "filter::FilterSlot::LPF",
        "filter::FilterParamType::MORPH",
    ],
    [
        MultiModeMenuMode(
            "STRING_FOR_DRIVE", "LPF is in a ladder mode", "filter/lpf/drive.md"
        ),
        MultiModeMenuMode(
            "STRING_FOR_MORPH", "LPF is in an SVF mode", "filter/lpf/morph.md"
        ),
    ],
    name="STRING_FOR_MORPH",
    title="STRING_FOR_LPF_MORPH",
)

menu = Submenu(
    "HorizontalMenu",
    "lpfMenu",
    ["{title}", "%%CHILDREN%%"],
    "filter/lpf/index.md",
    [mode, freq, res, morph],
    name="STRING_FOR_LPF",
)
