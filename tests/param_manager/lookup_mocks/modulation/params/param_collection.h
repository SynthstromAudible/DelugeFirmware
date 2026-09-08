#pragma once
#include "modulation/params/param.h"
class ModelStackWithParamId;
class ModelStackWithAutoParam;
class ParamCollection {
public:
	explicit ParamCollection(deluge::modulation::params::Kind kind) : kind(kind) {}
	virtual ~ParamCollection() = default;
	virtual deluge::modulation::params::Kind getParamKind() { return kind; }
	virtual ModelStackWithAutoParam* getAutoParamFromId(ModelStackWithParamId*, bool = true) = 0;
	deluge::modulation::params::Kind kind;
};