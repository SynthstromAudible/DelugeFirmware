import sys
from pathlib import Path
from typing import Optional

__all__ = [
    "Menu",
    "MultiContextMenu",
    "MultiModeMenu",
    "MultiModeMenuMode",
    "Submenu",
]


DOC_PATH = Path(__file__).parent.joinpath(*([".."] * 6 + ["docs", "menus"])).resolve()


def ensure_doc_path_exists(path):
    """Checks that a documentation path exists"""
    doc_path = DOC_PATH.joinpath(*path.split("/"))
    if not doc_path.exists():
        raise ValueError(f'Doc file "{doc_path}" missing')
    if not doc_path.is_file():
        raise ValueError(f'Doc file "{doc_path}" is not a file')


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
    """Base class for all menus descriptors.

    You probably want a subclass of this class if the Menu on the C++ side has
    any special behavior:

    * If the menu contains others (it's a ``Submenu`` subclass in C++) then you
      probably want a :py:class:`Submenu` instead.
    * If the menu appears only in a single submenu but can change its name and
      behavior depending on other system state, you want a
      :py:class:`MultiModeMenu`
    * If the menu appears in multiple submenus but has a different meaning in
      each, you want to define a :py:class:`MultiContextMenu` somewhere and
      use :py:meth:`MultiContextMenu.with_context` to instantiate it when it's
      used.

    TODO - Additional support is needed for capturing the following pieces of
    documentaiton-relavant information:
      - Whether a given menu item supports MIDI mapping (this should also be
        integrated into codegen).
      - Whether a given menu item supports the kit "holding affect all"
        behavior (this would be nice to integrate into codegen but is harder).
      - Maybe some sort of stable identifier for linking purposes?

    :param clazz: The C++ class to use for this menu item.
    :param cpp_name: The variable name to use when emitting C++ code.
    :param arg_template: A list of Python format strings which will be used to
        generate the C++ initializer arguments.
    :param title: The key in the language map for the header of this menu item
        when it's opened.
    :param description: Path to the description of this menu, relative to
        ``docs/menu``. Can be None, for menus which expect their children to be
        rendered as siblings of themselves rather than rendering themselves.
    :param name: The key in the langauge map for the string to be rendered when
        this menu item is presented for selection in a submenu. A value of None
        (the default) causes this to default to the same value as ``title``.
    :param available_when: A human-readable description of when this menu item
        is available, if it's only available in certain contexts or when the
        system is in a particular state. A value of None (the default)
        indicates this menu item is always available.
    """

    def __init__(
        self,
        clazz: str,
        cpp_name: str,
        arg_template: list[str],
        title: str,
        description: Optional[str],
        name: Optional[str] = None,
        available_when: Optional[str] = None,
    ):
        if description is not None:
            ensure_doc_path_exists(description)

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


class MultiContextMenu(Menu):
    ...


class MultiContextMenuInstance(Menu):
    """Instance of a :py:class`MultiContextMenu`. Has seperate documentation,
    but otherwise inherits all properties from its parent.

    :param parent:
    :param doc: Path to the documentation for this instance
    """

    def __init__(self, parent: MultiContextMenu, doc: str):
        Menu.__init__(
            self, parent.clazz, parent.cpp_name, parent.arg_template, parent.title, doc
        )
        self.parent = parent

    def visit(self, visitor):
        return visitor.visit_multicontext_instance(self)


class MultiContextMenu(Menu):
    """A menu that changes its description based on the context it is
    instantiated in.

    Using this as a direct child of a Submenu is an error, you should instead
    use a :py:class:`MultiContextMenuInstance`.

    :param clazz: The C++ class to use for this menu item.
    :param cpp_name: The variable name to use when emitting C++ code.
    :param arg_template: A list of python format strings which will be used to
        generate the C++ initializer arguments.
    :param title: The key in the language map for the header of this menu item
        when it's opened.
    :param name: The key in the langauge map for the string to be rendered when
        this menu item is presented for selection in a submenu. A value of None
        (the default) causes this to default to the same value as ``title``.
    """

    def __init__(
        self,
        clazz: str,
        cpp_name: str,
        arg_template: list[str],
        title: str,
        name: Optional[str] = None,
    ):
        Menu.__init__(self, clazz, cpp_name, arg_template, title, None, name=name)

    def with_context(self, docs: str):
        """Instantiate this menu into a specific context.

        :param docs: Path to the docs describing what this menu means in this
            context.
        """
        return MultiContextMenuInstance(self, docs)

    def visit(self, visitor):
        return visitor.visit_multicontext(self)


class Submenu(Menu):
    """
    A menu capable of containing others.

    :param clazz: The C++ class to use for this menu item.
    :param cpp_name: The variable name to use when emitting C++ code.
    :param arg_template: A list of Python format strings which will be used to
        generate the C++ initializer arguments. At least one of the argumnents
        must be ``%%CHILDREN%%`` which will be replaced by references to the
        children.
    :param title: The key in the language map for the header of this menu item
        when it's opened.
    :param description: Path to the description of this mode, relative to
        ``docs/menu``
    :param children: The menus this submenu selects from.
    :param name: The key in the langauge map for the string to be rendered when
        this menu item is presented for selection in a submenu. A value of None
        (the default) causes this to default to the same value as `title`.
    :param available_when: A human-readable description of when this menu item
        is available, if it's only available in certain contexts or when the
        system is in a particular state. A value of None (the default)
        indicates this menu item is always available.
    """

    def __init__(
        self,
        clazz: str,
        cpp_name: str,
        arg_template: list[str],
        title: str,
        description: str,
        children: list[Menu],
        name: Optional[str] = None,
        available_when: Optional[str] = None,
    ):
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


class MultiModeMenuMode:
    """A mode for a multimode menu item, containing information about when
    the mode is active and what the parameter means when it is active.

    :param title: The key in the language map for the title used by the
        MultiModeMenu when in this mode.
    :param available_when: Markdown fragment describing when this menu item is
        available.
    :param description: Path to the description of this mode, relative to
        ``docs/menu``
    :param name: The key in the language map used to describe this menu item
        when presenting a submenu. A value of None (the default) causes this to
        default to the same value as ``title``
    """

    def __init__(
        self,
        title: str,
        available_when: str,
        description: str,
        name: Optional[str] = None,
    ):
        self.title = title
        self.available_when = trim(available_when)
        self.description = description
        if name is None:
            self.name = title
        else:
            self.name = name

    def visit(self, visitor):
        return visitor.visit_multimode_mode(self)


class MultiModeMenu(Menu):
    """A menu that changes its title and operation depending on context.

    :param clazz: The C++ class to use for this menu item.
    :param cpp_name: The variable name to use when emitting C++ code.
    :param arg_template: A list of Python format strings which will be used to
        generate the C++ initializer arguments.
    :param title: The key in the language map for the header of this menu item
        when it's opened.
    :param modes:  The menus this submenu selects from.
    :param name: The key in the langauge map for the string to be rendered when
        this menu item is presented for selection in a submenu. A value of None
        (the default) causes this to default to the same value as ``title``.
    """

    def __init__(
        self,
        clazz: str,
        cpp_name: str,
        arg_template: list[str],
        title: str,
        modes: list[MultiModeMenuMode],
        name: Optional[str] = None,
    ):
        Menu.__init__(self, clazz, cpp_name, arg_template, title, None, name=name)

        self.modes = modes

    def visit(self, visitor):
        modes = [m.visit(visitor) for m in self.modes]
        return visitor.visit_multimode(self, modes)
