cmake_minimum_required(VERSION 3.31)
project(MemmoveFuzz)

add_executable(memmove_fuzz memmove_fuzz.c)
set(options
  -mfpu=neon
  -mfloat-abi=hard
  -funsafe-math-optimizations
  -fsanitize=fuzzer #,undefined,signed-integer-overflow,null,alignment
  -fno-omit-frame-pointer
  -g
  -O1
)

target_compile_features(memmove_fuzz PUBLIC c_std_23)

target_compile_options(memmove_fuzz PUBLIC ${options})
target_link_options(memmove_fuzz PUBLIC ${options})
