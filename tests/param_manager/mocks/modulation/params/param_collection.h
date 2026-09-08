#pragma once
#include "modulation/params/param.h"
class ModelStackWithParamCollection;

// Host tests exercise ParamManager without the DSP/automation engine.
// Keep the virtual dtor and virtual getParamKind() so this matches the real class's shape.
class ParamCollection {
public:
	explicit ParamCollection(deluge::modulation::params::Kind kind) : kind_(kind) {}
	virtual ~ParamCollection() = default;
	virtual deluge::modulation::params::Kind getParamKind() { return kind_; }
	virtual void tickTicks(int32_t ticks, ModelStackWithParamCollection* stack) {}
	virtual void processCurrentPos(ModelStackWithParamCollection* stack, int32_t ticks, bool reversed, bool pingpong,
	                               bool interpolate) {}
	int32_t ticksTilNextEvent = 0;
	void beenCloned(bool copyAutomation, int32_t reverseLength) {
		clonedAutomation = copyAutomation;
		clonedReverseLength = reverseLength;
	}
	uint32_t objectSize = sizeof(ParamCollection);
	bool clonedAutomation = false;
	int32_t clonedReverseLength = 0;

private:
	deluge::modulation::params::Kind kind_;
};
