# Specs for the app (src/deluge), each at its subsystem's path below this directory (util/, storage/, model/, gui/,
# ...), against deluge_spec's app sources and fakes. A subsystem directory with its own CMakeLists.txt builds its own
# driver.
deluge_specs_driver(app_specs TREES host LIBRARIES deluge_spec deluge_midi)
