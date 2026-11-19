# Specs for the host chain's table (src/effects/host_chain), which they check against the Deluge
# instrument's table.
deluge_specs_driver(host_chain_specs TREES host arm
  LIBRARIES deluge_host_chain deluge_instrument
  INCLUDES ${DELUGE_REPO_ROOT}/tests/support/mocks)
