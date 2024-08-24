from dmui.dsl import *

wave_index = Menu(
    "osc::source::WaveIndex",
    "sourceWaveIndexMenu",
    ["{name}", "{title}", "params::LOCAL_OSC_A_WAVE_INDEX"],
    "oscillator/wave_index.md",
    name="STRING_FOR_WAVE_INDEX",
    title="STRING_FOR_OSC_WAVE_IND_MENU_TITLE",
    available_when="Oscillator must be in wavetable mode",
)

feedback = Menu(
    "osc::source::Feedback",
    "sourceFeedbackMenu",
    ["{name}", "{title}", "params::LOCAL_CARRIER_0_FEEDBACK"],
    "oscillator/feedback.md",
    name="STRING_FOR_FEEDBACK",
    title="STRING_FOR_CARRIER_FEED_MENU_TITLE",
    available_when='Oscillator type must be <string-for name="STRING_FOR_FM">FM</string-for>',
)
