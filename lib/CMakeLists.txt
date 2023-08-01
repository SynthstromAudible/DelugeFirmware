cmake_policy(SET CMP0135 NEW)

include(FetchContent)

FetchContent_Declare(
  fmt
  #GIT_REPOSITORY https://github.com/fmtlib/fmt/
  #GIT_TAG        a0b8a92e3d1532361c2f7feb63babc5c18d00ef2 # release-10.0.0
  URL      https://github.com/fmtlib/fmt/archive/refs/tags/10.0.0.tar.gz
  URL_HASH MD5=fa629bc1178918b7af4b2ea6b6a271dc
)
FetchContent_MakeAvailable(fmt)
