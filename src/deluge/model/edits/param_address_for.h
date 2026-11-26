#pragma once

#include "definitions.h" // ALPHA_OR_BETA_VERSION
#include "project/param_address.h"
#include <optional>

class ModelStackWithAutoParam;
class ModelStackWithThreeMainThings;

namespace deluge::project {
struct DocAutoParam;
}

namespace deluge::edits {

/// @brief The owner a resolved engine ModelStack names, as an address with no param: the song; a clip (audio, MIDI or
/// CV); an instrument (synth, kit) or a drum (any type) with the clip whose params they are; with no timeline counter,
/// the output or sound drum whose own (backed-up) params the stack holds, with no clip.
///
/// @param modelStack A stack naming an owner and its param manager.
/// @return The owner's address; nullopt where the stack has no song or an owner has no id.
std::optional<deluge::project::ParamAddress> ownerAddressFor(const ModelStackWithThreeMainThings& modelStack);

/// @brief The stable id-address of the param lane a resolved engine ModelStack names.
///
/// The owner classification ModelStackWithAutoParam::doc() uses to resolve a lane.
///
/// @param modelStack A resolved stack naming the param.
/// @return The lane's address (ownerAddressFor's owner with the stack's param); nullopt where ownerAddressFor has
///         none.
std::optional<deluge::project::ParamAddress> addressFor(const ModelStackWithAutoParam& modelStack);

/// @brief Resolve the param lane of an owner whose store is not in the project tree yet.
///
/// Relocates the touched collection's engine store into the project tree, as load does but on the collection's first
/// param touch. Afterwards the owner id-resolves like any saved one; the allocation is a one-time cost per collection.
/// The owner already has its id (a song gives every owner one when it adopts it); an owner without one resolves
/// nothing here.
///
/// @param modelStack A resolved stack naming the param.
/// @return The resolved DocAutoParam, or nullptr if the owner still can't be classified (song-global, or a
///         genuinely unaddressable case), leaving the caller to decide the fallback.
deluge::project::DocAutoParam* relocateAndResolve(const ModelStackWithAutoParam& modelStack);

#if ALPHA_OR_BETA_VERSION
// How many times doc() has fallen through to the paramCollection engine fallback. Invariant once every owner is
// id-addressed: this stays 0. A host harness resets it, drives runtime creations, and asserts it is still 0,
// exhaustively over the model-level creation surface.
// TODO: delete the doc() fallback once this is proven 0 across that surface and non-Sound drums are shown not to
// reach doc().
extern uint32_t docFallbackHits;
/// Owners that reached relocateAndResolve with no id: must stay 0 (every owner gets its id when a song adopts it).
extern uint32_t lazyIdMisses;
#endif

} // namespace deluge::edits
