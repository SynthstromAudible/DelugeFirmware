#pragma once

// The engine's single render domain: the epoch the audio render advances once at the start of every block. Every
// structure the render reads that another tier replaces or frees -- RetireList's deferred destruction now, Published<T>
// snapshots later (the project-model line's SoundSnapshot is the first candidate) -- stamps and reclaims against THIS
// domain. There must never be a second one: two epochs would make "two blocks have passed" mean two different things.
//
// Include only from .cpp files: deluge/util/published.h declares namespace deluge::util, which shadows the global
// ::util that code inside namespace deluge names as util::.

#include "deluge/util/published.h"

namespace AudioEngine {
/// @brief The render domain (see above). Defined in audio_engine.cpp.
deluge::util::SnapshotDomain& renderDomain();
} // namespace AudioEngine
