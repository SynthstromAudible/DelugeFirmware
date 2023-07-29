file(GLOB_RECURSE RZA1_SOURCES CONFIGURE_DEPENDS *.S *.c *.h *.cpp *.hpp)

add_library(RZA1 STATIC ${RZA1_SOURCES})
target_include_directories(RZA1 PRIVATE 
  ${SHARED_INCLUDE}
)
target_link_libraries(RZA1 PRIVATE fatfs RTT)

add_library(RZA1_OLED STATIC ${RZA1_SOURCES})
target_include_directories(RZA1_OLED PRIVATE 
  ${SHARED_INCLUDE}
)
target_link_libraries(RZA1_OLED PRIVATE fatfs RTT)
target_compile_definitions(RZA1_OLED PUBLIC HAVE_OLED=1)
