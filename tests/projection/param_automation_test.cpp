// Projection round-trip test for the param/automation DOM leaf, DocAutoParam
// (docs/dev/param_automation_dom.md, Phase 1).
//
// Runs in the sim environment (real AutoParam + the host allocator), so it exercises the projection
// against the actual firmware engine container — and runs on x86 (build-sim) and ARM (build-sim-arm via
// qemu), the dual-arch coverage the design doc calls for. (A CppSpec unit spec can't host this: AutoParam
// pulls in the whole app, and the firmware containers don't build under the bare spec toolchain.)
//
// Exit: 0 = all round-trips faithful, 1 = a mismatch.

#include "deluge-project/param_automation.h"
#include "modulation/automation/auto_param.h"
#include "modulation/params/param_node.h"

#include <cstdint>
#include <cstdio>
#include <unistd.h>
#include <vector>

using deluge::project::DocAutoParam;
using deluge::project::fromDocAutoParam;
using deluge::project::toDocAutoParam;

namespace {

struct Node {
	int32_t pos;
	int32_t value;
	bool interpolated;
};

int failures = 0;

void check(bool ok, const char* what) {
	if (!ok) {
		std::fprintf(stderr, "FAIL: %s\n", what);
		failures++;
	}
}

void setNode(AutoParam& ap, int32_t pos, int32_t value, bool interpolated) {
	int32_t i = ap.nodes.insertSorted(pos, /*isDefinitelyLast=*/true).value_or(-1);
	if (i >= 0) {
		ap.nodes[i].pos = pos;
		ap.nodes[i].value = value;
		ap.nodes[i].interpolated = interpolated;
	}
}

void expectSameNodes(const AutoParam& a, const AutoParam& b, const char* name) {
	check(a.currentValue == b.currentValue, name);
	check(a.nodes.size() == b.nodes.size(), name);
	for (size_t i = 0; i < a.nodes.size() && i < b.nodes.size(); i++) {
		check(a.nodes[i].pos == b.nodes[i].pos, name);
		check(a.nodes[i].value == b.nodes[i].value, name);
		check(a.nodes[i].interpolated == b.nodes[i].interpolated, name);
	}
}

// Build a live AutoParam from a value + node list, round-trip it through the document form, and assert
// the rebuilt AutoParam is identical.
void roundTrip(const char* name, int32_t currentValue, const std::vector<Node>& nodes) {
	AutoParam ap;
	ap.currentValue = currentValue;
	for (const Node& n : nodes) {
		int32_t i = ap.nodes.insertSorted(n.pos, /*isDefinitelyLast=*/true).value_or(-1);
		check(i >= 0, "insertSorted (test setup)");
		ap.nodes[i].pos = n.pos;
		ap.nodes[i].value = n.value;
		ap.nodes[i].interpolated = n.interpolated;
	}

	DocAutoParam doc = toDocAutoParam(ap);
	check(doc.current_value == currentValue, name);
	check(doc.nodes.size() == nodes.size(), name);

	AutoParam rebuilt;
	fromDocAutoParam(doc, rebuilt);
	check(rebuilt.currentValue == currentValue, name);
	check(rebuilt.nodes.size() == nodes.size(), name);
	for (size_t i = 0; i < nodes.size() && i < rebuilt.nodes.size(); i++) {
		check(rebuilt.nodes[i].pos == nodes[i].pos, name);
		check(rebuilt.nodes[i].value == nodes[i].value, name);
		check(rebuilt.nodes[i].interpolated == nodes[i].interpolated, name);
	}
}

} // namespace

int main() {
	// Unautomated: current value only, no nodes.
	roundTrip("unautomated", 123456, {});

	// Automation with interpolated + non-interpolated nodes.
	roundTrip("automated", 777, {{0, 100, false}, {48, -250000000, true}, {96, 250000000, false}});

	// Full patch-cable value range (nodes stored in [-2^30, 2^30-1]).
	roundTrip("patch-cable range", 0, {{0, -1073741824, true}, {64, 1073741823, false}});

	// Extreme current value (full int32 range), no automation.
	roundTrip("int32 extremes", 2147483647, {});
	roundTrip("int32 min", -2147483647 - 1, {});

	// Phase 2: a parameter collection (DocParamSet) round-trips its kind + every param (a mix of
	// unautomated, automated, and patch-cable-range), preserving array order and count.
	{
		constexpr int kNumParams = 3;
		AutoParam set[kNumParams];
		set[0].currentValue = 111; // unautomated
		set[1].currentValue = 222; // automated
		setNode(set[1], 0, 100, false);
		setNode(set[1], 48, -250000000, true);
		set[2].currentValue = 333; // patch-cable range
		setNode(set[2], 0, -1073741824, true);

		using deluge::modulation::params::Kind;
		deluge::project::DocParamSet doc = deluge::project::toDocParamSet(Kind::PATCHED, set, kNumParams);
		check(doc.kind == Kind::PATCHED, "set kind");
		check(static_cast<int>(doc.params.size()) == kNumParams, "set size");

		AutoParam out[kNumParams];
		deluge::project::fromDocParamSet(doc, out, kNumParams);
		for (int p = 0; p < kNumParams; p++) {
			expectSameNodes(out[p], set[p], "set param");
		}
	}

	if (failures == 0) {
		std::printf("param_automation projection: OK\n");
		std::fflush(stdout);
		// _exit (not return) to skip deluge_app's global static destructors, which assume a fully
		// set-up app (e.g. a non-null display) — the same reason the golden harness uses _exit.
		_exit(0);
	}
	std::fprintf(stderr, "param_automation projection: %d failure(s)\n", failures);
	std::fflush(stderr);
	_exit(1);
}
