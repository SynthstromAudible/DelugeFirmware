from contextlib import contextmanager
from .visitor import Visitor
from .. import dsl


class CppEmitter(Visitor):
    """Visitor that emits C++ code"""

    def __init__(self, outf):
        """ Initialize the emitter """
        self.outf = outf
        self.indent = 0

    #
    # Internal methods for writting out code
    #
    def emit_indent(self):
        """ Emit whitespace to the current indent level """
        for _ in range(self.indent):
            self.outf.write("\t")

    #
    # Visitor methods
    #
    def visit_menu(self, menu: dsl.Menu):
        self.emit_indent();
        self.outf.write(menu.clazz)
        self.outf.write(' ')
        self.outf.write(menu.cpp_name)
        self.outf.write('{')
        self.outf.write(menu.arg_template.format(**menu.template_args()))
        self.outf.write('};\n')

        return menu

    def visit_submenu(self, menu: dsl.Submenu, children):
        self.emit_indent();
        self.outf.write(menu.clazz)
        self.outf.write(' ')
        self.outf.write(menu.cpp_name)
        self.outf.write('{')
        args = menu.template_args()

        args['children'] = '{' + ', '.join(map(lambda c: f'&{c.cpp_name}', children)) + '}'

        self.outf.write(menu.arg_template.format(**args))
        self.outf.write('};\n')

        return menu

    def visit_multimode(self, menu: dsl.MultiModeMenu, modes):
        self.visit_menu(menu)

        return menu

    def visit_multimode_mode(self, mode: dsl.MultiModeMenuMode):
        pass
