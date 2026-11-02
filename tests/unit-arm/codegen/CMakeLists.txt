# Every fixed-point operation must compile to the instruction its comment names, at the codegen flags the shipping
# image's TUs use: app TUs are Thumb-2, dsp-next's are ARM, both at -O2 with -fno-inline-functions. The image is
# bare metal, so neither the Linux toolchain's stack protector nor its PIC applies.
set(_fixed_point_probe_options
  -O2
  -mcpu=cortex-a9
  -mfpu=neon-fp16
  -mfloat-abi=hard
  -funsafe-math-optimizations
  -fno-inline-functions
  -ffunction-sections
  -fsigned-char
  -fno-stack-protector
  -fno-pic
)
foreach(_mode arm thumb)
  add_library(fixed_point_probe_${_mode} OBJECT fixed_point_probe.cpp)
  target_include_directories(fixed_point_probe_${_mode} PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../../../src/deluge)
  target_compile_options(fixed_point_probe_${_mode} PRIVATE ${_fixed_point_probe_options} -m${_mode})
  set_target_properties(fixed_point_probe_${_mode} PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED YES
                                                              POSITION_INDEPENDENT_CODE OFF)
endforeach()

find_program(LLVM_OBJDUMP NAMES llvm-objdump REQUIRED)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME fixed_point_codegen
         COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_LIST_DIR}/check_codegen.py ${LLVM_OBJDUMP}
                 $<TARGET_OBJECTS:fixed_point_probe_arm> $<TARGET_OBJECTS:fixed_point_probe_thumb>)
