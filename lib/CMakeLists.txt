cmake_policy(SET CMP0135 NEW)

include(FetchContent)

FetchContent_Declare(argon
  GIT_REPOSITORY https://github.com/stellar-aria/argon
  GIT_TAG 724f1be90d8f1a08750d65bda72e51108f1d3619
)
FetchContent_MakeAvailable(argon)

