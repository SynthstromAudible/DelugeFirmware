from dmui.dsl import *

count = Menu(
    "unison::CountToStereoSpread",
    "numUnisonMenu",
    ["{name}", "{title}"],
    "oscillator/unison/count.md",
    name="STRING_FOR_UNISON_NUMBER",
    title="STRING_FOR_UNISON_NUMBER_MENU_TITLE",
)

detune = Menu(
    "unison::Detune",
    "unisonDetuneMenu",
    ["{name}", "{title}"],
    "oscillator/unison/detune.md",
    name="STRING_FOR_UNISON_DETUNE",
    title="STRING_FOR_UNISON_DETUNE_MENU_TITLE",
)

stereo_spread = Menu(
    "unison::StereoSpread",
    "unison::stereoSpreadMenu",
    ["{name}", "{title}"],
    "oscillator/unison/stereo_spread.md",
    name="STRING_FOR_UNISON_STEREO_SPREAD",
    title="STRING_FOR_UNISON_STEREO_SPREAD_MENU_TITLE",
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
