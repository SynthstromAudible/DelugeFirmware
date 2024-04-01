import sys

__all__ = ["Menu", "Submenu", "MultiModeMenu", "MultiModeMenuMode"]


def trim(docstring):
    """Shamelessly stolen from PEP 257"""
    if not docstring:
        return ""
    # Convert tabs to spaces (following the normal Python rules)
    # and split into a list of lines:
    lines = docstring.expandtabs().splitlines()
    # Determine minimum indentation (first line doesn't count):
    indent = sys.maxsize
    for line in lines[1:]:
        stripped = line.lstrip()
        if stripped:
            indent = min(indent, len(line) - len(stripped))
    # Remove indentation (first line is special):
    trimmed = [lines[0].strip()]
    if indent < sys.maxsize:
        for line in lines[1:]:
            trimmed.append(line[indent:].rstrip())
    # Strip off trailing and leading blank lines:
    while trimmed and not trimmed[-1]:
        trimmed.pop()
    while trimmed and not trimmed[0]:
        trimmed.pop(0)
    # Return a single string:
    return "\n".join(trimmed)


class Menu:
    def __init__(
        self,
        clazz,
        cpp_name,
        arg_template,
        title,
        description,
        name=None,
        available_when=None,
    ):
        """
        A basic menu.

        Parameters:
            clazz: str
            The C++ class to use for this menu item.
        cpp_name: str
            The variable name to use when emitting C++ code.
        arg_template: list(str)
            A list of Python format strings which will be used to generate the
            C++ initializer arguments.
        title: str
            The key in the language map for the header of this menu item when
            it's opened.
        description: str
            Path to the description of this menu, relative to docs/menu
        name: str
            The key in the langauge map for the string to be rendered when this
            menu item is presented for selection in a submenu. A value of None
            (the default) causes this to default to the same value as `title`.
        available_when: str
            A human-readable description of when this menu item is available,
            if it's only available in certain contexts or when the system is in
            a particular state. A value of None (the default) indicates this
            menu item is always available.
        """
        self.cpp_name = cpp_name
        self.clazz = clazz
        self.title = title
        if name is None:
            self.name = title
        else:
            self.name = name
        self.arg_template = arg_template
        self.description = description
        if available_when is None:
            self.available_when = None
        else:
            self.available_when = trim(available_when)

    def template_args(self):
        return {"name": self.name, "title": self.title}

    def visit(self, visitor):
        return visitor.visit_menu(self)


class Submenu(Menu):
    def __init__(
        self,
        clazz,
        cpp_name,
        arg_template,
        title,
        description,
        children,
        name=None,
        available_when=None,
    ):
        """
        A menu capable of containing others.

        Parameters:
        clazz: str
            The C++ class to use for this menu item.
        cpp_name: str
            The variable name to use when emitting C++ code.
        arg_template: list(str)
            A list of Python format strings which will be used to generate the
            C++ initializer arguments. At least one of the argumnents must be
            '%%CHILDREN%%' which will be replaced by references to the children.
        title: str
            The key in the language map for the header of this menu item when
            it's opened.
        description: str
            Path to the description of this mode, relative to docs/menu
        children: list(Menu)
            The menus this submenu selects from.
        name: str
            The key in the langauge map for the string to be rendered when this
            menu item is presented for selection in a submenu. A value of None
            (the default) causes this to default to the same value as `title`.
        available_when: str
            A human-readable description of when this menu item is available,
            if it's only available in certain contexts or when the system is in
            a particular state. A value of None (the default) indicates this
            menu item is always available.
        """
        Menu.__init__(
            self,
            clazz,
            cpp_name,
            arg_template,
            title,
            description,
            available_when=available_when,
            name=name,
        )

        if type(children) != list:
            raise ValueError("Children must be a list")

        if "%%CHILDREN%%" not in arg_template:
            raise ValueError('Argument template does not include "%%CHILDREN%%')

        self.children = children

    def visit(self, visitor):
        children = [c.visit(visitor) for c in self.children]
        return visitor.visit_submenu(self, children)


class MultiModeMenu(Menu):
    def __init__(
        self,
        clazz,
        cpp_name,
        arg_template,
        title,
        modes,
        name=None,
        available_when=None,
    ):
        """A menu that changes its title and operation depending on context.

        Parameters:
        clazz: str
            The C++ class to use for this menu item.
        cpp_name: str
            The variable name to use when emitting C++ code.
        arg_template: list(str)
            A list of Python format strings which will be used to generate the
            C++ initializer arguments.
        title: str
            The key in the language map for the header of this menu item when
            it's opened.
        modes: list(MultiModeMenuItem)
            The menus this submenu selects from.
        name: str
            The key in the langauge map for the string to be rendered when this
            menu item is presented for selection in a submenu. A value of None
            (the default) causes this to default to the same value as `title`.
        available_when: str
            A human-readable description of when this menu item is available,
            if it's only available in certain contexts or when the system is in
            a particular state. A value of None (the default) indicates this
            menu item is always available.
        """
        Menu.__init__(
            self,
            clazz,
            cpp_name,
            arg_template,
            title,
            "",
            available_when=available_when,
            name=name,
        )

        self.modes = modes

    def visit(self, visitor):
        modes = [m.visit(visitor) for m in self.modes]
        return visitor.visit_multimode(self, modes)


class MultiModeMenuMode:
    def __init__(self, title, available_when, description, name=None):
        """A mode for a multimode menu item, containing information about when
        the mode is active and what the parameter means when it is active.

        title: str
            The key in the language map for the title used by the MultiModeMenu
            when in this mode.
        available_when: str
            Markdown fragment describing when this menu item is available.
        description: str
            Path to the description of this mode, relative to docs/menu
        name: str
            The key in the language map used to describe this menu item when
            presenting a submenu. A value of None (the default) causes this to
            default to the same value as `title`
        """
        self.title = title
        self.available_when = trim(available_when)
        self.description = description
        if name is None:
            self.name = title
        else:
            self.name = name

    def visit(self, visitor):
        return visitor.visit_multimode_mode(self)
