#pragma once
#include "modulation/params/param.h"

// Layout tests exercise the real ParamManager predicates without the DSP/automation engine.
// Collections supply only their kind; their storage and ownership are not under test here.
// Keep the virtual dtor and virtual getParamKind() so this matches the real class's shape.
class ParamCollection {
public:
	explicit ParamCollection(deluge::modulation::params::Kind kind) : kind_(kind) {}
	virtual ~ParamCollection() = default;
	virtual deluge::modulation::params::Kind getParamKind() { return kind_; }

private:
	deluge::modulation::params::Kind kind_;
};
