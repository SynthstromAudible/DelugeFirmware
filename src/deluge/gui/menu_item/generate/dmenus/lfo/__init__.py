from dmui.dsl import Submenu
from . import lfo

menus = []

for i in range(4):
    menus.append(
        Submenu(
            "HorizontalMenu",
            f"lfo{i + 1}Menu",
            ["{name}", "%%CHILDREN%%"],
            "oscillator/index.md",
            lfo.byLfo[i],
            name=f"STRING_FOR_LFO{i + 1}",
        )
    )
