from dmui.dsl import *

freq = Menu(
    "filter::HPFFreq",
    "hpfFreqMenu",
    ["{name}", "{title}", "params::LOCAL_HPF_FREQ"],
    "STRING_FOR_HPF_FREQUENCY",
    "filter/hpf/frequency.md",
    name="STRING_FOR_FREQUENCY",
)

res = Menu(
    "patched_param::IntegerNonFM",
    "hpfResMenu",
    ["{name}", "{title}", "params::LOCAL_HPF_RESONANCE"],
    "STRING_FOR_HPF_RESONANCE",
    "filter/hpf/resonance.md",
    name="STRING_FOR_RESONANCE",
)

mode = Menu(
    "filter::HPFMode",
    "hpfModeMenu",
    ["{name}", "{title}"],
    "STRING_FOR_HPF_MODE",
    "filter/hpf/resonance.md",
    name="STRING_FOR_MODE",
)

morph = MultiModeMenu(
    "filter::FilterMorph",
    "hpfMorphMenu",
    ["{title}", "params::LOCAL_HPF_MORPH", "true"],
    "STRING_FOR_MORPH",
    [
        MultiModeMenuMode(
            "STRING_FOR_FM", "HPF is in a ladder mode", "filter/hpf/fm.md"
        ),
        MultiModeMenuMode(
            "STRING_FOR_MORPH", "HPF is in an SVF mode", "filter/hpf/morph.md"
        ),
    ],
)

menu = Submenu(
    "submenu::Filter",
    "hpfMenu",
    ["{title}", "%%CHILDREN%%"],
    "STRING_FOR_HPF",
    "filter/hpf/index.md",
    [freq, res, mode, morph],
    name="STRING_FOR_HPF",
)
