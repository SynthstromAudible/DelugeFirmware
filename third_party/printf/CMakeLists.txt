add_library(eyalroz_printf STATIC)
target_include_directories(eyalroz_printf PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_sources(eyalroz_printf PRIVATE
  printf.c
)
