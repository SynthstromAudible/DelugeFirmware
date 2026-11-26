# The include directories of the Rust library crates that publish a C header (rule 8 of
# docs/dev/code_layout_north_star.md): each holds <crate name>.h beside the crate's Cargo.toml.
# Needs DELUGE_REPO_ROOT; every target that includes a crate header adds these.
set(DELUGE_CRATE_INCLUDE_DIRS
    ${DELUGE_REPO_ROOT}/src/base/alloc/include
    ${DELUGE_REPO_ROOT}/src/storage/resource/include
    ${DELUGE_REPO_ROOT}/src/storage/sample/source/include
    ${DELUGE_REPO_ROOT}/src/storage/sample/peaks/include
    ${DELUGE_REPO_ROOT}/src/storage/sample/reader/include
    ${DELUGE_REPO_ROOT}/src/storage/sample/fill/include
    ${DELUGE_REPO_ROOT}/src/storage/stream/include
)
