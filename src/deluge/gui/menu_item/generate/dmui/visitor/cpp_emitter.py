from contextlib import contextmanager
from .visitor import Visitor
from .. import dsl


class CppEmitter(Visitor):
    """Visitor that emits C++ code"""

    def __init__(self, outf):
        """Initialize the emitter"""
        self.outf = outf
        self.indent = 0

        self.outf.write('// clang-format off\n')

    #
    # Internal methods for writting out code
    #
    def emit_indent(self):
        """Emit whitespace to the current indent level"""
        for _ in range(self.indent):
            self.outf.write("\t")

    @contextmanager
    def emit_block(self):
        """ Context manager for emitting a block"""
        self.outf.write('{\n')
        self.indent += 1

        try:
            yield None
        finally:
            self.indent -= 1
            self.emit_indent()
            self.outf.write('}')

    def emit_args(self, args, template_args, children=None):
        multiline = children is not None
        if multiline:
            self.indent += 1
        for arg in args:
            if multiline:
                self.outf.write('\n')
                self.emit_indent()

            if arg == '%%CHILDREN%%':
                with self.emit_block():
                    for child in children:
                        self.emit_indent(),
                        self.outf.write('&')
                        self.outf.write(child.cpp_name)
                        self.outf.write(',\n')
            else:
                self.outf.write(arg.format(**template_args))

            if multiline:
                self.outf.write(',')
            else:
                self.outf.write(', ')
        if multiline:
            self.outf.write('\n')
            self.indent -= 1

    def finalize(self):
        self.outf.write('// clang-format on\n')

    #
    # Visitor methods
    #
    def visit_menu(self, menu: dsl.Menu):
        self.emit_indent()
        self.outf.write(menu.clazz)
        self.outf.write(" ")
        self.outf.write(menu.cpp_name)
        self.outf.write("{")

        self.emit_args(menu.arg_template, menu.template_args())

        self.outf.write("};\n")

        return menu

    def visit_submenu(self, menu: dsl.Submenu, children):
        self.emit_indent()
        self.outf.write(menu.clazz)
        self.outf.write(" ")
        self.outf.write(menu.cpp_name)
        self.outf.write("{")

        self.emit_args(menu.arg_template, menu.template_args(), children=children)

        self.outf.write("};\n")

        return menu

    def visit_multimode(self, menu: dsl.MultiModeMenu, modes):
        self.visit_menu(menu)

        return menu

    def visit_multimode_mode(self, mode: dsl.MultiModeMenuMode):
        pass
