cmake_policy(SET CMP0135 NEW)

include(FetchContent)

FetchContent_Declare(fmt
  #GIT_REPOSITORY https://github.com/fmtlib/fmt/
  #GIT_TAG        a0b8a92e3d1532361c2f7feb63babc5c18d00ef2 # release-10.0.0
  URL      https://github.com/fmtlib/fmt/archive/refs/tags/10.0.0.tar.gz
  URL_HASH MD5=fa629bc1178918b7af4b2ea6b6a271dc
)
FetchContent_MakeAvailable(fmt)

target_compile_options(fmt PRIVATE -Oz)
target_compile_definitions(fmt PUBLIC FMT_STATIC_THOUSANDS_SEPARATOR)


FetchContent_Declare(doxygen-awesome-css
  URL      https://github.com/jothepro/doxygen-awesome-css/archive/refs/tags/v2.2.1.tar.gz
  URL_HASH MD5=340d3a206794ac01a91791c2a513991f
)
FetchContent_MakeAvailable(doxygen-awesome-css)
set(doxygen-awesome-css_SOURCE_DIR ${doxygen-awesome-css_SOURCE_DIR} PARENT_SCOPE)
