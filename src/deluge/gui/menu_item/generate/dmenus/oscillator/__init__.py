from dmui.dsl import *
from . import sample
from . import source
from ..common import file_selector

ty = Menu(
    "osc::Type",
    "oscTypeMenu",
    ["{name}", "{title}"],
    "oscillator/type.md",
    name="STRING_FOR_TYPE",
    title="STRING_FOR_OSC_TYPE_MENU_TITLE",
)

pulse_width = Menu(
    "osc::PulseWidth",
    "pulseWidthMenu",
    ["{name}", "{title}", "params::LOCAL_OSC_A_PHASE_WIDTH"],
    "oscillator/pulse_width.md",
    name="STRING_FOR_PULSE_WIDTH",
    title="STRING_FOR_OSC_P_WIDTH_MENU_TITLE",
    available_when="Voice is in subtractive or ring-mod mode and oscillator is not in a sample or input monitoring mode",
)

sync = Menu(
    "osc::Sync",
    "oscSyncMenu",
    ["{name}"],
    "oscillator/sync.md",
    name="STRING_FOR_OSCILLATOR_SYNC",
    available_when="Voice is in subtractive or ring-mod mode and oscillator 1 is in a synthesis mode.",
)

retrigger_phase = Menu(
    "osc::RetriggerPhase",
    "oscPhaseMenu",
    ["{name}", "{title}", "false"],
    "oscillator/retrigger_phase.md",
    name="STRING_FOR_RETRIGGER_PHASE",
    title="STRING_FOR_OSC_R_PHASE_MENU_TITLE",
    available_when="Voice is in FM mode, or the oscillator is not in sample mode",
)

menus = [
    Submenu(
        "submenu::ActualSource",
        f"source{i}Menu",
        ["{name}", "%%CHILDREN%%", str(i)],
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
        name=f"STRING_FOR_OSCILLATOR_{i+1}",
    )
    for i in range(2)
]
