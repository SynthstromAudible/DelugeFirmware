#pragma once

// A probe for structural edits to the containers and heaps the audio render walks or edits: an insert, erase, reorder,
// relink, reallocation or heap step that runs while the render could preempt it. Every such edit must run inside an
// AudioExcludedSection (libdeluge/system.h), or on the audio tier itself; the containers call noteStructuralEdit() from
// each mutator, and an edit made outside a section is counted by container kind. The counts are what the
// container-guard gate and the golden harness's exit line report; they must stay 0.
//
// Compiled in only with ALPHA_OR_BETA_VERSION (definitions.h); otherwise noteStructuralEdit() is empty.

#include <cstdint>

namespace deluge {

enum class StructuralContainer : uint8_t {
	ORDERED_POS_VECTOR, ///< notes, note rows (NoteRowVector delegates to it), automation nodes, clip instances
	CLIP_ARRAY,         ///< a song's session and arrangement-only clips
	OUTPUT_LIST,        ///< a song's output list and hibernating-instrument list
	DRUM_LIST,          ///< a kit's drum list
	RENDERING_DRUMS,    ///< a kit's drums rendering now (Kit's rendering-drums set)
	VOICES,             ///< a Sound's active voices (Sound::voices_)
	HEAP,               ///< a heap's TLSF step (crates/deluge_alloc: malloc, free, shrink, size, reclaim guard)
	SLAB,               ///< a cluster-slab slot's claim, clear, pin, unpin or touch
	ALLOC_METADATA,     ///< MEM_GUARD's side table of live allocations (memory/alloc_metadata.cpp)
	PATCH_CABLES,       ///< a patch cable set's cables (PatchCableSet::patchCables)
	MOD_FX_RING,        ///< an owner's mod FX ring hand-over (ModControllableAudio::projectModControllableSnapshot)
	COUNT_,
};

/// @brief [task] [audio] Record a structural edit to a container of @p kind. Counts it if the caller is neither on the
///        audio tier nor inside an AudioExcludedSection.
/// @param kind Which container kind the edit was made to.
void noteStructuralEdit(StructuralContainer kind);

/// @brief [task] Structural edits of @p kind made while the render could preempt them, since boot.
/// @param kind Which container kind to report.
/// @return The unguarded-edit count for @p kind.
uint32_t unguardedStructuralEdits(StructuralContainer kind);

/// @brief [task] Structural edits of @p kind recorded since boot, guarded or not. A harness uses it to tell that an
/// edit site ran at all, which a zero unguarded count cannot.
/// @param kind Which container kind to report.
/// @return The number of noteStructuralEdit() calls for @p kind.
uint32_t notedStructuralEdits(StructuralContainer kind);

/// @brief [task] Structural edits of every kind made while the render could preempt them, since boot.
/// @return The unguarded-edit count across every container kind.
uint32_t unguardedStructuralEditsTotal();

} // namespace deluge
