# The golden root: everything a spec compares with lives under tests/golden/<consumer>/ (tests/golden/README.md), and
# a spec names a golden by its path under GOLDEN_ROOT ("ui/gfx/oled/icons.pbm", "ui/menu_oracle/tables_1.tsv",
# "render/wavetable_pins/WTP_S8.WAV"). deluge_specs_driver gives it to every spec executable; a target that compiles
# specs outside a driver (tests/integration/ui/scenes's library) calls deluge_golden_root itself.
include_guard(GLOBAL)

# deluge_golden_root(<target>)
#
# Defines GOLDEN_ROOT, the golden root's absolute path without a trailing slash, for <target>'s own sources.
function(deluge_golden_root target)
  cmake_path(SET root NORMALIZE "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../../golden")
  target_compile_definitions(${target} PRIVATE GOLDEN_ROOT="${root}")
endfunction()
