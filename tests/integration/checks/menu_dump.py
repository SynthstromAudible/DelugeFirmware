"""The declarative menu tree, dumped by golden_vt_render's menu_dump scenario, must equal the committed
website/src/data/generated/menu-tree.json. `./dbt test --update ui-menu-dump` re-records it; regenerate the website's
menu-hierarchies.json from it afterwards (website/src/data/generated/README.md).

Gate ui-menu-dump:
    the declarative menu tree (every root of src/scenes/menu's node tables, with each node's type, l10n name and title,
    availability and the values it offers) is dumped as JSON by the app itself and must equal
    website/src/data/generated/menu-tree.json, so a menu change shows up as a diff of that file. ./dbt test --update
    ui-menu-dump regenerates it.
"""

from pathlib import Path

import golden_update

COMMITTED = Path("website/src/data/generated/menu-tree.json")


def run(gate) -> None:
    out = gate.tmp / "menu-tree.json"
    run = gate.run(env={"MENU_DUMP_OUT": str(out)})
    if not gate.require_standard(run):
        gate.show(run)
        return
    if not out.is_file() or out.stat().st_size == 0:
        gate.fail("the dump wrote nothing")
        return
    if golden_update.updating(gate.env):
        golden_update.write_bytes(
            gate.root / COMMITTED, out.read_bytes(), str(COMMITTED), env=gate.env
        )
        return
    if out.read_bytes() != (gate.root / COMMITTED).read_bytes():
        gate.fail(
            "menu-tree.json no longer matches the menu tree: ./dbt test --update ui-menu-dump re-records it "
            "(commit it with the reason)"
        )
