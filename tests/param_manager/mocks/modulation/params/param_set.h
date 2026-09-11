#pragma once
#include "modulation/params/param_collection.h"
#include "modulation/params/param_collection_summary.h"

class SetupCollection : public ParamCollection {
public:
	SetupCollection(ParamCollectionSummary* summary, deluge::modulation::params::Kind initialKind)
	    : ParamCollection(initialKind), kind(initialKind) {
		*summary = {};
		objectSize = sizeof(SetupCollection);
	}
	deluge::modulation::params::Kind getParamKind() override { return kind; }
	deluge::modulation::params::Kind kind;
};

class UnpatchedParamSet : public SetupCollection {
public:
	explicit UnpatchedParamSet(ParamCollectionSummary* summary)
	    : SetupCollection(summary, deluge::modulation::params::Kind::UNPATCHED_SOUND) {}
};
class PatchedParamSet : public SetupCollection {
public:
	explicit PatchedParamSet(ParamCollectionSummary* summary)
	    : SetupCollection(summary, deluge::modulation::params::Kind::PATCHED) {}
};
class ExpressionParamSet : public SetupCollection {
public:
	ExpressionParamSet(ParamCollectionSummary* summary, bool forDrum)
	    : SetupCollection(summary, deluge::modulation::params::Kind::EXPRESSION), forDrum(forDrum) {
		objectSize = sizeof(ExpressionParamSet);
	}
	bool forDrum;
};
