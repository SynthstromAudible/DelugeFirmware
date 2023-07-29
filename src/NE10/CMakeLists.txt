add_library(NE10 STATIC)
target_include_directories(NE10 PUBLIC
    inc
)
target_include_directories(NE10 PRIVATE 
    ${SHARED_INCLUDE}
    common
    modules
)

target_sources(NE10 PRIVATE 
    common/NE10header.s
    common/NE10_mask_table.c
    common/versionheader.s

    modules/NE10_init.c

    modules/dsp/NE10_fft.c
    modules/dsp/NE10_fft_generic_int32.cpp
    modules/dsp/NE10_fft_generic_int32.neonintrinsic.cpp
    modules/dsp/NE10_fft_int32.c
    modules/dsp/NE10_fft_int32.neonintrinsic.c
    modules/dsp/NE10_fir_init.c
    modules/dsp/NE10_init_dsp.c
)
