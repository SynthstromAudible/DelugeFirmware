#pragma once

#include "deluge-project/param_address.h"
#include <optional>

class ModelStackWithAutoParam;

namespace deluge::edits {

/// The stable id-address of the param lane a resolved engine ModelStack names — the owner
/// classification extracted (behaviour-identical) from ModelStackWithAutoParam::doc(). Returns
/// nullopt for the one unaddressable case: a never-saved owner with no serialId; callers fall
/// back to engine resolution, exactly as doc() does.
std::optional<deluge::project::ParamAddress> addressFor(const ModelStackWithAutoParam& modelStack);

} // namespace deluge::edits
