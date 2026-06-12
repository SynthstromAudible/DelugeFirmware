cmake_policy(SET CMP0135 NEW)

include(FetchContent)

FetchContent_Declare(argon
        GIT_REPOSITORY https://github.com/stellar-aria/argon
        # 90fde9a "Fix load bugs": the lane accessor's Load() now mutates the bound vector
        # in place (was a no-op return) — required, else the per-lane wavetable gather in
        # waveRenderingFunctionGeneral reads zeros and the oscillators are SILENT (on NEON too,
        # not just SIMDe). Covered by argon test/specs/lane_spec.cpp.
        # cae5738 "Fix the half-float issue with SIMDe": under the SIMDe backend the SIMD
        # wrappers are now always-inlined (the nce/ace macros were empty), so a 64-bit
        # float32x2 (ArgonHalf<float>) no longer crosses a non-inlined call boundary and get
        # corrupted. Host-sim only (native NEON was always fine). Covered by lane_spec.cpp.
        GIT_TAG cae5738c8e4119448ea80df691c297c4c8bc2d5e
)
FetchContent_MakeAvailable(argon)

FetchContent_Declare(
        etl
        GIT_REPOSITORY https://github.com/ETLCPP/etl
        GIT_TAG 20.40.0
)

FetchContent_MakeAvailable(etl)
