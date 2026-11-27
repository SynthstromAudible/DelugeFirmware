#include "modulation/params/arp_param_set.h"

#include "memory/general_memory_allocator.h"
#include "memory/heaps.h"
#include "model/clip/clip.h"
#include "model/lane_owner.h"
#include "model/project_bridge/lane_target.h"
#include "modulation/params/param.h"
#include "processing/sound/sound.h"
#include "util/exceptions.h"

#include <new>

ArpParamSet::ArpParamSet(ParamCollectionSummary* summary, deluge::arp::Owner owner)
    : ParamSet(sizeof(ArpParamSet), summary), owner_(owner) {
	params = params_.data();
	numParams_ = static_cast<int32_t>(params_.size());
	topUintToRepParams = (numParams_ - 1) >> 5;
}

ArpParamSet::ArpParamSet(ArpParamSet const& other) : ParamSet(other), owner_(other.owner_), params_(other.params_) {
	params = params_.data();
}

Error ArpParamSet::clone(ParamCollection*& result, bool copy_automation, int32_t reverse_direction_with_length) const {
	void* mem = deluge::memory::alloc_external(sizeof(ArpParamSet), alignof(ArpParamSet));
	if (mem == nullptr) {
		return Error::INSUFFICIENT_RAM;
	}
	ArpParamSet* clone = nullptr;
	try {
		clone = new (mem) ArpParamSet(*this);
	} catch (deluge::exception&) {
		delugeDealloc(mem);
		return Error::INSUFFICIENT_RAM;
	}
	if (!copy_automation) {
		clone->stripAutomation();
	}
	else if (reverse_direction_with_length != 0) {
		try {
			clone->flattenReverseAutomation(reverse_direction_with_length);
		} catch (deluge::exception&) {
			clone->~ArpParamSet();
			delugeDealloc(mem);
			return Error::INSUFFICIENT_RAM;
		}
	}
	result = clone;
	return Error::NONE;
}

void ArpParamSet::set_to_defaults() {
	for (std::size_t i = 0; i < deluge::arp::kParamCount; ++i) {
		set_word(deluge::arp::id_at(i), deluge::arp::default_value(owner_, deluge::arp::id_at(i)));
	}
}

deluge::arp::Values ArpParamSet::values() const {
	deluge::arp::Values v;
	for (std::size_t i = 0; i < deluge::arp::kParamCount; ++i) {
		v.words[i] = params_[i].getCurrentValue();
	}
	return v;
}

int32_t ArpParamSet::word(deluge::plugin::ParamId id) const {
	return params_[id.value].getCurrentValue();
}

void ArpParamSet::set_words(const deluge::arp::Values& values) {
	for (std::size_t i = 0; i < deluge::arp::kParamCount; ++i) {
		set_word(deluge::arp::id_at(i), values.words[i]);
	}
}

void ArpParamSet::notifyParamModifiedInSomeWay(const deluge::bridge::LaneTarget& lane, int32_t oldValue,
                                               bool automationChanged, bool automatedBefore, bool automatedNow) {
	ParamSet::notifyParamModifiedInSomeWay(lane, oldValue, automationChanged, automatedBefore, automatedNow);
	if (owner_ != deluge::arp::Owner::SOUND || lane.param_id != static_cast<int32_t>(deluge::arp::param::kRate.value)) {
		return;
	}
	// As a patched param's change reaches the matrix: only while the sound plays this clip (or there is no clip)
	if ((lane.timeline == nullptr || static_cast<Clip*>(lane.timeline)->isActiveOnOutput())
	    && lane.auto_param->getCurrentValue() != oldValue) {
		deluge::model::sound_of(lane.owner)
		    .recalculatePatchingToParam(deluge::modulation::params::GLOBAL_ARP_RATE, lane.manager);
	}
}

void ArpParamSet::set_word(deluge::plugin::ParamId id, int32_t word) {
	params_[id.value].setBase(*docForParam(static_cast<int32_t>(id.value)), word);
}
