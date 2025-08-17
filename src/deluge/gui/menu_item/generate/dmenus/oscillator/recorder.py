from dmui.dsl import Menu

menus = []

for i in range(2):
    menus.append(
        Menu(
            "osc::AudioRecorder",
            f"sample{i}RecorderMenu",
            ["{name}", f"{i}"],
            "oscillator/sample/recorder.md",
            name="STRING_FOR_RECORD_TO_SAMPLE",
        )
    )
