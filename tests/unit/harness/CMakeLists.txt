# Specs for the test harness itself (tests/harness): the level-0 script parser, the snapshot writers and their identity
# with the level-3 ones, the level-3 name tables against the shell's profile, and the re-record switch
# (tests/support/golden_update.h). The level-3 runner has its own driver (level3/).
deluge_specs_driver(harness_specs TREES host LIBRARIES deluge_spec deluge_midi)
