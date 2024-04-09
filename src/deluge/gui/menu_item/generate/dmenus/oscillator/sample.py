from dmui.dsl import *

_available_txt = 'Oscillator has its type set to <string-for name="STRING_FOR_SAMPLE">SAMPLE</string-for>'

recorder = Menu(
    "osc::AudioRecorder",
    "audioRecorderMenu",
    ["{name}"],
    "oscillator/sample/recorder.md",
    name="STRING_FOR_RECORD_AUDIO",
)

reverse = Menu(
    "sample::Reverse",
    "sampleReverseMenu",
    ["{name}", "{title}"],
    "oscillator/sample/reverse.md",
    name="STRING_FOR_REVERSE",
    title="STRING_FOR_SAMP_REVERSE_MENU_TITLE",
    available_when=_available_txt,
)

repeat = Menu(
    "sample::Repeat",
    "sampleRepeatMenu",
    ["{name}", "{title}"],
    "oscillator/sample/repeat.md",
    name="STRING_FOR_REPEAT_MODE",
    title="STRING_FOR_SAMP_REPEAT_MENU_TITLE",
    available_when=_available_txt,
)

start = Menu(
    "sample::Start",
    "sampleStartMenu",
    ["{name}"],
    "oscillator/sample/start_point.md",
    name="STRING_FOR_START_POINT",
    available_when=_available_txt,
)

end = Menu(
    "sample::End",
    "sampleEndMenu",
    ["{name}"],
    "oscillator/sample/end_point.md",
    name="STRING_FOR_END_POINT",
    available_when=_available_txt,
)

transpose = Menu(
    "sample::Transpose",
    "sourceTransposeMenu",
    ["{name}", "{title}", "params::LOCAL_OSC_A_PITCH_ADJUST"],
    "oscillator/sample/transpose.md",
    name="STRING_FOR_TRANSPOSE",
    title="STRING_FOR_OSC_TRANSPOSE_MENU_TITLE",
)

pitch_speed = Menu(
    "sample::PitchSpeed",
    "samplePitchSpeedMenu",
    ["{name}"],
    "oscillator/sample/pitch_speed.md",
    available_when=_available_txt,
    name="STRING_FOR_PITCH_SPEED",
)

timestretch = Menu(
    "sample::TimeStretch",
    "timeStretchMenu",
    ["{name}", "{title}"],
    "oscillator/sample/timestretch.md",
    name="STRING_FOR_SPEED",
    title="STRING_FOR_SAMP_SPEED_MENU_TITLE",
    available_when=_available_txt,
)

interpolation = Menu(
    "sample::Interpolation",
    "interpolationMenu",
    ["{name}", "{title}"],
    "oscillator/sample/interpolation.md",
    name="STRING_FOR_INTERPOLATION",
    title="STRING_FOR_SAMP_INTERP_MENU_TITLE",
    available_when="Only when a sample is loaded or monitoring an input",
)
