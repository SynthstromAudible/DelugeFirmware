cmake_policy(SET CMP0135 NEW)

include(FetchContent)

FetchContent_Declare(argon
  GIT_REPOSITORY https://github.com/stellar-aria/argon
  GIT_TAG dc020d017579ba17c7bbe7f92e0c042673a0e330
)
FetchContent_MakeAvailable(argon)

