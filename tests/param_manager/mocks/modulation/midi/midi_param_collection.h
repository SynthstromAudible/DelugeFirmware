#pragma once
#include "modulation/params/param_set.h"

class MIDIParamCollection : public SetupCollection {
public:
	explicit MIDIParamCollection(ParamCollectionSummary* summary)
	    : SetupCollection(summary, deluge::modulation::params::Kind::MIDI) {}
};
