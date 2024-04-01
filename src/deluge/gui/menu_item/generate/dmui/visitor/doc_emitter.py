from .visitor import Visitor
from .. import dsl
from pathlib import Path
import os.path as path

BASE_PATH = Path(path.abspath(__file__)).parent
DOC_ROOT = BASE_PATH.joinpath(*([".."] * 7 + ["docs", "menus"])).resolve()


def read_description(description):
    with open(DOC_ROOT / description) as inf:
        return inf.read()


class DocEmitter(Visitor):
    def __init__(self):
        pass

    def visit_menu(self, menu: dsl.Menu):
        tr = {
            "type": "basic",
            "title": menu.title,
            "name": menu.name,
            "description": menu.description,
        }
        if menu.available_when is not None:
            tr["available_when"] = menu.available_when
        return tr

    def visit_submenu(self, menu: dsl.Submenu, children):
        tr = {
            "type": "submenu",
            "title": menu.title,
            "name": menu.name,
            "description": menu.description,
            "children": children,
        }
        if menu.available_when is not None:
            tr["available_when"] = menu.available_when
        return tr

    def visit_multimode(self, menu: dsl.MultiModeMenu, modes):
        tr = {"type": "multimode", "modes": modes}
        return tr

    def visit_multimode_mode(self, mode: dsl.MultiModeMenuMode):
        tr = {
            "type": "multimode-mode",
            "title": mode.title,
            "name": mode.name,
            "available_when": mode.available_when,
            "description": mode.description,
        }
        return tr

    def visit_multicontext(self, menu: dsl.MultiContextMenu):
        raise ValueError(
            f"Failed to process menu {menu.cpp_name}: "
            "Doc emitter can only consume MultiContextMenuInstances, not the "
            "underlying MultiContextMenu. Did you forget a .with_context()?"
        )

    def visit_multicontext_instance(self, instance: dsl.MultiContextMenuInstance):
        return {
            "type": "multicontext",
            "title": instance.parent.title,
            "name": instance.parent.name,
            "description": instance.description,
        }
