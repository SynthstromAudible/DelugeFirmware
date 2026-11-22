# Specs for the immediate-mode toolkit (src/ui/widgets: core/, oled/ and grid/), and two previews that print the OLED
# starfield and rainfall as ASCII, so their animations can be checked by eye without hardware. The previews assert
# nothing, and their file names avoid the *_spec.cpp glob, so they are no ctest tests.
deluge_specs_driver(ui_widgets_specs TREES host LIBRARIES deluge_spec deluge_midi)
add_executable(StarfieldPreview starfield_preview.cpp)
target_link_libraries(StarfieldPreview PRIVATE deluge_spec)
set_target_properties(StarfieldPreview PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED YES)
add_executable(RainfallPreview rainfall_preview.cpp)
target_link_libraries(RainfallPreview PRIVATE deluge_spec)
set_target_properties(RainfallPreview PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED YES)
