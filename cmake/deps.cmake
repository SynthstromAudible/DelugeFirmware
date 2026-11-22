# Every third-party dependency the firmware, the host sim and the tests fetch, pinned in one place.
#
# Declaring is all this does: each tree calls FetchContent_MakeAvailable for what it uses. FetchContent keeps the
# first declaration of a name, so nothing downstream can re-pin one. src/storage/sample/convert/build.rs fetches
# argon and SIMDe itself and must name the same revisions.
include_guard(GLOBAL)
include(FetchContent)

FetchContent_Declare(etl
        GIT_REPOSITORY https://github.com/ETLCPP/etl
        GIT_TAG 20.40.0
)
# deluge_argon_headers_are_system(): call after FetchContent_MakeAvailable(argon). argon is a third-party header
# library, so its headers are system headers and the libraries' warnings stay on our own code.
function(deluge_argon_headers_are_system)
  get_target_property(includes argon INTERFACE_INCLUDE_DIRECTORIES)
  set_target_properties(argon PROPERTIES INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${includes}")
endfunction()

FetchContent_Declare(argon
        GIT_REPOSITORY https://github.com/stellar-aria/argon
        GIT_TAG 8ad157c1dd3ccc31490a6e9ab5474c1d73b130e4
)
# NEON intrinsics on non-ARM hosts: argon's <arm/neon.h> and src/platform/host/compat's <arm_neon.h> resolve into it.
FetchContent_Declare(simde
        GIT_REPOSITORY https://github.com/simd-everywhere/simde
        GIT_TAG v0.8.4-rc3
        GIT_SHALLOW TRUE
)
FetchContent_Declare(CppSpec
        URL https://github.com/toroidal-code/cppspec/archive/refs/tags/v1.1.0.tar.gz
)
