"""The declarative menu tree, dumped by golden_vt_render's menu_dump scenario, must equal the committed
website/src/data/generated/menu-tree.json. scripts/menu_dump_check.sh regenerates it."""


def run(gate) -> None:
    out = gate.tmp / "menu-tree.json"
    run = gate.run(env={"MENU_DUMP_OUT": str(out)})
    if not gate.require_standard(run):
        gate.show(run)
        return
    committed = gate.root / "website/src/data/generated/menu-tree.json"
    if out.read_text() != committed.read_text():
        gate.fail(
            "menu-tree.json no longer matches the menu tree: run scripts/menu_dump_check.sh and commit the result"
        )
