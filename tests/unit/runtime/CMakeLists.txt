# tests/unit/runtime/CMakeLists.txt
#
# deluge::runtime specs: the app loop's job list, against the real app_loop.cpp, with a fake clock.
deluge_specs_driver(runtime_specs
  SOURCES
    ../../../src/deluge/runtime/app_loop.cpp
  INCLUDES
    ../../../src/deluge # runtime/app_loop.h
)
