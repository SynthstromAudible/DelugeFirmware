# Every third-party dependency the firmware, the host sim and the tests fetch, pinned in one place.
#
# Declaring is all this does: each tree calls FetchContent_MakeAvailable for what it uses. FetchContent keeps the
# first declaration of a name, so nothing downstream can re-pin one. crates/deluge_sample_convert/build.rs fetches
# argon and SIMDe itself and must name the same revisions.
include_guard(GLOBAL)
include(FetchContent)

FetchContent_Declare(etl
        GIT_REPOSITORY https://github.com/ETLCPP/etl
        GIT_TAG 20.40.0
)
FetchContent_Declare(argon
        GIT_REPOSITORY https://github.com/stellar-aria/argon
        GIT_TAG 4b8efc4f4f21595d454e415fb619dd91223c0400
)
# NEON intrinsics on non-ARM hosts: argon's <arm/neon.h> and sim/compat's <arm_neon.h> resolve into it.
FetchContent_Declare(simde
        GIT_REPOSITORY https://github.com/simd-everywhere/simde
        GIT_TAG v0.8.2
        GIT_SHALLOW TRUE
)
FetchContent_Declare(CppUTest
        GIT_REPOSITORY https://github.com/cpputest/cpputest.git
        GIT_TAG eccbc2190c672e598c1dd2bf5e4295f5ba27aad1
)
FetchContent_Declare(CppSpec
        URL https://github.com/toroidal-code/cppspec/archive/5fb1a59dc9a47f5f33eba16bab5891eb6a93d892.tar.gz
)
