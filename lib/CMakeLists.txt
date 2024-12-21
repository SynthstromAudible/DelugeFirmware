cmake_policy(SET CMP0135 NEW)

include(FetchContent)

FetchContent_Declare(argon
  GIT_REPOSITORY https://github.com/stellar-aria/argon
  GIT_TAG 67ba8399c5c6b939c2b34be5746ad0d2676418d2
)
FetchContent_MakeAvailable(argon)

