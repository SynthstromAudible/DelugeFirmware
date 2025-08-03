from dmui.dsl import Menu

modulatorDest = Menu(
    "modulator::Destination",
    "modulatorDestMenu",
    ["{name}", "{title}"],
    "oscillator/modulator/destination.md",
    name="STRING_FOR_DESTINATION",
    title="STRING_FOR_FM_MOD2_DEST_MENU_TITLE",
)

byModulator = {}
volumes = []

for i in range(2):
    byModulator[i] = [
        Menu(
            "modulator::Transpose",
            f"modulator{i}TransposeMenu",
            ["{name}", "{title}", "params::LOCAL_MODULATOR_0_PITCH_ADJUST", f"{i}"],
            "oscillator/modulator/transpose.md",
            name="STRING_FOR_TRANSPOSE",
            title="STRING_FOR_FM_MOD_TRAN_MENU_TITLE",
        ),
        Menu(
            "source::patched_param::ModulatorFeedback",
            f"modulator{i}FeedbackMenu",
            ["{name}", "{title}", "params::LOCAL_MODULATOR_0_FEEDBACK", f"{i}"],
            "oscillator/modulator/feedback.md",
            name="STRING_FOR_FEEDBACK",
            title="STRING_FOR_FM_MOD_FBACK_MENU_TITLE",
        ),
        Menu(
            "osc::RetriggerPhase",
            f"modulator{i}PhaseMenu",
            ["{name}", "{title}", f"{i}", "true"],
            "oscillator/modulator/retrigger_phase.md",
            name="STRING_FOR_RETRIGGER_PHASE",
            title="STRING_FOR_FM_MOD_RETRIG_MENU_TITLE",
        ),
    ]

    volumes.append(
        Menu(
            "source::patched_param::ModulatorLevel",
            f"modulator{i}Volume",
            ["{name}", "params::LOCAL_MODULATOR_0_VOLUME", f"{i}"],
            "oscillator/modulator/volume.md",
            name="STRING_FOR_FM_MOD_LEVEL_MENU_TITLE",
        )
    )
