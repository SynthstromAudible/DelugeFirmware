cmake_policy(SET CMP0135 NEW)

include(FetchContent)

FetchContent_Declare(argon
  GIT_REPOSITORY https://github.com/stellar-aria/argon
  GIT_TAG 4c4aa8ae891b29ed926309cb15ae6bddc38d7895
)
FetchContent_MakeAvailable(argon)

