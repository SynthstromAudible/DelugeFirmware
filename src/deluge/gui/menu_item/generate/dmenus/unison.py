from dmui.dsl import *

count = Menu(
    "unison::Count",
    "numUnisonMenu",
    ["{name}"],
    "oscillator/unison/count.md",
    name="STRING_FOR_NUMBER",
    title="STRING_FOR_UNISON_NUMBER",
)

detune = Menu(
    "unison::Detune",
    "unisonDetuneMenu",
    ["{name}"],
    "oscillator/unison/detune.md",
    name="STRING_FOR_DETUNE",
    title="STRING_FOR_UNISON_DETUNE",
)

stereo_spread = Menu(
    "unison::StereoSpread",
    "unison::stereoSpreadMenu",
    ["{name}"],
    "oscillator/unison/stereo_spread.md",
    name="STRING_FOR_SPREAD",
    title="STRING_FOR_UNISON_STEREO_SPREAD",
)

menu = Submenu(
    "HorizontalMenu",
    "unisonMenu",
    ["{name}", "%%CHILDREN%%"],
    "oscillator/unison/index.md",
    [
        count,
        detune,
        stereo_spread,
    ],
    name="STRING_FOR_UNISON",
)
