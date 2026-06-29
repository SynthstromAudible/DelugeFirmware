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
        # cae5738 "Fix the half-float issue with SIMDe": under the SIMDe backend the SIMD
        # wrappers are now always-inlined (the nce/ace macros were empty), so a 64-bit
        # float32x2 (ArgonHalf<float>) no longer crosses a non-inlined call boundary in an
        # i386 MMX register and get corrupted — that mangled `ArgonHalf<float> = 16.0f` to
        # [NaN, 16], which NaN'd the Mutable reverb's cosine LFO and railed the host-sim
        # output. Host-sim only (native NEON was always fine). Covered by lane_spec.cpp.
        GIT_TAG cae5738c8e4119448ea80df691c297c4c8bc2d5e
)
FetchContent_MakeAvailable(argon)

FetchContent_Declare(
        etl
        GIT_REPOSITORY https://github.com/ETLCPP/etl
        GIT_TAG 20.40.0
)

FetchContent_MakeAvailable(etl)
