from dmui.dsl import Menu, Submenu

count = Menu(
    "unison::CountToStereoSpread",
    "numUnisonMenu",
    ["{name}", "{title}"],
    "oscillator/unison/count.md",
    name="STRING_FOR_UNISON_NUMBER_SHORT",
    title="STRING_FOR_UNISON_NUMBER",
)

detune = Menu(
    "unison::Detune",
    "unisonDetuneMenu",
    ["{name}", "{title}"],
    "oscillator/unison/detune.md",
    name="STRING_FOR_UNISON_DETUNE_SHORT",
    title="STRING_FOR_UNISON_DETUNE",
)

stereo_spread = Menu(
    "unison::StereoSpread",
    "unison::stereoSpreadMenu",
    ["{name}", "{title}"],
    "oscillator/unison/stereo_spread.md",
    name="STRING_FOR_UNISON_STEREO_SPREAD_SHORT",
    title="STRING_FOR_UNISON_STEREO_SPREAD",
)

menu = Submenu(
    "HorizontalMenu",
    "unisonMenu",
    ["{name}", "%%CHILDREN%%", "HorizontalMenu::Layout::FIXED"],
    "oscillator/unison/index.md",
    [
        count,
        detune,
        stereo_spread,
    ],
    name="STRING_FOR_UNISON",
)
