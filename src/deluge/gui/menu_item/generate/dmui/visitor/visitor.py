from abc import ABC, abstractmethod
from .. import dsl


class Visitor(ABC):
    @abstractmethod
    def visit_menu(self, menu: dsl.Menu):
        pass

    @abstractmethod
    def visit_submenu(self, menu: dsl.Submenu, children):
        pass

    @abstractmethod
    def visit_multimode(self, menu: dsl.MultiModeMenu, modes):
        pass

    @abstractmethod
    def visit_multimode_mode(self, mode: dsl.MultiModeMenuMode):
        pass
