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
        # 0a2399f (merges cd6b9c3 "Fix each_lane/each_lane_with_index for Clang"): each_lane
        # bound a non-const reference directly to a vector subscript (out[i]), a GCC extension
        # Clang rejects. Now round-trips each lane through a scalar — behaviour-identical and
        # perf-neutral/better on ARM-NEON and SIMDe alike (a vector lane isn't addressable
        # in-register, so the extract/insert was always there). Required for the Clang host-sim.
        # The intervening a322da4/5781330/d5cddc6 are MSVC/CI-only, no DSP behaviour change.
        GIT_TAG 0a2399fc24b61ecf41d9204838ce0948faac136b
)
FetchContent_MakeAvailable(argon)

FetchContent_Declare(
        etl
        GIT_REPOSITORY https://github.com/ETLCPP/etl
        GIT_TAG 20.40.0
)

FetchContent_MakeAvailable(etl)
