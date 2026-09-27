#pragma once

// A probe for structural edits to the containers the audio render walks: an insert, erase, reorder, relink or
// reallocation that runs while the render could preempt it. Every such edit must run inside an AudioExcludedSection
// (libdeluge/system.h), or on the audio tier itself; the containers call noteStructuralEdit() from each mutator, and an
// edit made outside a section is counted by container kind. The counts are what the container-guard gate and the
// golden harness's exit line report; they must stay 0.
//
// Compiled in only with ALPHA_OR_BETA_VERSION (definitions.h); otherwise noteStructuralEdit() is empty.

#include <cstdint>

namespace deluge {

enum class StructuralContainer : uint8_t {
	ORDERED_POS_VECTOR, ///< notes, note rows (NoteRowVector delegates to it), automation nodes, clip instances
	CLIP_ARRAY,         ///< a song's session and arrangement-only clips
	OUTPUT_LIST,        ///< a song's output list and hibernating-instrument list
	DRUM_LIST,          ///< a kit's drum list
	COUNT_,
};

/// [task] [audio] Record a structural edit to a container of @p kind. Counts it if the caller is neither on the audio
/// tier nor inside an AudioExcludedSection.
void noteStructuralEdit(StructuralContainer kind);

/// [task] Structural edits of @p kind made while the render could preempt them, since boot.
uint32_t unguardedStructuralEdits(StructuralContainer kind);

/// [task] Structural edits of every kind made while the render could preempt them, since boot.
uint32_t unguardedStructuralEditsTotal();

} // namespace deluge
