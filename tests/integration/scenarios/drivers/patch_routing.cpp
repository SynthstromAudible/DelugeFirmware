// The patch-routing oracle.
//
// Runs PatchCableSet::setupPatching over seeded random topologies (cables to params, cables to other cables' depths,
// orphaned depth cables, unknown-placeholder and empty targets) under seeded random usability tables shaped like
// Sound::maySourcePatchToParam (a function of source and param), and folds every result into one digest: the cables'
// order (the order a save walks), the usable count, each usable cable's range link, the Destinations of both
// globalities and sourcesPatchedToAnything. The digest is pinned: any change to the routing or the save order fails.
//
// Exit: 0 = the digest matches, 1 = a mismatch.

#include "definitions_cxx.hpp"
#include "modulation/params/param.h"
#include "modulation/params/param_collection_summary.h"
#include "modulation/patch/patch_cable.h"
#include "modulation/patch/patch_cable_set.h"

#include <algorithm>
#include <array>
#include <bitset>
#include <cstdint>
#include <cstdio>
#include <utility>
#include <vector>

#include "scenarios/drivers/driver.h"

extern int32_t rangeFinalValues[];

namespace {

using deluge::scenarios::driver::Io;
namespace params = deluge::modulation::params;

Io io{};

constexpr int32_t kCases = 20000;
/// The digest of kCases topologies routed by setupPatching.
constexpr uint32_t kPinnedDigest = 0xc5bf140e;

struct Rng {
	uint32_t s;
	uint32_t next() {
		s ^= s << 13;
		s ^= s >> 17;
		s ^= s << 5;
		return s;
	}
	uint32_t below(uint32_t n) { return next() % n; }
};

struct TopologyCable {
	PatchSource from;
	ParamDescriptor target;
	Polarity polarity;
};

struct Topology {
	std::vector<TopologyCable> cables;
	std::array<std::bitset<params::kNumParams>, kNumPatchSources> usable{};
};

PatchSource randomSource(Rng& rng) {
	return static_cast<PatchSource>(rng.below(kNumPatchSources));
}

/// A param a depth cable may name: never the unknown placeholder, which no loaded depth cable targets.
uint32_t randomRangeParam(Rng& rng) {
	uint32_t p = rng.below(params::kNumParams);
	return p == params::PLACEHOLDER_RANGE ? 0 : p;
}

Topology randomTopology(uint32_t seed) {
	Rng rng{(seed * 2654435761u) + 1};
	Topology t;
	for (std::bitset<params::kNumParams>& row : t.usable) {
		for (int32_t p = 0; p < params::kNumParams; p++) {
			row[p] = rng.below(5) != 0;
		}
	}
	const auto n = static_cast<size_t>(rng.below(kMaxNumPatchCables + 1));
	auto exists = [&](PatchSource from, ParamDescriptor target) {
		return std::ranges::any_of(t.cables,
		                           [&](const TopologyCable& c) { return c.from == from && c.target == target; });
	};
	for (int32_t attempts = 0; t.cables.size() < n && attempts < 1000; attempts++) {
		const PatchSource from = randomSource(rng);
		ParamDescriptor target;
		const uint32_t kind = rng.below(20);
		if (kind < 13 || t.cables.empty()) {
			target.setToHaveParamOnly(static_cast<int32_t>(rng.below(params::kNumParams)));
		}
		else if (kind < 18) {
			const TopologyCable& other = t.cables[rng.below(t.cables.size())];
			if (other.target.isNull() || !other.target.isJustAParam()
			    || other.target.getJustTheParam() == params::PLACEHOLDER_RANGE) {
				continue;
			}
			target.setToHaveParamAndSource(other.target.getJustTheParam(), other.from);
		}
		else if (kind < 19) {
			target.setToHaveParamAndSource(static_cast<int32_t>(randomRangeParam(rng)), randomSource(rng));
		}
		else {
			target.setToNull();
		}
		if (exists(from, target)) {
			continue;
		}
		t.cables.push_back({from, target, rng.below(2) != 0 ? Polarity::BIPOLAR : Polarity::UNIPOLAR});
	}
	return t;
}

/// patchCableIsUsable's rule, with the table standing in for Sound::maySourcePatchToParam.
bool usableIn(const Topology& t, PatchSource from, ParamDescriptor target) {
	if (target.isNull() || target.isSetToParamWithNoSource(params::PLACEHOLDER_RANGE)) {
		return false;
	}
	PatchSource s = target.getTopLevelSource();
	if (s == PatchSource::NOT_AVAILABLE) {
		s = from;
	}
	return t.usable[std::to_underlying(s)][target.getJustTheParam()];
}

uint32_t fold(uint32_t h, uint32_t v) {
	for (int32_t i = 0; i < 4; i++) {
		h ^= (v >> (8 * i)) & 0xFFu;
		h *= 16777619u;
	}
	return h;
}

int32_t rangeLink(const PatchCable& cable) {
	const int32_t* p = cable.rangeAdjustmentPointer;
	return (p >= rangeFinalValues && p < rangeFinalValues + kMaxNumPatchCables)
	           ? static_cast<int32_t>(p - rangeFinalValues)
	           : -1;
}

uint32_t foldSet(uint32_t h, const PatchCableSet& set) {
	h = fold(h, set.numPatchCables);
	h = fold(h, set.numUsablePatchCables);
	for (int32_t c = 0; c < set.numPatchCables; c++) {
		h = fold(h, std::to_underlying(set.patchCables[c].from));
		h = fold(h, set.patchCables[c].destinationParamDescriptor.data);
		h = fold(h, std::to_underlying(set.patchCables[c].polarity));
	}
	for (int32_t c = 0; c < set.numUsablePatchCables; c++) {
		h = fold(h, static_cast<uint32_t>(rangeLink(set.patchCables[c])));
	}
	for (int32_t g = 0; g < 2; g++) {
		h = fold(h, set.sourcesPatchedToAnything[g]);
		for (const Destination* d = set.destinations[g]; d != nullptr && d->sources != 0; d++) {
			h = fold(h, d->destinationParamDescriptor.data);
			h = fold(h, d->sources);
			h = fold(h, d->firstCable);
			h = fold(h, d->endCable);
		}
	}
	return h;
}

/// Build the topology's cables into @p set in topology order and route them.
void route(PatchCableSet& set, ParamCollectionSummary& summary, const Topology& t) {
	set.truncateCables(0);
	std::bitset<kMaxNumPatchCables> usable;
	for (const TopologyCable& c : t.cables) {
		usable[set.numPatchCables] = usableIn(t, c.from, c.target);
		PatchCable* cable = set.addCable();
		cable->from = c.from;
		cable->destinationParamDescriptor = c.target;
		cable->polarity = c.polarity;
		cable->initAmount(0);
	}
	set.setupPatching(summary, usable);
}

int patch_routing_main(int /*argc*/, char** /*argv*/, Io driverIo) {
	io = driverIo;
	ParamCollectionSummary summary{};
	PatchCableSet set{&summary};
	uint32_t digest = 2166136261u;
	for (int32_t seed = 0; seed < kCases; seed++) {
		const Topology t = randomTopology(static_cast<uint32_t>(seed));
		route(set, summary, t);
		digest = foldSet(digest, set);
	}
	std::fprintf(io.out, "ROUTING-DIGEST cases=%d digest=0x%08x pinned=0x%08x\n", kCases, digest, kPinnedDigest);
	return digest == kPinnedDigest ? 0 : 1;
}

} // namespace

DELUGE_DRIVER_SCENARIO(patch_routing, patch_routing_main)
