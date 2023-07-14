set(CMAKE_SYSTEM_NAME               Generic)
set(CMAKE_SYSTEM_PROCESSOR          arm)

file(GLOB TOOLCHAIN_DIR "toolchain/**/arm-none-eabi-gcc")

set(ARM_TOOLCHAIN_ROOT ${TOOLCHAIN_DIR})
cmake_path(SET CMAKE_SYSROOT ${ARM_TOOLCHAIN_ROOT}/arm-none-eabi)
cmake_path(SET CMAKE_FIND_ROOT_PATH ${ARM_TOOLCHAIN_ROOT}/arm-none-eabi)
cmake_path(SET ARM_TOOLCHAIN_BIN_PATH ${ARM_TOOLCHAIN_ROOT}/bin)

# Without that flag CMake is not able to pass test compilation check
set(CMAKE_TRY_COMPILE_TARGET_TYPE   STATIC_LIBRARY)

if(WIN32)
  set(TOOLCHAIN_EXT ".exe" )
else()
  set(TOOLCHAIN_EXT "" )
endif()

cmake_path(SET CMAKE_AR                        ${ARM_TOOLCHAIN_BIN_PATH}/arm-none-eabi-ar${TOOLCHAIN_EXT})
cmake_path(SET CMAKE_ASM_COMPILER              ${ARM_TOOLCHAIN_BIN_PATH}/arm-none-eabi-gcc${TOOLCHAIN_EXT})
cmake_path(SET CMAKE_C_COMPILER                ${ARM_TOOLCHAIN_BIN_PATH}/arm-none-eabi-gcc${TOOLCHAIN_EXT})
cmake_path(SET CMAKE_CXX_COMPILER              ${ARM_TOOLCHAIN_BIN_PATH}/arm-none-eabi-g++${TOOLCHAIN_EXT})
cmake_path(SET CMAKE_LINKER                    ${ARM_TOOLCHAIN_BIN_PATH}/arm-none-eabi-ld${TOOLCHAIN_EXT})
cmake_path(SET CMAKE_OBJCOPY                   ${ARM_TOOLCHAIN_BIN_PATH}/arm-none-eabi-objcopy${TOOLCHAIN_EXT})
cmake_path(SET CMAKE_RANLIB                    ${ARM_TOOLCHAIN_BIN_PATH}/arm-none-eabi-ranlib${TOOLCHAIN_EXT})
cmake_path(SET CMAKE_SIZE                      ${ARM_TOOLCHAIN_BIN_PATH}/arm-none-eabi-size${TOOLCHAIN_EXT})
cmake_path(SET CMAKE_STRIP                     ${ARM_TOOLCHAIN_BIN_PATH}/arm-none-eabi-strip${TOOLCHAIN_EXT})

# Architecture
set(MCU
  -mcpu=cortex-a9
  -marm
  -mthumb-interwork
  -mlittle-endian
  -mfloat-abi=hard
  -mfpu=neon
)
add_compile_options(${MCU})
add_link_options(${MCU})

# Features
add_compile_options(
  -fdiagnostics-parseable-fixits
  -fmessage-length=0
  -fsigned-char
  -ffunction-sections
  -fdata-sections
)

add_link_options(
  -T ${PROJECT_SOURCE_DIR}/linker_script_rz_a1l.ld
  LINKER:--start-group
  LINKER:--end-group
  -nostartfiles
  LINKER:--gc-sections
  LINKER:-Map,Deluge-debug-oled.map
  -estart
#  --specs=nosys.specs
# --specs=nano.specs  
  --specs=rdimon.specs
)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
