# Specs for src/deluge/model/song/clip_iterators: the clip iterators against the song stand-in. IN_UNIT_TESTS
# makes clip_iterators.h take mock_song.h's Song and Clip, which no other spec's world shares, so these build their
# own driver.
deluge_specs_driver(deluge_model_song_clip_iterators_specs TREES host
  SOURCES
    ${DELUGE_REPO_ROOT}/src/deluge/model/song/clip_iterators.cpp
    ${DELUGE_REPO_ROOT}/tests/support/mocks/mock_song.cpp
  LIBRARIES
    etl::etl
  INCLUDES
    ${DELUGE_REPO_ROOT}/src
    ${DELUGE_REPO_ROOT}/src/deluge
    ${DELUGE_REPO_ROOT}/include
    ${DELUGE_REPO_ROOT}/tests/support/mocks
  DEFINITIONS
    IN_UNIT_TESTS=1
)
