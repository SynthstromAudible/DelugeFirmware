# Specs for the scenes' model-free parts: the menu's Node builders, id registry and validator over fake trees (menu/,
# against the fake shortcuts and bindings in menu/support), and view state and the peak retry (services/). The binding
# to the model has its own driver (bind/).
deluge_specs_driver(scenes_specs TREES host LIBRARIES deluge_spec deluge_midi deluge_ui_views_waveform)
