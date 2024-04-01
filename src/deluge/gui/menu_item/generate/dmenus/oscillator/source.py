from dmui.dsl import *

wave_index = Menu(
    "osc::source::WaveIndex",
    "sourceWaveIndexMenu",
    ["{name}", "{title}", "params::LOCAL_OSC_A_WAVE_INDEX"],
    "STRING_FOR_OSC_WAVE_IND_MENU_TITLE",
    "oscillator/wave_index.md",
    name="STRING_FOR_WAVE_INDEX",
    available_when="Oscillator must be in wavetable mode",
)

volume = Menu(
    "osc::source::Volume",
    "sourceVolumeMenu",
    ["{name}", "{title}", "params::LOCAL_OSC_A_VOLUME"],
    "STRING_FOR_OSC_LEVEL_MENU_TITLE",
    "oscillator/volume.md",
    name="STRING_FOR_VOLUME_LEVEL",
    available_when='Oscillator type must not be <string-for name="STRING_FOR_RINGMOD">RINGMOD</string-for>',
)

feedback = Menu(
    "osc::source::Feedback",
    "sourceFeedbackMenu",
    ["{name}", "{title}", "params::LOCAL_CARRIER_0_FEEDBACK"],
    "STRING_FOR_CARRIER_FEED_MENU_TITLE",
    "oscillator/feedback.md",
    name="STRING_FOR_FEEDBACK",
    available_when='Oscillator type must be <string-for name="STRING_FOR_FM">FM</string-for>',
)
