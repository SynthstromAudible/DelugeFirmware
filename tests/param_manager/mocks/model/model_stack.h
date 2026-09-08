#pragma once
#include "modulation/params/param_collection_summary.h"

class ModelStackWithParamCollection {
public:
	ParamCollectionSummary* paramCollectionSummary;
};

class ModelStackWithThreeMainThings {
public:
	ModelStackWithParamCollection* addParamCollectionSummary(ParamCollectionSummary* summary) {
		collection.paramCollectionSummary = summary;
		return &collection;
	}

private:
	ModelStackWithParamCollection collection;
};