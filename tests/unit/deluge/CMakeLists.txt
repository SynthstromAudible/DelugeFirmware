# Specs for the app (src/deluge), each at its subsystem's path below this directory (util/, storage/, model/, gui/,
# ...), against deluge_spec's app sources and fakes. A subsystem directory with its own CMakeLists.txt builds its own
# driver.
# SOURCES: the app files only these specs reach (value scaling, name order, load steps, default names), compiled here
# rather than in deluge_spec, so the other drivers do not carry them.
deluge_specs_driver(app_specs TREES host
  SOURCES
    ${DELUGE_REPO_ROOT}/src/deluge/util/value_scaling.cpp
    ${DELUGE_REPO_ROOT}/src/deluge/util/name_compare.cpp
    ${DELUGE_REPO_ROOT}/src/deluge/gui/ui/load/confirm_step.cpp
    ${DELUGE_REPO_ROOT}/src/deluge/gui/ui/load/navigation_step.cpp
    ${DELUGE_REPO_ROOT}/src/deluge/gui/ui/browser/default_name.cpp
  LIBRARIES deluge_spec deluge_midi)
