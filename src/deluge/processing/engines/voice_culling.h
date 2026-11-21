#pragma once

// Existing audio-engine API; keep the namespace consistent with its other declarations.
// NOLINTNEXTLINE(readability-identifier-naming)
namespace AudioEngine {
// Long synchronous work (including container relocation) can delay rendering.
// Suppress voice culling for that render cycle rather than treating the delay
// as sustained audio overload. The audio engine clears this after rendering.
// Declaration only; this existing public flag is defined in voice_culling.cpp.
// NOLINTNEXTLINE(readability-identifier-naming, bugprone-dynamic-static-initializers)
extern bool bypassCulling;
} // namespace AudioEngine
