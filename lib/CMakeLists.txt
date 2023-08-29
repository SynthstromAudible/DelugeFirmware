cmake_policy(SET CMP0135 NEW)

include(FetchContent)




FetchContent_Declare(doxygen-awesome-css
  URL      https://github.com/jothepro/doxygen-awesome-css/archive/refs/tags/v2.2.1.tar.gz
  URL_HASH MD5=340d3a206794ac01a91791c2a513991f
)
FetchContent_MakeAvailable(doxygen-awesome-css)
set(doxygen-awesome-css_SOURCE_DIR ${doxygen-awesome-css_SOURCE_DIR} PARENT_SCOPE)
