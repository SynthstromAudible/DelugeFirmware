cmake_policy(SET CMP0135 NEW)

include(FetchContent)

FetchContent_Declare(argon
        GIT_REPOSITORY https://github.com/stellar-aria/argon
        # Pinned past v0.3.1 for the SIMDe backend (Platform::SIMDe) — lets the DSP/NEON
        # code compile off-target (x86-64/ARM64) for the host-sim BSP — plus the
        # post-release API fixes the app's DSP relies on (MultiplyAddFixedPoint,
        # LoadGatherInterleaved, MultiplyRoundFixedPoint).
        # 90fde9a "Fix load bugs": the lane accessor's Load() now mutates the bound vector
        # in place (was a no-op return) — required, else the per-lane wavetable gather in
        # waveRenderingFunctionGeneral reads zeros and the oscillators are SILENT (on NEON too,
        # not just SIMDe). Covered by argon test/specs/lane_spec.cpp.
        GIT_TAG 90fde9a1d7ec67c1670d184e8569e293e8424d6e
)
FetchContent_MakeAvailable(argon)

FetchContent_Declare(
        etl
        GIT_REPOSITORY https://github.com/ETLCPP/etl
        GIT_TAG 20.40.0
)

FetchContent_MakeAvailable(etl)
