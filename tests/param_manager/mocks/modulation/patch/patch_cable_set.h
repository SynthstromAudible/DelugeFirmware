#pragma once
#include "modulation/params/param_set.h"

class PatchCableSet : public SetupCollection {
public:
	explicit PatchCableSet(ParamCollectionSummary* summary)
	    : SetupCollection(summary, deluge::modulation::params::Kind::PATCH_CABLE) {}
};
