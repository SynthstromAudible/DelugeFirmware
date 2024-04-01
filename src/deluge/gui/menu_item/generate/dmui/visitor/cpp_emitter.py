from contextlib import contextmanager
from .visitor import Visitor
from .. import dsl


class CppEmitter(Visitor):
    """Visitor that emits C++ code"""

    def __init__(self, outf):
        """Initialize the emitter"""
        self.outf = outf
        self.indent = 0
        self.child_lists = {}

        self.outf.write("// clang-format off\n")

    #
    # Internal methods for writting out code
    #
    def emit_indent(self):
        """Emit whitespace to the current indent level"""
        for _ in range(self.indent):
            self.outf.write("    ")

    @contextmanager
    def emit_block(self):
        """Context manager for emitting a block"""
        self.outf.write("{\n")
        self.indent += 1

        try:
            yield None
        finally:
            self.indent -= 1
            self.emit_indent()
            self.outf.write("}")

    def emit_child_array(self, children):
        """Write out an array initialized with the addresses of the given children"""
        child_key = "".join([c.cpp_name for c in children])
        if child_key in self.child_lists:
            return self.child_lists[child_key]

        child_list_name = f"child{len(self.child_lists)}"

        self.emit_indent()
        self.outf.write("std::array<MenuItem*, ")
        self.outf.write(str(len(children)))
        self.outf.write("> ")
        self.outf.write(child_list_name)
        self.outf.write(" = ")

        with self.emit_block():
            for child in children:
                self.emit_indent()
                self.outf.write("&")
                self.outf.write(child.cpp_name)
                self.outf.write(",\n")

        self.outf.write(";\n")

        self.child_lists[child_key] = child_list_name
        return child_list_name

    def emit_args(self, args, template_args, child_key=None):
        first = True

        for arg in args:
            if first:
                first = False
            else:
                self.outf.write(", ")

            if arg == "%%CHILDREN%%":
                self.outf.write(child_key)
            else:
                self.outf.write(arg.format(**template_args))

    def finalize(self):
        self.outf.write("// clang-format on\n")

    #
    # Visitor methods
    #
    def visit_menu(self, menu: dsl.Menu):
        if hasattr(menu, "_cpp_emitted"):
            return menu
        menu._cpp_emitted = True

        self.emit_indent()
        self.outf.write(menu.clazz)
        self.outf.write(" ")
        self.outf.write(menu.cpp_name)
        self.outf.write("{")

        self.emit_args(menu.arg_template, menu.template_args())

        self.outf.write("};\n")

        return menu

    def visit_submenu(self, menu: dsl.Submenu, children):
        if hasattr(menu, "_cpp_emitted"):
            return menu
        menu._cpp_emitted = True

        child_key = self.emit_child_array(children)

        self.emit_indent()
        self.outf.write(menu.clazz)
        self.outf.write(" ")
        self.outf.write(menu.cpp_name)
        self.outf.write("{")

        # TODO: test if generating an explicit array has smaller code size than
        # the initializer list.
        self.emit_args(menu.arg_template, menu.template_args(), child_key=child_key)

        self.outf.write("};\n")

        return menu

    def visit_multimode(self, menu: dsl.MultiModeMenu, modes):
        self.visit_menu(menu)

        return menu

    def visit_multimode_mode(self, mode: dsl.MultiModeMenuMode):
        pass

    def visit_multicontext(self, menu: dsl.MultiContextMenu):
        self.visit_menu(menu)

        return menu

    def visit_multicontext_instance(self, instance: dsl.MultiContextMenuInstance):
        # make sure the menu is already emitted but otherwise do nothing special
        self.visit_menu(instance.parent)

        return instance
