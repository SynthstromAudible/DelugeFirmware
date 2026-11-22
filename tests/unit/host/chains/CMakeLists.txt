# Specs for the host chain's table (src/host/chains), which they check against the Deluge
# instrument's table.
deluge_specs_driver(host_chains_specs TREES host arm
  LIBRARIES deluge_host_chains deluge_instruments_classic
  INCLUDES ${DELUGE_REPO_ROOT}/tests/support/mocks)
