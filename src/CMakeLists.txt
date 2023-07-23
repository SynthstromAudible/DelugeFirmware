# Normally this would be add_executable, but since we're using add_firmware,
# we use an interface library to simply collect all the source files we care
# about before creating the real build targets
#
# Once the unified display system/one-bin-file build is done, this can be
# changed to add_executable
add_library(DelugeFirmware INTERFACE)
target_include_directories(DelugeFirmware INTERFACE ${CMAKE_CURRENT_LIST_DIR})
target_sources(DelugeFirmware PUBLIC main.c resetprg.c)

add_subdirectory(deluge)

target_link_libraries(DelugeFirmware INTERFACE
    FatFs
    RZA1
    RTT
    NE10
)

add_library(RZA1 STATIC
    # RZA1-specific code
    RZA1/bsc/bsc_userdef.c
    RZA1/cache/cache.c
    RZA1/diskio.c
    RZA1/gpio/gpio.c
    RZA1/intc/intc.c
    RZA1/intc/intc_handler.c
    RZA1/intc/intc_userdef.c
    RZA1/mtu/mtu.c
    RZA1/oled/oled_low_level.c
    RZA1/peripheral_init_basic.c
    RZA1/rspi/rspi.c
    RZA1/sdhi/src/sd/access/sd_cd.c
    RZA1/sdhi/src/sd/access/sd_cmd.c
    RZA1/sdhi/src/sd/access/sd_format.c
    RZA1/sdhi/src/sd/access/sd_init.c
    RZA1/sdhi/src/sd/access/sd_int.c
    RZA1/sdhi/src/sd/access/sd_mount.c
    RZA1/sdhi/src/sd/access/sd_read.c
    RZA1/sdhi/src/sd/access/sd_trns.c
    RZA1/sdhi/src/sd/access/sd_upcase_table.c
    RZA1/sdhi/src/sd/access/sd_util.c
    RZA1/sdhi/src/sd/access/sd_write.c
    RZA1/sdhi/src/sd/sdio/sdio_direct.c
    RZA1/sdhi/src/sd/sdio/sdio_int.c
    RZA1/sdhi/src/sd/sdio/sdio_read.c
    RZA1/sdhi/src/sd/sdio/sdio_trns.c
    RZA1/sdhi/src/sd/sdio/sdio_util.c
    RZA1/sdhi/src/sd/sdio/sdio_write.c
    RZA1/sdhi/userdef/sd_dev_dmacdrv.c
    RZA1/sdhi/userdef/sd_dev_low.c
    RZA1/spibsc/spibsc_Deluge_setup.c
    RZA1/spibsc/spibsc_flash_api.c
    RZA1/spibsc/spibsc_flash_userdef.c
    RZA1/spibsc/spibsc_ioset_api.c
    RZA1/spibsc/spibsc_ioset_drv.c
    RZA1/spibsc/spibsc_ioset_userdef.c
    RZA1/ssi/ssi.c
    RZA1/stb/stb.c
    RZA1/system/rza_io_regrw.c
    RZA1/uart/sio_char.c
    RZA1/usb/r_usb_basic/src/driver/r_usb_cdataio.c
    RZA1/usb/r_usb_basic/src/driver/r_usb_clibusbip.c
    RZA1/usb/r_usb_basic/src/driver/r_usb_hbc.c
    RZA1/usb/r_usb_basic/src/driver/r_usb_hcontrolrw.c
    RZA1/usb/r_usb_basic/src/driver/r_usb_hdriver.c
    RZA1/usb/r_usb_basic/src/driver/r_usb_hhubsys.c
    RZA1/usb/r_usb_basic/src/driver/r_usb_hintfifo.c
    RZA1/usb/r_usb_basic/src/driver/r_usb_hinthandler_usbip0.c
    RZA1/usb/r_usb_basic/src/driver/r_usb_hinthandler_usbip1.c
    RZA1/usb/r_usb_basic/src/driver/r_usb_hlibusbip.c
    RZA1/usb/r_usb_basic/src/driver/r_usb_hmanager.c
    RZA1/usb/r_usb_basic/src/driver/r_usb_hscheduler.c
    RZA1/usb/r_usb_basic/src/driver/r_usb_hsignal.c
    RZA1/usb/r_usb_basic/src/driver/r_usb_hstdfunction.c
    RZA1/usb/r_usb_basic/src/driver/r_usb_pbc.c
    RZA1/usb/r_usb_basic/src/driver/r_usb_pcontrolrw.c
    RZA1/usb/r_usb_basic/src/driver/r_usb_pdriver.c
    RZA1/usb/r_usb_basic/src/driver/r_usb_peptable.c
    RZA1/usb/r_usb_basic/src/driver/r_usb_pintfifo.c
    RZA1/usb/r_usb_basic/src/driver/r_usb_pinthandler_usbip0.c
    RZA1/usb/r_usb_basic/src/driver/r_usb_plibusbip.c
    RZA1/usb/r_usb_basic/src/driver/r_usb_psignal.c
    RZA1/usb/r_usb_basic/src/driver/r_usb_pstdfunction.c
    RZA1/usb/r_usb_basic/src/driver/r_usb_pstdrequest.c
    RZA1/usb/r_usb_basic/src/driver/r_usb_usbif_api.c
    RZA1/usb/r_usb_basic/src/hw/r_usb_creg_abs.c
    RZA1/usb/r_usb_basic/src/hw/r_usb_creg_access.c
    RZA1/usb/r_usb_basic/src/hw/r_usb_dma.c
    RZA1/usb/r_usb_basic/src/hw/r_usb_hostelectrical.c
    RZA1/usb/r_usb_basic/src/hw/r_usb_hreg_abs.c
    RZA1/usb/r_usb_basic/src/hw/r_usb_hreg_access.c
    RZA1/usb/r_usb_basic/src/hw/r_usb_preg_abs.c
    RZA1/usb/r_usb_basic/src/hw/r_usb_preg_access.c
    RZA1/usb/r_usb_basic/src/hw/r_usb_rz_mcu.c
    RZA1/usb/r_usb_hmidi/src/r_usb_hmidi_api.c
    RZA1/usb/r_usb_hmidi/src/r_usb_hmidi_driver.c
    RZA1/usb/USBConnection.c

    # Assembly
    RZA1/compiler/asm/access.S
    RZA1/compiler/asm/initsct.S
    RZA1/compiler/asm/irqfiq_handler.S
    RZA1/compiler/asm/isr.S
    RZA1/compiler/asm/l1_cache_init.S
    RZA1/compiler/asm/reset_handler.S
    RZA1/compiler/asm/start.S
    RZA1/compiler/asm/ttb_init.S    
)

target_include_directories(RZA1 PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}
)

target_link_libraries(RZA1 PRIVATE FatFs RTT)


add_library(FatFs STATIC
  fatfs/ff.c
  fatfs/ffsystem.c
  fatfs/ffunicode.c
)

target_include_directories(FatFs PUBLIC fatfs)

add_library(NE10 STATIC
    NE10/modules/dsp/NE10_fft_int32.c
    NE10/modules/dsp/NE10_init_dsp.c
    NE10/modules/dsp/NE10_fft_generic_int32.neonintrinsic.cpp
    NE10/modules/dsp/NE10_fft.c
    NE10/modules/dsp/NE10_fir_init.c
    NE10/modules/dsp/NE10_fft_int32.neonintrinsic.c
    NE10/modules/dsp/NE10_fft_generic_int32.cpp
    NE10/modules/NE10_init.c
    NE10/common/NE10_mask_table.c
)

target_include_directories(NE10 PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}
    NE10/common
    NE10/inc
    NE10/modules
)

add_library(RTT STATIC
    RTT/SEGGER_RTT.c
    RTT/SEGGER_RTT_ASM_ARMv7M.S
    RTT/SEGGER_RTT_printf.c
)
target_include_directories(RTT PUBLIC RTT)
target_link_libraries(RTT PRIVATE RZA1)
