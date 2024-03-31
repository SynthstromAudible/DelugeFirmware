from .visitor import Visitor
from .. import dsl

class DocEmitter(Visitor):
    def __init__(self):
        pass

    def visit_menu(self, menu: dsl.Menu):
        tr = {
            'type': 'basic',
            'title': menu.title,
            'name': menu.name,
            'description': menu.description,
        }
        if menu.available_when is not None:
            tr['available_when'] = menu.available_when
        return tr

    def visit_submenu(self, menu: dsl.Submenu, children):
        tr = {
            'type': 'submenu',
            'title': menu.title,
            'name': menu.name,
            'description': menu.description,
            'children': children
        }
        if menu.available_when is not None:
            tr['available_when'] = menu.available_when
        return tr

    def visit_multimode(self, menu: dsl.MultiModeMenu, modes):
        tr = {
            'type': 'multimode',
            'modes': modes
        }
        return tr

    def visit_multimode_mode(self, mode: dsl.MultiModeMenuMode):
        tr = {
            'type': 'multimode-mode',
            'title': mode.title,
            'name': mode.name,
            'available_when': mode.available_when,
            'description': mode.description,
        }
        return tr
