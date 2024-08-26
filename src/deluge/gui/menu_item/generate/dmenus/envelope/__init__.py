from dmui.dsl import Submenu
from . import envelope

menus = []

for i in range(4):
    menus.append(
        Submenu(
            "envelope::EnvelopeMenu",
            f"env{i + 1}Menu",
            ["{name}", "%%CHILDREN%%", str(i)],
            "envelope/index.md",
            envelope.byEnvelope[i],
            name="STRING_FOR_ENVELOPE_MENU_TITLE",
        )
    )
