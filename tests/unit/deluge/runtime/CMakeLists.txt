# Specs for deluge::runtime (src/deluge/runtime): the app loop's job list, against the real app_loop.cpp, with a fake
# clock.
deluge_specs_driver(runtime_specs TREES host
  SOURCES ${DELUGE_REPO_ROOT}/src/deluge/runtime/app_loop.cpp
  INCLUDES
    ${DELUGE_REPO_ROOT}/src/deluge  # runtime/app_loop.h
    ${DELUGE_REPO_ROOT}/include     # libdeluge/app.h
)
