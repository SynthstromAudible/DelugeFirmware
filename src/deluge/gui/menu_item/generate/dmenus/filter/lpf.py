from dmui.dsl import *

freq = Menu(
    "filter::LPFFreq",
    "lpfFreqMenu",
    ["{name}", "{title}", "params::LOCAL_LPF_FREQ"],
    "filter/lpf/frequency.md",
    name="STRING_FOR_FREQUENCY",
    title="STRING_FOR_LPF_FREQUENCY",
)

res = Menu(
    "patched_param::IntegerNonFM",
    "lpfResMenu",
    ["{name}", "{title}", "params::LOCAL_LPF_RESONANCE"],
    "filter/lpf/resonance.md",
    name="STRING_FOR_RESONANCE",
    title="STRING_FOR_LPF_RESONANCE",
)

mode = Menu(
    "filter::LPFMode",
    "lpfModeMenu",
    ["{name}", "{title}"],
    "filter/lpf/resonance.md",
    name="STRING_FOR_MODE",
    title="STRING_FOR_LPF_MODE",
)

global_mode = Menu(
    "filter::GlobalLPFMode",
    "globalLPFModeMenu",
    ["{name}", "{title}"],
    "filter/lpf/resonance.md",
    name="STRING_FOR_MODE",
    title="STRING_FOR_LPF_MODE",
)

morph = MultiModeMenu(
    "filter::FilterMorph",
    "lpfMorphMenu",
    ["{title}", "params::LOCAL_LPF_MORPH", "false"],
    [
        MultiModeMenuMode(
            "STRING_FOR_DRIVE", "LPF is in a ladder mode", "filter/lpf/drive.md"
        ),
        MultiModeMenuMode(
            "STRING_FOR_MORPH", "LPF is in an SVF mode", "filter/lpf/morph.md"
        ),
    ],
    title="STRING_FOR_MORPH",
)

menu = Submenu(
    "submenu::Filter",
    "lpfMenu",
    ["{title}", "%%CHILDREN%%"],
    "filter/lpf/index.md",
    [freq, res, mode, morph],
    name="STRING_FOR_LPF",
)
