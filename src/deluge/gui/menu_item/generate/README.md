# Deluge Menu Compiler

This directory contains 2 python modules:
 - `dmui` is a DSL (Domain Specific Language) for specifying the Deluge menu hierarchies.
 - `dmenu` is the actual menu specification itself.

Unless you're adding new capabilities to the menu compiler, you should only need to add or change files in the `dmenu` module.

## High-Level Guidance
To add a new menu item, we recommend the "monkey see, monkey do" approach - find a menu item that behaves similar to the one you want to add, and copy-paste code until it works.
For those who prefer a more principled approach, the `dmui` module can be processed by the Sphinx documentation generator via the config and makefile in `src/deluge/gui/menu_item/generate/gendoc`.
What follows is a quick-reference version of that documentation.

The Python DSL does not map 1:1 to C++ menus -- instead, each Python class encompasses the broad-strokes behavior of the menu item.
To select the appropriate Python class for your new menu item, consider the following factors:

  - If the menu item is a `Submenu` child on the C++ side (i.e. it contains a list of other menus), use a `Submenu` on the Python side.
  - If the menu item is intended to be a child of multiple submenus and needs separate documentation for each context, use a `MultiContextMenu` to specify the menu and instantiate it using `with_context` in the child lists.
  - If the menu item behaves differently depending on system state, as though it's different menu items available only in certain contexts (e.g. filter morph), use a `MultiModeMenu`.
  - Otherwise, use just a `Menu` on the Python side.

## Implementation Details
The DSL is composed of Python objects that do some amount of sanity checking to ensure the generated menu hierarchy and documentation is well-formed.
To actually generate the various output types (C++ and documentation), we apply the visitor pattern with visitor implementations for each supported output type.
