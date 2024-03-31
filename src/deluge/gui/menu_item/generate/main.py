from dmui.dsl import *
from dmui.visitor import CppEmitter, DocEmitter
import sys

lpf_freq = Menu(
    "filter::LPFFreq",
    "lpfFreqMenu",
    "{name} {title}, params::LOCAL_LPF_FREQ",
    "STRING_FOR_LPF_FREQUENCY",
    """Low-pass filter frequency.

    TODO: describe what frequencies 0 and 50 represent.
    """,
    name="STRING_FOR_FREQUENCY",
)

lpf_res = Menu(
    "patched_param::IntegerNonFM",
    "lpfResMenu",
    "{name} {title}, params::LOCAL_LPF_RESONANCE",
    "STRING_FOR_LPF_RESONANCE",
    """Low-pass filter resonance.

    TODO: describe what the resonance is actually emulating, and the meaning of
    the 0-50 range.
    """,
    name="STRING_FOR_RESONANCE",
)

lpf_mode = Menu(
    "filter::LPFMode",
    "lpfModeMenu",
    "{name}, {title}",
    "STRING_FOR_LPF_MODE",
    """Low-pass filter mode.

    Can be one of
      - <string-for string="STRING_FOR_12DB_LADDER"> 12db Ladder </string-for>
      - <string-for string="STRING_FOR_24DB_LADDER"> 24db Ladder </string-for>
      - <string-for string="STRING_FOR_DRIVE"> Drive </string-for>
      - <string-for string="STRING_FOR_SVF_BAND"> SVF Bandpass </string-for>
      - <string-for string="STRING_FOR_SVF_NOTCH"> SVF Notch </string-for>
    """,
    name="STRING_FOR_MODE",
)

lpf_morph = MultiModeMenu(
    "filter::FilterMorph",
    "lpfMorphMenu",
    "{title}, params::LOCAL_LPF_MORPH, false",
    "STRING_FOR_MORPH",
    [
        MultiModeMenuMode(
            "STRING_FOR_DRIVE",
            "LPF is in a ladder mode",
            """Ladder filter drive. Drive emulates the effects of overdriving a
            diode ladder filter.""",
        ),
        MultiModeMenuMode(
            "STRING_FOR_MORPH",
            "LPF is in an SVF mode",
            """State-variable filter morph. Controls morphing between low-pass
            and high-pass.

            A value of 0 means fully low-pass, while a value of 50 represents
            fully high-pass.
            """,
        ),
    ],
)

lpf_menu = Submenu(
    "submenu::Filter",
    "lpfMenu",
    "{title}, {children}",
    "STRING_FOR_LPF",
    "Low-pass filter controls",
    [lpf_freq, lpf_res, lpf_mode, lpf_morph],
    name="STRING_FOR_LPF",
)

def main():
    emitter = CppEmitter(sys.stdout)
    # emitter = DocEmitter()
    result = lpf_menu.visit(emitter)

    # import json
    # print(json.dumps(result))


if __name__ == "__main__":
    main()
