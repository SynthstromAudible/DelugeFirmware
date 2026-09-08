#include "definitions_cxx.hpp"

static_assert(ALPHA_OR_BETA_VERSION == EXPECTED_PARAM_MANAGER_DIAGNOSTICS,
              "Param manager tests must compile in the requested diagnostics mode");