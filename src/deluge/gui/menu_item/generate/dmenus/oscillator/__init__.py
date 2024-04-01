from dmui.dsl import *
from . import sample
from . import source
from ..common import file_selector

ty = Menu(
    "osc::Type",
    "oscTypeMenu",
    ["{name}", "{title}"],
    "STRING_FOR_OSC_TYPE_MENU_TITLE",
    "oscillator/type.md",
    name="STRING_FOR_TYPE",
)

pulse_width = Menu(
    "osc::PulseWidth",
    "pulseWidthMenu",
    ["{name}", "{title}", "params::LOCAL_OSC_A_PHASE_WIDTH"],
    "STRING_FOR_OSC_P_WIDTH_MENU_TITLE",
    "oscillator/pulse_width.md",
    name="STRING_FOR_PULSE_WIDTH",
    available_when="Voice is in subtractive or ring-mod mode and oscillator is not in a sample or input monitoring mode",
)

sync = Menu(
    "osc::Sync",
    "oscSyncMenu",
    ["{name}"],
    "STRING_FOR_OSCILLATOR_SYNC",
    "oscillator/sync.md",
    available_when="Voice is in subtractive or ring-mod mode and oscillator 1 is in a synthesis mode.",
)

retrigger_phase = Menu(
    "osc::RetriggerPhase",
    "oscPhaseMenu",
    ["{name}", "{title}", "false"],
    "STRING_FOR_OSC_R_PHASE_MENU_TITLE",
    "oscillator/retrigger_phase.md",
    name="STRING_FOR_RETRIGGER_PHASE",
    available_when="Voice is in FM mode, or the oscillator is not in sample mode",
)

menus = [
    Submenu(
        "submenu::ActualSource",
        f"source{i}Menu",
        ["{name}", "%%CHILDREN%%", str(i)],
        f"STRING_FOR_OSCILLATOR_{i+1}",
        "oscillator/index.md",
        [
            ty,
            source.volume,
            source.wave_index,
            source.feedback,
            file_selector.with_context("oscillator/file_browser.md"),
            sample.recorder,
            sample.reverse,
            sample.repeat,
            sample.start,
            sample.end,
            sample.transpose,
            sample.pitch_speed,
            sample.timestretch,
            sample.interpolation,
            pulse_width,
            sync,
            retrigger_phase,
        ],
    )
    for i in range(2)
]
