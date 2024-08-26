from dmui.dsl import *

_available_txt = 'Oscillator has its type set to <string-for name="STRING_FOR_SAMPLE">SAMPLE</string-for>'

sync = Menu(
    "osc::Sync",
    "oscSyncMenu",
    ["{name}"],
    "oscillator/sync.md",
    name="STRING_FOR_OSCILLATOR_SYNC",
    available_when="Voice is in subtractive or ring-mod mode and oscillator 1 is in a synthesis mode.",
)

noise = Menu(
    "patched_param::IntegerNonFM",
    "noiseMenu",
    ["{name}", "params::LOCAL_NOISE_VOLUME"],
    "oscillator/sync.md",
    name="STRING_FOR_NOISE_LEVEL",
)

byOsc = {}
volumes = []

for i in range(2):
    byOsc[i] = [
        Menu(
            "osc::Type",
            f"osc{i}TypeMenu",
            ["{name}", "{title}", f"{i}"],
            "oscillator/type.md",
            name="STRING_FOR_TYPE",
            title="STRING_FOR_OSC_TYPE_MENU_TITLE",
        ),
        Menu(
            "FileSelector",
            f"deluge::gui::menu_item::file{i}SelectorMenu",
            ["{name}", f"{i}"],
            "oscillator/file_browser.md",
            name="STRING_FOR_FILE_BROWSER",
        ),
        Menu(
            "sample::Transpose",
            f"source{i}TransposeMenu",
            ["{name}", "{title}", "params::LOCAL_OSC_A_PITCH_ADJUST", f"{i}"],
            "oscillator/sample/transpose.md",
            name="STRING_FOR_TRANSPOSE",
            title="STRING_FOR_OSC_TRANSPOSE_MENU_TITLE",
        ),
        Menu(
            "osc::source::WaveIndex",
            f"source{i}WaveIndexMenu",
            ["{name}", "{title}", "params::LOCAL_OSC_A_WAVE_INDEX", f"{i}"],
            "oscillator/wave_index.md",
            name="STRING_FOR_WAVE_INDEX",
            title="STRING_FOR_OSC_WAVE_IND_MENU_TITLE",
            available_when="Oscillator must be in wavetable mode",
        ),
        Menu(
            "osc::PulseWidth",
            f"osc{i}PulseWidthMenu",
            ["{name}", "{title}", "params::LOCAL_OSC_A_PHASE_WIDTH", f"{i}"],
            "oscillator/pulse_width.md",
            name="STRING_FOR_PULSE_WIDTH",
            title="STRING_FOR_OSC_P_WIDTH_MENU_TITLE",
            available_when="Voice is in subtractive or ring-mod mode and oscillator is not in a sample or input monitoring mode",
        ),
        Menu(
            "osc::source::Feedback",
            f"source{i}FeedbackMenu",
            ["{name}", "{title}", "params::LOCAL_CARRIER_0_FEEDBACK", f"{i}"],
            "oscillator/feedback.md",
            name="STRING_FOR_FEEDBACK",
            title="STRING_FOR_CARRIER_FEED_MENU_TITLE",
            available_when='Oscillator type must be <string-for name="STRING_FOR_FM">FM</string-for>',
        ),
        Menu(
            "osc::RetriggerPhase",
            f"osc{i}PhaseMenu",
            ["{name}", "{title}", f"{i}", "false"],
            "oscillator/retrigger_phase.md",
            name="STRING_FOR_RETRIGGER_PHASE",
            title="STRING_FOR_OSC_R_PHASE_MENU_TITLE",
            available_when="Voice is in FM mode, or the oscillator is not in sample mode",
        ),
        Menu(
            "sample::Reverse",
            f"sample{i}ReverseMenu",
            ["{name}", "{title}", f"{i}"],
            "oscillator/sample/reverse.md",
            name="STRING_FOR_REVERSE",
            title="STRING_FOR_SAMP_REVERSE_MENU_TITLE",
            available_when=_available_txt,
        ),
        Menu(
            "sample::Repeat",
            f"sample{i}RepeatMenu",
            ["{name}", "{title}", f"{i}"],
            "oscillator/sample/repeat.md",
            name="STRING_FOR_REPEAT_MODE",
            title="STRING_FOR_SAMP_REPEAT_MENU_TITLE",
            available_when=_available_txt,
        ),
        Menu(
            "sample::TimeStretch",
            f"sample{i}TimeStretchMenu",
            ["{name}", "{title}", f"{i}"],
            "oscillator/sample/timestretch.md",
            name="STRING_FOR_SPEED",
            title="STRING_FOR_SAMP_SPEED_MENU_TITLE",
            available_when=_available_txt,
        ),
        Menu(
            "sample::PitchSpeed",
            f"sample{i}PitchSpeedMenu",
            ["{name}", f"{i}"],
            "oscillator/sample/pitch_speed.md",
            available_when=_available_txt,
            name="STRING_FOR_PITCH_SPEED",
        ),
        Menu(
            "sample::Interpolation",
            f"sample{i}InterpolationMenu",
            ["{name}", "{title}", f"{i}"],
            "oscillator/sample/interpolation.md",
            name="STRING_FOR_INTERPOLATION",
            title="STRING_FOR_SAMP_INTERP_MENU_TITLE",
            available_when="Only when a sample is loaded or monitoring an input",
        ),
        Menu(
            "sample::Start",
            f"sample{i}StartMenu",
            ["{name}", f"{i}"],
            "oscillator/sample/start_point.md",
            name="STRING_FOR_START_POINT",
            available_when=_available_txt,
        ),
        Menu(
            "sample::End",
            f"sample{i}EndMenu",
            ["{name}", f"{i}"],
            "oscillator/sample/end_point.md",
            name="STRING_FOR_END_POINT",
            available_when=_available_txt,
        ),
    ]

    volumes.append(
        Menu(
            "osc::source::Volume",
            f"source{i}VolumeMenu",
            ["{title}", "params::LOCAL_OSC_A_VOLUME", f"{i}"],
            "oscillator/volume.md",
            title="STRING_FOR_OSC_LEVEL_MENU_TITLE",
            available_when='Oscillator type must not be <string-for name="STRING_FOR_RINGMOD">RINGMOD</string-for>',
        )
    )
