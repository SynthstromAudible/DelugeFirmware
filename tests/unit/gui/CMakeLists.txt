# tests/unit/gui/CMakeLists.txt
#
# GUI specs that need no UI: the arithmetic the UI timer manager schedules its job with.
deluge_specs_driver(gui_specs
  INCLUDES
    ../../../src/deluge # gui/ui_timer_manager.h, runtime/app_loop.h
    ../../../src        # definitions_cxx.hpp
    ../../../include    # libdeluge headers definitions_cxx.hpp reaches
)
