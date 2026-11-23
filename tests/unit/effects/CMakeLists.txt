# Specs for the effects (src/effects), built against the standalone libraries' include roots only, so a hidden
# dependency on the app fails this build. They link the DSP library (deluge_dsp_fixed), whose filter set, compressor, duck
# envelope and delay the effects run, and the app code the DSP library reaches (deluge_test_dsp_closure: the lookup tables, the
# noise stream, the panic mock). The DSP library specs' workloads and references (tests/unit/dsp) are included
# by name. NEON: the sidechain's duck envelope runs through Control, whose kernel core includes argon.
deluge_specs_driver(effects_specs TREES host arm NEON
  LIBRARIES deluge_effects deluge_dsp_fixed deluge_test_dsp_closure
  INCLUDES ${DELUGE_REPO_ROOT}/tests/support/mocks ${DELUGE_REPO_ROOT}/tests/unit/dsp)
