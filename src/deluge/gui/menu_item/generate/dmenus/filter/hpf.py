from dmui.dsl import *

freq = Menu(
    "filter::FilterParam",
    "hpfFreqMenu",
    [
        "{name}",
        "{title}",
        "params::LOCAL_HPF_FREQ",
        "filter::FilterSlot::HPF",
        "filter::FilterParamType::FREQUENCY",
    ],
    "filter/hpf/frequency.md",
    name="STRING_FOR_FREQUENCY",
    title="STRING_FOR_HPF_FREQUENCY",
)

res = Menu(
    "filter::FilterParam",
    "hpfResMenu",
    [
        "{name}",
        "{title}",
        "params::LOCAL_HPF_RESONANCE",
        "filter::FilterSlot::HPF",
        "filter::FilterParamType::RESONANCE",
    ],
    "filter/hpf/resonance.md",
    name="STRING_FOR_RESONANCE",
    title="STRING_FOR_HPF_RESONANCE",
)

mode = Menu(
    "filter::FilterModeSelection",
    "hpfModeMenu",
    [
        "{name}",
        "{title}",
        "filter::FilterSlot::HPF",
    ],
    "filter/hpf/resonance.md",
    name="STRING_FOR_MODE",
    title="STRING_FOR_HPF_MODE",
)

morph = MultiModeMenu(
    "filter::FilterParam",
    "hpfMorphMenu",
    [
        "{title}",
        "params::LOCAL_HPF_MORPH",
        "filter::FilterSlot::HPF",
        "filter::FilterParamType::MORPH",
    ],
    [
        MultiModeMenuMode(
            "STRING_FOR_FM", "HPF is in a ladder mode", "filter/hpf/fm.md"
        ),
        MultiModeMenuMode(
            "STRING_FOR_MORPH", "HPF is in an SVF mode", "filter/hpf/morph.md"
        ),
    ],
    name="STRING_FOR_MORPH",
    title="STRING_FOR_HPF_MORPH",
)

menu = Submenu(
    "HorizontalMenu",
    "hpfMenu",
    ["{title}", "%%CHILDREN%%"],
    "filter/hpf/index.md",
    [mode, freq, res, morph],
    name="STRING_FOR_HPF",
)
