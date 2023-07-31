file(GLOB_RECURSE RZA1_SOURCES CONFIGURE_DEPENDS *.S *.c *.h *.cpp *.hpp)

target_sources(deluge INTERFACE ${RZA1_SOURCES})
# WILL BE RE-ENABLED WHEN HAVE_OLED IS EXCISED

# add_library(RZA1 STATIC ${RZA1_SOURCES})
# target_include_directories(RZA1 PRIVATE ${SHARED_INCLUDE})
# target_link_libraries(RZA1 PRIVATE fatfs RTT)

# if(ENABLE_RTT)
#   message(STATUS "RTT enabled for RZA1")
#
#   target_compile_definitions(RZA1 PRIVATE
#       $<$<CONFIG:DEBUG>:ENABLE_TEXT_OUTPUT=1>
#       $<$<CONFIG:DEBUG>:HAVE_RTT=1>
#   )
#
# endif(ENABLE_RTT)
