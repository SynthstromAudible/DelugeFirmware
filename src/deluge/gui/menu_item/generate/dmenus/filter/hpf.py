from dmui.dsl import *

freq = Menu(
    "filter::HPFFreq",
    "hpfFreqMenu",
    ["{name}", "{title}", "params::LOCAL_HPF_FREQ"],
    "filter/hpf/frequency.md",
    name="STRING_FOR_FREQUENCY",
    title="STRING_FOR_HPF_FREQUENCY",
)

res = Menu(
    "patched_param::IntegerNonFM",
    "hpfResMenu",
    ["{name}", "{title}", "params::LOCAL_HPF_RESONANCE"],
    "filter/hpf/resonance.md",
    name="STRING_FOR_RESONANCE",
    title="STRING_FOR_HPF_RESONANCE",
)

mode = Menu(
    "filter::HPFMode",
    "hpfModeMenu",
    ["{name}", "{title}"],
    "filter/hpf/resonance.md",
    name="STRING_FOR_MODE",
    title="STRING_FOR_HPF_MODE",
)

global_mode = Menu(
    "filter::GlobalHPFMode",
    "globalHPFModeMenu",
    ["{name}", "{title}"],
    "filter/hpf/resonance.md",
    name="STRING_FOR_MODE",
    title="STRING_FOR_HPF_MODE",
)

morph = MultiModeMenu(
    "filter::FilterMorph",
    "hpfMorphMenu",
    ["{title}", "params::LOCAL_HPF_MORPH", "true"],
    [
        MultiModeMenuMode(
            "STRING_FOR_FM", "HPF is in a ladder mode", "filter/hpf/fm.md"
        ),
        MultiModeMenuMode(
            "STRING_FOR_MORPH", "HPF is in an SVF mode", "filter/hpf/morph.md"
        ),
    ],
    name="STRING_FOR_MORPH",
)

menu = Submenu(
    "submenu::Filter",
    "hpfMenu",
    ["{title}", "%%CHILDREN%%"],
    "filter/hpf/index.md",
    [freq, res, mode, morph],
    name="STRING_FOR_HPF",
)
