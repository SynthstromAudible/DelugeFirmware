from dmui.dsl import Submenu
from . import recorder, modulator, source

menus = [
    Submenu(
        "HorizontalMenu",
        "recorderMenu",
        ["{name}", "%%CHILDREN%%"],
        "oscillator/sample/recorder.md",
        recorder.menus,
        name="STRING_FOR_SAMPLE_RECORDER",
    )
]

# recorder

# oscillator 1-2
for i in range(2):
    menus.append(
        Submenu(
            "submenu::ActualSource",
            f"source{i}Menu",
            ["{name}", "%%CHILDREN%%", str(i)],
            "oscillator/index.md",
            source.byOsc[i],
            name=f"STRING_FOR_OSCILLATOR_{i + 1}",
        )
    )

# modulator 1-2
for i in range(2):
    modulatorItems = modulator.byModulator[i]

    if i == 1:
        modulatorItems.append(modulator.modulatorDest)

    menus.append(
        Submenu(
            "submenu::Modulator",
            f"modulator{i}Menu",
            ["{name}", "%%CHILDREN%%"],
            "oscillator/index.md",
            modulatorItems,
            name=f"STRING_FOR_FM_MODULATOR_{i + 1}",
        )
    )

# oscillators mixer
menus.append(
    Submenu(
        "HorizontalMenu",
        "oscMixerMenu",
        ["{name}", "%%CHILDREN%%"],
        "oscillator/index.md",
        [
            *source.volumes,
            *modulator.volumes,
            source.noise,
            source.sync,
        ],
        name="STRING_FOR_OSCILLATOR_MIXER",
    )
)
