cmake_policy(SET CMP0135 NEW)

include(FetchContent)

FetchContent_Declare(argon
        GIT_REPOSITORY https://github.com/stellar-aria/argon
        GIT_TAG 3779c5315a978a2f8c892e320ac12ecbb77c3759
)
FetchContent_MakeAvailable(argon)

FetchContent_Declare(
        etl
        GIT_REPOSITORY https://github.com/ETLCPP/etl
        GIT_TAG 20.40.0
)

FetchContent_MakeAvailable(etl)

# tinyusb
FetchContent_Declare(tinyusb
  URL https://github.com/hathach/tinyusb/archive/refs/tags/0.17.0.tar.gz
  SOURCE_SUBDIR src
)

add_library(tinyusb_config INTERFACE)
target_include_directories(tinyusb_config INTERFACE tinyusb)

FetchContent_MakeAvailable(tinyusb)

add_library(tinyusb-rusb2)
target_sources(tinyusb-rusb2 PUBLIC
    ${tinyusb_SOURCE_DIR}/src/portable/renesas/rusb2/dcd_rusb2.c
    ${tinyusb_SOURCE_DIR}/src/portable/renesas/rusb2/hcd_rusb2.c
    ${tinyusb_SOURCE_DIR}/src/portable/renesas/rusb2/rusb2_common.c
)
target_link_libraries(tinyusb-rusb2 PUBLIC
    tinyusb
    tinyusb_config)
