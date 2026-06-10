cmake_policy(SET CMP0135 NEW)

include(FetchContent)

FetchContent_Declare(argon
        GIT_REPOSITORY https://github.com/stellar-aria/argon
        # Pinned past v0.3.1 for the SIMDe backend (Platform::SIMDe) — lets the DSP/NEON
        # code compile off-target (x86-64/ARM64) for the host-sim BSP — plus the
        # post-release API fixes the app's DSP relies on (MultiplyAddFixedPoint,
        # LoadGatherInterleaved, MultiplyRoundFixedPoint).
        GIT_TAG 39a82de7f76b7d952b65e645d43a5335cbd5043d
)
FetchContent_MakeAvailable(argon)

FetchContent_Declare(
        etl
        GIT_REPOSITORY https://github.com/ETLCPP/etl
        GIT_TAG 20.40.0
)

FetchContent_MakeAvailable(etl)
