include_guard(GLOBAL)

# deluge_library_warnings(<target>)
#
# The warnings the libraries below the app hold themselves to, as errors: -Wall, -Wextra and -Wpedantic, plus
# shadowing, an undefined macro in #if, a float silently widened to double (a software-slow path on the A9) and an
# unannotated switch fall-through. Not -Wextra's missing-designated-field-initializers: a field a designated
# initialiser leaves out is value-initialised, and leaving defaults out is the idiom. PRIVATE, so an app target that
# links a library keeps the app's own flags.
function(deluge_library_warnings target)
  target_compile_options(${target} PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Wshadow
    -Wundef
    -Wdouble-promotion
    -Wimplicit-fallthrough
    -Wno-missing-designated-field-initializers
    -Werror
  )
endfunction()
