# Specs for src/deluge/memory/, all run by this directory's driver: ObjectPool, the TrackingAllocator mock
# (tests/support/mocks/tracking_allocator.h) and MEM_GUARD's side table of live allocations. The table's
# structural-edit probe asks whether it runs on the render tier, which mock_hal.cpp answers; that file also defines
# getSystemTime(), as deluge_spec's fake legacy UI does, so mem_guard_spec cannot link into app_specs and the directory
# builds its own driver.
deluge_specs_driver(deluge_memory_specs TREES host
  SOURCES
    ${DELUGE_REPO_ROOT}/src/deluge/memory/alloc_metadata.cpp
    ${DELUGE_REPO_ROOT}/src/deluge/util/structural_edit_probe.cpp
    ${DELUGE_REPO_ROOT}/tests/support/mocks/mock_hal.cpp
    ${DELUGE_REPO_ROOT}/tests/support/mocks/mock_critical_section.cpp
    ${DELUGE_REPO_ROOT}/tests/support/mocks/mock_print.cpp
  INCLUDES
    ${DELUGE_REPO_ROOT}/src
    ${DELUGE_REPO_ROOT}/src/deluge
    ${DELUGE_REPO_ROOT}/include
)
