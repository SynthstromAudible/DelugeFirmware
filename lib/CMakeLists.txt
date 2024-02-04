cmake_policy(SET CMP0135 NEW)

include(FetchContent)

FetchContent_Declare(argon
  #GIT_REPOSITORY https://github.com/stellar-aria/argon
  #GIT_TAG caa7cd8056351d4fea353c1e572fa62e04a49971
  URL https://github.com/stellar-aria/argon/archive/refs/tags/v0.1.0.tar.gz
  URL_HASH MD5=2cbc9a803e52ded736f1e28eca8b14aa
)
FetchContent_MakeAvailable(argon)

FetchContent_Declare(doxygen-awesome-css
  URL https://github.com/jothepro/doxygen-awesome-css/archive/refs/tags/v2.2.1.tar.gz
  URL_HASH MD5=340d3a206794ac01a91791c2a513991f
)
FetchContent_MakeAvailable(doxygen-awesome-css)
set(doxygen-awesome-css_SOURCE_DIR ${doxygen-awesome-css_SOURCE_DIR} PARENT_SCOPE)
