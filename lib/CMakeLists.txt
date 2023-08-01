include(FetchContent)

FetchContent_Declare(
  fmt
  GIT_REPOSITORY https://github.com/fmtlib/fmt/
  GIT_TAG        a0b8a92e3d1532361c2f7feb63babc5c18d00ef2 # release-10.0.0
)
FetchContent_MakeAvailable(fmt)
