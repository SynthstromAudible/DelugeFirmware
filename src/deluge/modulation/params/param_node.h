#pragma once

// An automation point is model data (deluge_project); this keeps the app's existing includes working.
#include "deluge-project/param_node.h"

using deluge::project::ParamNode;

/// The nodes a recording borrowed out of a parameter's automation, handed back when it finishes.
struct StolenParamNodes {
	int32_t num;
	ParamNode* nodes;
};
