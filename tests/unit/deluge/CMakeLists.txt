# Specs for the app (src/deluge), each at its subsystem's path below this directory (util/, storage/, model/, gui/,
# ...), against deluge_spec's app sources and fakes. A subsystem directory with its own CMakeLists.txt builds its own
# driver.
# SOURCES: the app files only these specs reach (value scaling, name order, load steps, default names, chord
# voicings, the external clock's output schedule, iterance and its legacy migration with the preset tables it reads,
# the DX7 cartridge's sysex parser), compiled here rather than in deluge_spec, so the other drivers do not carry them.
# The chord voicings are note sets, so the driver links deluge_music.
# The song-preview deferral spec reads the load UI's sources under DELUGE_REPO_ROOT, which deluge_spec defines.
deluge_specs_driver(app_specs TREES host
  SOURCES
    ${DELUGE_REPO_ROOT}/src/deluge/util/value_scaling.cpp
    ${DELUGE_REPO_ROOT}/src/deluge/util/name_compare.cpp
    ${DELUGE_REPO_ROOT}/src/deluge/gui/ui/load/confirm_step.cpp
    ${DELUGE_REPO_ROOT}/src/deluge/gui/ui/load/navigation_step.cpp
    ${DELUGE_REPO_ROOT}/src/deluge/gui/ui/browser/default_name.cpp
    ${DELUGE_REPO_ROOT}/src/deluge/gui/ui/keyboard/chords.cpp
    ${DELUGE_REPO_ROOT}/src/deluge/playback/clock_output_scheduler.cpp
    ${DELUGE_REPO_ROOT}/src/deluge/sequence/iterance_migration.cpp
    ${DELUGE_REPO_ROOT}/src/deluge/util/lookuptables/preset_tables.cpp
    ${DELUGE_REPO_ROOT}/src/deluge/storage/DX7Cartridge.cpp
  LIBRARIES deluge_spec deluge_midi deluge_music deluge_project)
