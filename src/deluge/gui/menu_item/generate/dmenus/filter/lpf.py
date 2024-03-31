from dmui.dsl import *

freq = Menu(
    "filter::LPFFreq",
    "lpfFreqMenu",
    ["{name}", "{title}", "params::LOCAL_LPF_FREQ"],
    "STRING_FOR_LPF_FREQUENCY",
    "filter/lpf/frequency.md",
    name="STRING_FOR_FREQUENCY",
)

res = Menu(
    "patched_param::IntegerNonFM",
    "lpfResMenu",
    ["{name}", "{title}", "params::LOCAL_LPF_RESONANCE"],
    "STRING_FOR_LPF_RESONANCE",
    "filter/lpf/resonance.md",
    name="STRING_FOR_RESONANCE",
)

mode = Menu(
    "filter::LPFMode",
    "lpfModeMenu",
    ["{name}", "{title}"],
    "STRING_FOR_LPF_MODE",
    "filter/lpf/resonance.md",
    name="STRING_FOR_MODE",
)

morph = MultiModeMenu(
    "filter::FilterMorph",
    "lpfMorphMenu",
    ["{title}", "params::LOCAL_LPF_MORPH", "false"],
    "STRING_FOR_MORPH",
    [
        MultiModeMenuMode(
            "STRING_FOR_DRIVE", "LPF is in a ladder mode", "filter/lpf/drive.md"
        ),
        MultiModeMenuMode(
            "STRING_FOR_MORPH", "LPF is in an SVF mode", "filter/lpf/morph.md"
        ),
    ],
)

menu = Submenu(
    "submenu::Filter",
    "lpfMenu",
    ["{title}", "%%CHILDREN%%"],
    "STRING_FOR_LPF",
    "filter/lpf/index.md",
    [freq, res, mode, morph],
    name="STRING_FOR_LPF",
)
