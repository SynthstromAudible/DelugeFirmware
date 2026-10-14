#pragma once

#include "definitions.h" // ALPHA_OR_BETA_VERSION
#include "deluge-project/param_address.h"
#include <optional>

class ModelStackWithAutoParam;

namespace deluge::project {
struct DocAutoParam;
}

namespace deluge::edits {

/// @brief The stable id-address of the param lane a resolved engine ModelStack names.
///
/// The owner classification ModelStackWithAutoParam::doc() uses to resolve a lane.
///
/// @param modelStack A resolved stack naming the param.
/// @return The lane's address; nullopt for the one unaddressable case, an owner with no id. Callers fall back to
///         engine resolution, exactly as doc() does.
std::optional<deluge::project::ParamAddress> addressFor(const ModelStackWithAutoParam& modelStack);

/// @brief Resolve the param lane for a RUNTIME-created owner, addressing it first if needed.
///
/// When addressFor() can't yet address the owner (an instrument / drum made after load has no id yet), mint ids for
/// the owner chain and relocate the touched collection's engine store into the project tree, as load does but
/// lazily on first param touch. Afterwards the owner id-resolves like any saved one. The allocation is a one-time
/// cost per owner, not per touch.
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
#endif

} // namespace deluge::edits
