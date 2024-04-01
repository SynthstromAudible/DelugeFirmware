from dmui.dsl import *

_available_txt = 'Oscillator has its type set to <string-for name="STRING_FOR_SAMPLE">SAMPLE</string-for>'

recorder = Menu(
    "osc::AudioRecorder",
    "audioRecorderMenu",
    ["{name}"],
    "STRING_FOR_RECORD_AUDIO",
    "oscillator/sample/recorder.md",
)

reverse = Menu(
    "sample::Reverse",
    "sampleReverseMenu",
    ["{name}", "{title}"],
    "STRING_FOR_SAMP_REVERSE_MENU_TITLE",
    "oscillator/sample/reverse.md",
    name="STRING_FOR_REVERSE",
    available_when=_available_txt,
)

repeat = Menu(
    "sample::Repeat",
    "sampleRepeatMenu",
    ["{name}", "{title}"],
    "STRING_FOR_SAMP_REPEAT_MENU_TITLE",
    "oscillator/sample/repeat.md",
    name="STRING_FOR_REPEAT_MODE",
    available_when=_available_txt,
)

start = Menu(
    "sample::Start",
    "sampleStartMenu",
    ["{name}"],
    "STRING_FOR_START_POINT",
    "oscillator/sample/start_point.md",
    available_when=_available_txt,
)

end = Menu(
    "sample::End",
    "sampleEndMenu",
    ["{name}"],
    "STRING_FOR_END_POINT",
    "oscillator/sample/end_point.md",
    available_when=_available_txt,
)

transpose = Menu(
    "sample::Transpose",
    "sourceTransposeMenu",
    ["{name}", "{title}", "params::LOCAL_OSC_A_PITCH_ADJUST"],
    "STRING_FOR_OSC_TRANSPOSE_MENU_TITLE",
    "oscillator/sample/transpose.md",
    name="STRING_FOR_TRANSPOSE",
)

pitch_speed = Menu(
    "sample::PitchSpeed",
    "samplePitchSpeedMenu",
    ["{name}"],
    "STRING_FOR_PITCH_SPEED",
    "oscillator/sample/pitch_speed.md",
    available_when=_available_txt,
)

timestretch = Menu(
    "sample::TimeStretch",
    "timeStretchMenu",
    ["{name}", "{title}"],
    "STRING_FOR_SAMP_SPEED_MENU_TITLE",
    "oscillator/sample/timestretch.md",
    name="STRING_FOR_SPEED",
    available_when=_available_txt,
)

interpolation = Menu(
    "sample::Interpolation",
    "interpolationMenu",
    ["{name}", "{title}"],
    "STRING_FOR_SAMP_INTERP_MENU_TITLE",
    "oscillator/sample/interpolation.md",
    name="STRING_FOR_INTERPOLATION",
    available_when="Only when a sample is loaded or monitoring an input",
)
