from dmui.dsl import *

file_selector = MultiContextMenu(
    "FileSelector",
    "deluge::gui::menu_item::fileSelectorMenu",
    ["{name}"],
    "STRING_FOR_FILE_BROWSER",
)
