// The patch-routing oracle.
//
// Runs PatchCableSet::setupPatching over seeded random topologies (cables to params, cables to other cables' depths,
// orphaned depth cables, unknown-placeholder and empty targets) under seeded random usability tables shaped like
// Sound::maySourcePatchToParam (a function of source and param), and folds every result into one digest: the cables'
// order (the order a save walks), the usable count, each usable cable's range link, the Destinations of both
// globalities and sourcesPatchedToAnything. The digest is pinned: any change to the routing or the save order fails.
//
// Every case is also compiled by the synth library's compile_routing, which must agree with the runtime field by
// field.
//
// Exit: 0 = the digest matches and every case agrees, 1 = a mismatch.

#include "synth/modulation/patch_routing.h"
#include "definitions_cxx.hpp"
#include "instruments/deluge/cable_records.h"
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

/// The range value that scales cable @p c's depth, or -1 for none.
int32_t rangeLink(const PatchCableSet& set, int32_t c) {
	return set.routing().cables[c].range_index;
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
		h = fold(h, static_cast<uint32_t>(rangeLink(set, c)));
	}
	for (int32_t g = 0; g < 2; g++) {
		h = fold(h, set.sourcesPatchedToAnything[g]);
		for (const deluge::synth::RoutedDestination* d = set.destinations[g]; d != nullptr && d->sources != 0; d++) {
			h = fold(h, d->target.word);
			h = fold(h, d->sources);
			h = fold(h, d->first_cable);
			h = fold(h, d->end_cable);
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
		set.appendCable({c.from, {c.target.data}, static_cast<deluge::synth::CablePolarity>(c.polarity)});
	}
	set.setupPatching(summary, usable);
}

deluge::instrument::CableRecords recordsOf(const Topology& t) {
	deluge::instrument::CableRecords r;
	for (const TopologyCable& c : t.cables) {
		r.append({c.from, {c.target.data}, static_cast<deluge::synth::CablePolarity>(c.polarity)});
	}
	return r;
}

/// Whether the library's compile of @p t gives the runtime's routing of it, @p set.
bool agrees(const Topology& t, const PatchCableSet& set, int32_t seed) {
	deluge::instrument::CableRecords records = recordsOf(t);
	const auto usable =
	    deluge::synth::usability_of(records.records(), [&](PatchSource s, deluge::synth::CableTarget target) {
		    return target != deluge::synth::CableTarget::param(params::PLACEHOLDER_RANGE)
		           && t.usable[std::to_underlying(s)][target.param_index()];
	    });
	static deluge::synth::RoutingBuild b;
	deluge::synth::compile_routing(records.records(), usable, b);
	records.permute(std::span{b.order}.first(b.num_cables));
	auto fail = [&](const char* what) {
		std::fprintf(io.err, "FAIL seed=%d: %s\n", seed, what);
		return false;
	};
	if (b.num_usable != set.numUsablePatchCables) {
		return fail("usable count");
	}
	if (set.records().size() != set.numPatchCables) {
		return fail("records count");
	}
	for (int32_t c = 0; c < set.numPatchCables; c++) {
		if (set.records()[c] != recordOf(set.patchCables[c])) {
			return fail("records");
		}
	}
	for (int32_t c = 0; c < set.numPatchCables; c++) {
		const PatchCable& cable = set.patchCables[c];
		if (records[c].source != cable.from || records[c].target.word != cable.destinationParamDescriptor.data
		    || std::to_underlying(records[c].polarity) != std::to_underlying(cable.polarity)) {
			return fail("order");
		}
	}
	for (int32_t c = 0; c < set.numUsablePatchCables; c++) {
		if (b.cables[c].range_index != rangeLink(set, c)) {
			return fail("range link");
		}
	}
	for (int32_t g = 0; g < 2; g++) {
		if (b.sources_patched[g] != set.sourcesPatchedToAnything[g]) {
			return fail("sources patched");
		}
		const deluge::synth::RoutedDestination* d = set.destinations[g];
		for (int32_t k = 0; k < b.num_destinations[g]; k++, d++) {
			const deluge::synth::RoutedDestination& r = b.destinations[g][k];
			if (d == nullptr || r.target.word != d->target.word || r.sources != d->sources
			    || r.first_cable != d->first_cable || r.end_cable != d->end_cable) {
				return fail("destination");
			}
		}
		if (d != nullptr && d->sources != 0) {
			return fail("destination count");
		}
	}
	return true;
}

int patch_routing_main(int /*argc*/, char** /*argv*/, Io driverIo) {
	io = driverIo;
	ParamCollectionSummary summary{};
	PatchCableSet set{&summary};
	uint32_t digest = 2166136261u;
	int32_t mismatches = 0;
	for (int32_t seed = 0; seed < kCases; seed++) {
		const Topology t = randomTopology(static_cast<uint32_t>(seed));
		route(set, summary, t);
		digest = foldSet(digest, set);
		if (!agrees(t, set, seed) && ++mismatches > 5) {
			break;
		}
	}
	std::fprintf(io.out, "ROUTING-DIGEST cases=%d digest=0x%08x pinned=0x%08x\n", kCases, digest, kPinnedDigest);
	std::fprintf(io.out, "ROUTING-DIFF mismatches=%d\n", mismatches);
	return digest == kPinnedDigest && mismatches == 0 ? 0 : 1;
}

} // namespace

DELUGE_DRIVER_SCENARIO(patch_routing, patch_routing_main)
