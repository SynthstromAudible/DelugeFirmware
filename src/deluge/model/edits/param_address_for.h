#pragma once

#include "definitions.h" // ALPHA_OR_BETA_VERSION
#include "deluge-project/param_address.h"
#include <optional>

class ModelStackWithAutoParam;

namespace deluge::project {
struct DocAutoParam;
}

namespace deluge::edits {

/// The stable id-address of the param lane a resolved engine ModelStack names — the owner
/// classification extracted (behaviour-identical) from ModelStackWithAutoParam::doc(). Returns
/// nullopt for the one unaddressable case: a never-saved owner with no serialId; callers fall
/// back to engine resolution, exactly as doc() does.
std::optional<deluge::project::ParamAddress> addressFor(const ModelStackWithAutoParam& modelStack);

/// S5-b: make doc() total for RUNTIME-created owners. When `addressFor` can't yet address the owner (a clip /
/// instrument / drum made after load has neither a persisted serialId nor a session id), mint session ids for the
/// owner chain and relocate the touched collection's engine store into the session tree — mirroring the load-time
/// relocation, but lazily on first param touch. After it runs, the owner id-resolves like any saved one. Returns
/// the resolved `DocAutoParam`, or nullptr if the owner still can't be classified (song-global, or a genuinely
/// unaddressable case), leaving the caller to decide the fallback. Same allocation profile as the pre-existing
/// lazy `bindDomStore` in doc()'s old fallback — a one-time cost per owner, not per touch.
deluge::project::DocAutoParam* relocateAndResolve(const ModelStackWithAutoParam& modelStack);

#if ALPHA_OR_BETA_VERSION
// S5-b.5b: how many times doc() has fallen through to the paramCollection engine fallback. Invariant once every
// owner is id-addressed: this stays 0. A host harness resets it, drives runtime creations, and asserts it is still
// 0 — the same gate on-device smoke provides, but exhaustively over the model-level creation surface. Deleting the
// fallback outright is safe once this is proven 0 across that surface (and non-Sound drums shown not to reach doc()).
extern uint32_t docFallbackHits;
#endif

} // namespace deluge::edits
