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
)

add_library(tinyusb_config INTERFACE)
target_include_directories(tinyusb_config INTERFACE ${CMAKE_CURRENT_LIST_DIR}/tinyusb)

FetchContent_MakeAvailable(tinyusb)

add_library(tinyusb)
target_sources(tinyusb PRIVATE
    ${tinyusb_SOURCE_DIR}/src/tusb.c
    ${tinyusb_SOURCE_DIR}/src/common/tusb_fifo.c
    ${tinyusb_SOURCE_DIR}/src/device/usbd.c
    ${tinyusb_SOURCE_DIR}/src/device/usbd_control.c
    ${tinyusb_SOURCE_DIR}/src/class/midi/midi_device.c
)

target_include_directories(tinyusb PUBLIC
    ${tinyusb_SOURCE_DIR}/src
    ${CMAKE_CURRENT_LIST_DIR}/../src/RZA1/system
    ${CMAKE_CURRENT_LIST_DIR}/../src/
)

target_link_libraries(tinyusb PUBLIC
    tinyusb_config
)

add_library(tinyusb-rusb2)
target_sources(tinyusb-rusb2 PRIVATE
    ${tinyusb_SOURCE_DIR}/src/portable/renesas/rusb1/dcd_rusb1.c
    ${tinyusb_SOURCE_DIR}/src/portable/renesas/rusb1/hcd_rusb1.c
    ${tinyusb_SOURCE_DIR}/src/portable/renesas/rusb1/rusb1_common.c
)
target_link_libraries(tinyusb-rusb2 PUBLIC
    tinyusb
    tinyusb_config
)
