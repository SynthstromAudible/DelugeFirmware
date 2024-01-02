add_library(mpaland_printf STATIC)
target_include_directories(mpaland_printf PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_sources(mpaland_printf PRIVATE
  printf.c
)
