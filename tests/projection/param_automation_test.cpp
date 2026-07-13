// Projection round-trip test for the param/automation DOM leaf, DocAutoParam
// (docs/dev/param_automation_dom.md, Phase 1).
//
// Runs in the sim environment (real AutoParam + the host allocator), so it exercises the projection
// against the actual firmware engine container — and runs on x86 (build-sim) and ARM (build-sim-arm via
// qemu), the dual-arch coverage the design doc calls for. (A CppSpec unit spec can't host this: AutoParam
// pulls in the whole app, and the firmware containers don't build under the bare spec toolchain.)
//
// Exit: 0 = all round-trips faithful, 1 = a mismatch.

#include "definitions_cxx.hpp"
#include "deluge-project/backed_params.h"
#include "deluge-project/param_automation.h"
#include "deluge-project/param_backup.h"
#include "deluge-project/param_edit.h"
#include "model/global_effectable/global_effectable.h"
#include "model/mod_controllable/filters/filter_config.h"
#include "model/mod_controllable/mod_controllable_audio.h"
#include "model/model_stack.h"
#include "model/note/note_row.h"
#include "modulation/automation/auto_param.h"
#include "modulation/automation/copied_param_automation.h" // engine clipboard type for the copy/paste oracle
#include "modulation/midi/midi_param.h"
#include "modulation/midi/midi_param_collection.h"
#include "modulation/params/param_manager.h"
#include "modulation/params/param_node.h"
#include "modulation/params/param_set.h"
#include "modulation/patch/patch_cable.h"
#include "modulation/patch/patch_cable_set.h"
#include "processing/sound/sound_drum.h"
#include "storage/storage_manager.h"

namespace params = deluge::modulation::params;

#include <cstdint>
#include <cstdio>
#include <cstring>
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

// M2-F5: a standalone oracle AutoParam has no real owner, so give it a minimal ParamCollection whose
// docParamForId returns a caller-chosen DocAutoParam. This lets modelStack->doc() resolve through the normal
// owner path (paramCollection->docParamForId) without any inline AutoParam storage. The other virtuals are never
// exercised by the oracle (its ops only read doc()).
struct OracleOwner : public ParamCollection {
	deluge::project::DocAutoParam* target = nullptr;
	explicit OracleOwner(ParamCollectionSummary* s) : ParamCollection(sizeof(OracleOwner), s) {}
	deluge::project::DocAutoParam* docParamForId(int32_t) override { return target; }
	Error clone(ParamCollection*&, bool, int32_t) const override { return Error::NONE; }
	void tickSamples(int32_t, ModelStackWithParamCollection*) override {}
	void tickTicks(int32_t, ModelStackWithParamCollection*) override {}
	void playbackHasEnded(ModelStackWithParamCollection*) override {}
	void grabValuesFromPos(uint32_t, ModelStackWithParamCollection*) override {}
	void generateRepeats(ModelStackWithParamCollection*, uint32_t, uint32_t, bool) override {}
	void appendParamCollection(ModelStackWithParamCollection*, ModelStackWithParamCollection*, int32_t, int32_t,
	                           bool) override {}
	void trimToLength(uint32_t, ModelStackWithParamCollection*, Action*, bool) override {}
	void shiftHorizontally(ModelStackWithParamCollection*, int32_t, int32_t) override {}
	void processCurrentPos(ModelStackWithParamCollection*, int32_t, bool, bool, bool) override {}
	void remotelySwapParamState(AutoParamState*, ModelStackWithParamId*) override {}
	void deleteAllAutomation(Action*, ModelStackWithParamCollection*) override {}
	void nudgeNonInterpolatingNodesAtPos(int32_t, int32_t, int32_t, Action*, ModelStackWithParamCollection*) override {}
	ModelStackWithAutoParam* getAutoParamFromId(ModelStackWithParamId*, bool) override { return nullptr; }
	deluge::modulation::params::Kind getParamKind() override { return deluge::modulation::params::Kind::NONE; }
};

ParamCollectionSummary g_oracleSummary{};
OracleOwner g_oracleOwner{&g_oracleSummary};

// Wire a manually-built ModelStackWithAutoParam so modelStack->doc() resolves to `doc` (for MSWAP oracle ops on a
// standalone param, which has no real owner).
void bindOracle(ModelStackWithAutoParam* ms, AutoParam* ap, deluge::project::DocAutoParam* doc) {
	g_oracleOwner.target = doc;
	ms->autoParam = ap;
	ms->paramCollection = &g_oracleOwner;
	ms->kind = g_oracleOwner.getParamKind(); // keep the threaded kind consistent (doc() ALPHA-asserts they match)
}

// Author a node directly into a DOM node store (the owner-resolved slot for a bound param, or a standalone
// DocAutoParam). M2-E: node access is owner-resolved, so bound-param tests author through docForParam().
void setNodeIn(deluge::project::DocAutoParam& doc, int32_t pos, int32_t value, bool interpolated) {
	int32_t i = doc.nodes.insertSorted(pos, /*isDefinitelyLast=*/true).value_or(-1);
	if (i >= 0) {
		doc.nodes[i].pos = pos;
		doc.nodes[i].value = value;
		doc.nodes[i].interpolated = interpolated;
	}
}

bool cableMatches(const deluge::project::DocPatchCable& a, const deluge::project::DocPatchCable& b) {
	return a.from == b.from && a.destination.data == b.destination.data && a.polarity == b.polarity
	       && a.amount.current_value == b.amount.current_value && a.amount.nodes.size() == b.amount.nodes.size();
}

// M2-F5: an engine op mutates a DocAutoParam (the standalone oracle param's local store); diff it against the DOM
// kernel's DocAutoParam, element-wise.
void expectDocMatchesEngine(const deluge::project::DocAutoParam& eng, const deluge::project::DocAutoParam& doc,
                            const char* name) {
	check(eng.nodes.size() == doc.nodes.size(), name);
	for (size_t i = 0; i < eng.nodes.size() && i < doc.nodes.size(); i++) {
		check(eng.nodes[i].pos == doc.nodes[i].pos, name);
		check(eng.nodes[i].value == doc.nodes[i].value, name);
		check(eng.nodes[i].interpolated == doc.nodes[i].interpolated, name);
	}
}

// M2-F: compare two owner-resolved DOM node stores (for params that live in a ParamSet — their nodes are in the
// set's store, not the AutoParam).
void expectSameDoc(const deluge::project::DocAutoParam& a, const deluge::project::DocAutoParam& b, const char* name) {
	check(a.nodes.size() == b.nodes.size(), name);
	for (size_t i = 0; i < a.nodes.size() && i < b.nodes.size(); i++) {
		check(a.nodes[i].pos == b.nodes[i].pos, name);
		check(a.nodes[i].value == b.nodes[i].value, name);
		check(a.nodes[i].interpolated == b.nodes[i].interpolated, name);
	}
}

// M2-F: compare a param at index `idx` across two ParamSets — the RT cursor (currentValue) plus the owner-resolved
// node store. Replaces expectSameNodes for params that live in a set.
void expectSameParamInSet(ParamSet* a, ParamSet* b, int32_t idx, const char* name) {
	check(a->params[idx].currentValue == b->params[idx].currentValue, name);
	expectSameDoc(*a->docForParam(idx), *b->docForParam(idx), name);
}

// Round-trip a (value, node list) through the document projection helpers and assert it is preserved.
void roundTrip(const char* name, int32_t currentValue, const std::vector<Node>& nodes) {
	deluge::project::DocAutoParam src;
	for (const Node& n : nodes) {
		setNodeIn(src, n.pos, n.value, n.interpolated);
	}

	DocAutoParam doc = toDocAutoParam(currentValue, src.nodes);
	check(doc.current_value == currentValue, name);
	check(doc.nodes.size() == nodes.size(), name);

	int32_t rebuiltValue = 0;
	deluge::project::DocAutoParam rebuilt;
	fromDocAutoParam(doc, rebuiltValue, rebuilt.nodes);
	check(rebuiltValue == currentValue, name);
	check(rebuilt.nodes.size() == nodes.size(), name);
	for (size_t i = 0; i < nodes.size() && i < rebuilt.nodes.size(); i++) {
		check(rebuilt.nodes[i].pos == nodes[i].pos, name);
		check(rebuilt.nodes[i].value == nodes[i].value, name);
		check(rebuilt.nodes[i].interpolated == nodes[i].interpolated, name);
	}
}

// Option I save-flip: the engine save wrappers (Sound/GlobalEffectable/ExpressionParamSet::writeToFile) now read
// the DOM store's scalar, not the engine cursor. A test that authors state via params[p].currentValue must seed
// the store to match — exactly as a real read/edit does. (docForParam resolves the set's own store here.)
void authorStoreFromEngine(ParamSet& s) {
	for (int32_t p = 0; p < s.getNumParams(); p++) {
		s.docForParam(p)->current_value = s.params[p].currentValue;
	}
}
void authorStoreFromEngine(ParamManagerForTimeline& pm) {
	authorStoreFromEngine(*pm.getUnpatchedParamSet());
	if (ParamSet* p = pm.getPatchedParamSet()) {
		authorStoreFromEngine(*p);
	}
}

} // namespace

int main() {
	// Filter-mode serialization must round-trip for EVERY FilterMode, including the OFF sentinel. Regression
	// guard for a layout-dependent OOB read: filterMap was sized kNumFilterModes (which excludes OFF), so
	// lpfTypeToString(FilterMode::OFF) read one past the array and emitted adjacent garbage ("H2L"), which
	// reloaded as HPLADDER — a non-idempotent save that silently turned an off filter on (Glitchy_2 golden).
	{
		const FilterMode allModes[] = {FilterMode::TRANSISTOR_12DB,
		                               FilterMode::TRANSISTOR_24DB,
		                               FilterMode::TRANSISTOR_24DB_DRIVE,
		                               FilterMode::SVF_BAND,
		                               FilterMode::SVF_NOTCH,
		                               FilterMode::HPLADDER,
		                               FilterMode::OFF};
		for (FilterMode m : allModes) {
			const char* s = lpfTypeToString(m);
			check(s != nullptr && s[0] != '\0', "filter mode: writes a non-empty string");
			check(stringToLPFType(s) == m, "filter mode: string round-trips to the same mode");
		}
	}

	// Phase 1 (DOM-live split): setNodeAtPos operates directly on DocAutoParam (pure data, no engine).
	{
		using deluge::project::DocAutoParam;
		DocAutoParam p{}; // current_value 0, empty nodes
		int32_t i0 = deluge::project::setNodeAtPos(p, 48, 100, true);
		check(p.nodes.size() == 1 && i0 == 0, "setNodeAtPos: first insert");
		check(p.nodes[0].pos == 48 && p.nodes[0].value == 100 && p.nodes[0].interpolated, "setNodeAtPos: fields");
		int32_t i1 = deluge::project::setNodeAtPos(p, 0, -50, false); // inserts BEFORE (sorted)
		check(p.nodes.size() == 2 && i1 == 0 && p.nodes[0].pos == 0, "setNodeAtPos: sorted insert at front");
		int32_t i2 = deluge::project::setNodeAtPos(p, 48, 7, false); // modifies existing node at 48
		check(p.nodes.size() == 2 && i2 == 1 && p.nodes[1].pos == 48 && p.nodes[1].value == 7
		          && !p.nodes[1].interpolated,
		      "setNodeAtPos: modify existing");
	}

	// Phase 1 (DOM-live split): getValueAtPos interpolates through DocAutoParam nodes (pure data, no engine).
	{
		using deluge::project::DocAutoParam;
		DocAutoParam p{};
		deluge::project::setNodeAtPos(p, 0, 100, true);
		deluge::project::setNodeAtPos(p, 48, -200, false); // stepped node
		check(deluge::project::getValueAtPos(p, 0, 96) == 100, "getValueAtPos: at node 0");
		check(deluge::project::getValueAtPos(p, 48, 96) == -200, "getValueAtPos: at node 48");
		// Node at 48 is non-interpolated ⇒ the span [0,48) holds the left value (100), no ramp.
		check(deluge::project::getValueAtPos(p, 24, 96) == 100, "getValueAtPos: stepped span holds left value");
		DocAutoParam empty{};
		empty.current_value = 777;
		check(deluge::project::getValueAtPos(empty, 10, 96) == 777, "getValueAtPos: empty -> current_value");

		// Interpolated right node ⇒ linear ramp between the two nodes.
		DocAutoParam ramp{};
		deluge::project::setNodeAtPos(ramp, 0, 0, true);
		deluge::project::setNodeAtPos(ramp, 48, 4800, true);
		check(deluge::project::getValueAtPos(ramp, 24, 96) == 2400, "getValueAtPos: linear ramp midpoint");
	}

	// Phase 1 fidelity guard: the DOM getValueAtPos must be bit-identical to the engine AutoParam::getValueAtPos
	// across the full position range and both directions, including the loop-wrap branches (which the engine
	// reaches via ModelStack::getLoopLength). We build a live AutoParam with the same nodes and diff every
	// position; getLoopLength is satisfied by a NoteRow with an independent loop length (no Song needed).
	{
		struct N {
			int32_t pos;
			int32_t value;
			bool interpolated;
		};
		auto engineValueAt = [](AutoParam& ap, deluge::project::DocAutoParam& apDoc, uint32_t pos, int32_t loopLength,
		                        bool reversed) -> int32_t {
			// Only AutoParam::getValueAtPos's getLoopLength() call touches the ModelStack; a NoteRow with a
			// non-zero loopLengthIfIndependent short-circuits it, so no Song/TimelineCounter is required.
			alignas(ModelStackWithAutoParam) char buf[MODEL_STACK_MAX_SIZE];
			auto* ms = reinterpret_cast<ModelStackWithAutoParam*>(buf);
			ms->song = nullptr;
			ms->setTimelineCounter(nullptr);
			NoteRow nr;
			nr.setLoopLengthIfIndependent(loopLength);
			ms->setNoteRow(&nr);
			bindOracle(ms, &ap, &apDoc);
			return ap.getValueAtPos(pos, ms, reversed);
		};

		const std::vector<std::vector<N>> configs = {
		    {{0, 100, true}, {48, -200, true}, {96, 300, false}}, // interpolated spans + a stepped node
		    {{10, -1000, true}, {80, 1000, true}},                // wrap before first / after last
		    {{50, 777, true}},                                    // single node
		    {{0, -1073741824, true}, {64, 1073741823, false}},    // patch-cable value range
		};
		// Realistic timeline inputs only: every node pos and every query pos is strictly within the loop
		// ([0, loopLength)). Feeding a node at pos == loopLength makes the wrap span collapse to zero ticks,
		// which divides by zero in BOTH the engine and this port (identically) — an out-of-range input the
		// engine never sees at runtime, so probing it would test nothing about the port's fidelity.
		const int32_t loopLengths[] = {128, 2147483647}; // finite (> every node pos) + infinite
		const uint32_t positions[] = {0, 1, 5, 10, 24, 47, 48, 49, 50, 63, 64, 80, 95, 96, 120, 127};

		for (const auto& cfg : configs) {
			AutoParam ap;
			deluge::project::DocAutoParam apDoc;
			DocAutoParam doc{};
			for (const N& n : cfg) {
				setNodeIn(apDoc, n.pos, n.value, n.interpolated);
				deluge::project::setNodeAtPos(doc, n.pos, n.value, n.interpolated);
			}
			for (int32_t loopLength : loopLengths) {
				for (uint32_t pos : positions) {
					for (bool reversed : {false, true}) {
						int32_t want = engineValueAt(ap, apDoc, pos, loopLength, reversed);
						int32_t got = deluge::project::getValueAtPos(doc, pos, loopLength, reversed);
						check(want == got, "getValueAtPos: matches engine oracle");
					}
				}
			}
		}
	}

	// Phase 1: getDistanceToNextNode (pure query) — functional + engine-oracle diff over the full position
	// range and both directions (the engine reads the loop length from the ModelStack as effectiveLength).
	{
		using deluge::project::DocAutoParam;
		DocAutoParam p{};
		deluge::project::setNodeAtPos(p, 10, 0, true);
		deluge::project::setNodeAtPos(p, 80, 0, true);
		check(deluge::project::getDistanceToNextNode(p, 0, 96, false) == 10, "getDistanceToNextNode: fwd to first");
		check(deluge::project::getDistanceToNextNode(p, 10, 96, false) == 70, "getDistanceToNextNode: fwd from a node");
		check(deluge::project::getDistanceToNextNode(p, 80, 96, false) == 26,
		      "getDistanceToNextNode: fwd wraps to first");
		check(deluge::project::getDistanceToNextNode(p, 0, 96, true) == 16, "getDistanceToNextNode: rev wraps to last");
		DocAutoParam none{};
		check(deluge::project::getDistanceToNextNode(none, 5, 96, false) == 96, "getDistanceToNextNode: empty -> loop");

		auto engineDistance = [](AutoParam& ap, deluge::project::DocAutoParam& apDoc, int32_t pos, int32_t loopLength,
		                         bool reversed) -> int32_t {
			alignas(ModelStackWithAutoParam) char buf[MODEL_STACK_MAX_SIZE];
			auto* ms = reinterpret_cast<ModelStackWithAutoParam*>(buf);
			ms->song = nullptr;
			ms->setTimelineCounter(nullptr);
			NoteRow nr;
			nr.setLoopLengthIfIndependent(loopLength);
			ms->setNoteRow(&nr);
			bindOracle(ms, &ap, &apDoc);
			return ap.getDistanceToNextNode(ms, pos, reversed);
		};

		struct N {
			int32_t pos;
			bool interpolated;
		};
		const std::vector<std::vector<N>> configs = {
		    {{0, true}, {48, true}, {96, false}},
		    {{10, true}, {80, true}},
		    {{50, true}},
		};
		const int32_t loopLengths[] = {128, 2147483647};
		const int32_t positions[] = {0, 1, 5, 10, 24, 47, 48, 49, 50, 63, 64, 80, 95, 96, 120, 127};
		for (const auto& cfg : configs) {
			AutoParam ap;
			deluge::project::DocAutoParam apDoc;
			DocAutoParam doc{};
			for (const N& n : cfg) {
				setNodeIn(apDoc, n.pos, 0, n.interpolated);
				deluge::project::setNodeAtPos(doc, n.pos, 0, n.interpolated);
			}
			for (int32_t loopLength : loopLengths) {
				for (int32_t pos : positions) {
					for (bool reversed : {false, true}) {
						int32_t want = engineDistance(ap, apDoc, pos, loopLength, reversed);
						int32_t got = deluge::project::getDistanceToNextNode(doc, pos, loopLength, reversed);
						check(want == got, "getDistanceToNextNode: matches engine oracle");
					}
				}
			}
		}
	}

	// Phase 1: insertTime + shiftHorizontally (pure structural ops) — functional + engine-oracle diff.
	// Apply the same op to a live AutoParam and a DocAutoParam, then compare the resulting node arrays.
	{
		using deluge::project::DocAutoParam;

		// insertTime: open a gap; nodes at/after pos move right.
		{
			DocAutoParam p{};
			deluge::project::setNodeAtPos(p, 0, 10, true);
			deluge::project::setNodeAtPos(p, 48, 20, true);
			deluge::project::setNodeAtPos(p, 96, 30, false);
			deluge::project::insertTime(p, 48, 16);
			check(p.nodes.size() == 3 && p.nodes[0].pos == 0 && p.nodes[1].pos == 64 && p.nodes[2].pos == 112,
			      "insertTime: shifts nodes at/after pos");
		}

		struct N {
			int32_t pos;
			int32_t value;
			bool interpolated;
		};
		const std::vector<std::vector<N>> configs = {
		    {{0, 10, true}, {48, -20, true}, {96, 30, false}},
		    {{10, 1, true}, {80, 2, true}},
		    {{50, 7, true}},
		};

		// insertTime oracle diff.
		const struct {
			int32_t pos;
			int32_t len;
		} insertCases[] = {{0, 16}, {48, 32}, {50, 1}, {96, 8}, {200, 4}};
		for (const auto& cfg : configs) {
			for (const auto& ic : insertCases) {
				AutoParam ap;
				deluge::project::DocAutoParam apDoc;
				DocAutoParam doc{};
				for (const N& n : cfg) {
					setNodeIn(apDoc, n.pos, n.value, n.interpolated);
					deluge::project::setNodeAtPos(doc, n.pos, n.value, n.interpolated);
				}
				ap.insertTime(*(&apDoc), ic.pos, ic.len);
				deluge::project::insertTime(doc, ic.pos, ic.len);
				expectDocMatchesEngine(apDoc, doc, "insertTime: matches engine oracle");
			}
		}

		// shiftHorizontally oracle diff (positive, negative, and > length amounts).
		const struct {
			int32_t amount;
			int32_t effectiveLength;
		} shiftCases[] = {{1, 128}, {-1, 128}, {16, 128}, {-16, 128}, {64, 128}, {130, 128}, {-130, 128}, {0, 128}};
		for (const auto& cfg : configs) {
			for (const auto& sc : shiftCases) {
				AutoParam ap;
				deluge::project::DocAutoParam apDoc;
				DocAutoParam doc{};
				for (const N& n : cfg) {
					setNodeIn(apDoc, n.pos, n.value, n.interpolated);
					deluge::project::setNodeAtPos(doc, n.pos, n.value, n.interpolated);
				}
				ap.shiftHorizontally(*(&apDoc), sc.amount, sc.effectiveLength);
				deluge::project::shiftHorizontally(doc, sc.amount, sc.effectiveLength);
				expectDocMatchesEngine(apDoc, doc, "shiftHorizontally: matches engine oracle");
			}
		}

		// shiftValues oracle diff — current_value + node values, saturating at the int32 extremes.
		const int32_t shiftValueOffsets[] = {0, 100, -100, 2000000000, -2000000000, 2147483647, -2147483647};
		const struct {
			int32_t currentValue;
			std::vector<N> cfg;
		} valueCases[] = {
		    {123, {{0, 10, true}, {48, -20, true}}},
		    {2000000000, {{0, 2000000000, true}, {48, -2000000000, false}}}, // near the rails
		    {-2147483647 - 1, {{0, 2147483647, true}}},                      // at the rails
		};
		for (const auto& vc : valueCases) {
			for (int32_t offset : shiftValueOffsets) {
				AutoParam ap;
				deluge::project::DocAutoParam apDoc;
				ap.currentValue = vc.currentValue;
				DocAutoParam doc{};
				doc.current_value = vc.currentValue;
				for (const N& n : vc.cfg) {
					setNodeIn(apDoc, n.pos, n.value, n.interpolated);
					deluge::project::setNodeAtPos(doc, n.pos, n.value, n.interpolated);
				}
				ap.shiftValues(*(&apDoc), offset);
				deluge::project::shiftValues(doc, offset);
				check(ap.currentValue == doc.current_value, "shiftValues: current_value matches engine oracle");
				expectDocMatchesEngine(apDoc, doc, "shiftValues: nodes match engine oracle");
			}
		}

		// Movement 2 (M2-A): transposeCCValuesToChannelPressure — current_value + node values remap; oracle diff.
		{
			const struct {
				int32_t currentValue;
				std::vector<N> cfg;
			} transposeCases[] = {
			    {0, {{0, 0, true}, {48, 127, false}}},
			    {64, {{0, -1073741824, true}, {48, 1073741823, false}}}, // full patch-cable range
			    {100, {{0, 2000000000, true}, {48, -2000000000, false}}},
			    {-2147483647 - 1, {{0, 2147483647, true}}}, // rails
			};
			for (const auto& tc : transposeCases) {
				AutoParam ap;
				deluge::project::DocAutoParam apDoc;
				ap.currentValue = tc.currentValue;
				DocAutoParam doc{};
				doc.current_value = tc.currentValue;
				for (const N& n : tc.cfg) {
					setNodeIn(apDoc, n.pos, n.value, n.interpolated);
					deluge::project::setNodeAtPos(doc, n.pos, n.value, n.interpolated);
				}
				ap.transposeCCValuesToChannelPressureValues(*(&apDoc));
				deluge::project::transposeCCValuesToChannelPressure(doc);
				check(ap.currentValue == doc.current_value, "transposeCC: current_value matches engine oracle");
				expectDocMatchesEngine(apDoc, doc, "transposeCC: nodes match engine oracle");
			}
		}

		// generateRepeats oracle diff — tile out to newLength, both straight and pingponged, over node
		// configs that live within the first repeat [0, oldLength).
		const std::vector<std::vector<N>> repeatConfigs = {
		    {{0, 100, true}, {24, -50, true}, {40, 200, false}}, // node at 0 + interior + stepped
		    {{8, 10, true}, {32, -10, true}},                    // nothing at 0 (exercises the pingpong seam node)
		    {{0, 7, false}},                                     // single stepped node at 0
		    {{16, 5, true}},                                     // single node, nothing at 0
		};
		const struct {
			uint32_t oldLength;
			uint32_t newLength;
		} repeatCases[] = {{48, 96}, {48, 128}, {48, 144}, {48, 100}, {48, 48}};
		for (const auto& cfg : repeatConfigs) {
			for (const auto& rc : repeatCases) {
				for (bool pingpong : {false, true}) {
					AutoParam ap;
					deluge::project::DocAutoParam apDoc;
					DocAutoParam doc{};
					for (const N& n : cfg) {
						setNodeIn(apDoc, n.pos, n.value, n.interpolated);
						deluge::project::setNodeAtPos(doc, n.pos, n.value, n.interpolated);
					}
					ap.generateRepeats(*(&apDoc), rc.oldLength, rc.newLength, pingpong);
					deluge::project::generateRepeats(doc, rc.oldLength, rc.newLength, pingpong);
					expectDocMatchesEngine(apDoc, doc, "generateRepeats: matches engine oracle");
				}
			}
		}
	}

	// Phase 1: homogenizeRegion (region collapse) — engine-oracle diff over forward non-wrap, forward clip-cut,
	// and reversed (incl. wrap) via the NoteRow model-stack harness. The forward-wrap-no-cut sub-branch consults
	// Clip::getCurrentlyRecordingLinearly (needs a Clip timeline-counter), so it's covered by DOM-only invariant
	// checks below rather than the engine oracle.
	{
		using deluge::project::DocAutoParam;

		auto engineHomogenize = [](AutoParam& ap, deluge::project::DocAutoParam& apDoc, int32_t startPos,
		                           int32_t length, int32_t startValue, bool interpL, bool interpR, int32_t effLen,
		                           bool reversed, int32_t cut) -> int32_t {
			alignas(ModelStackWithAutoParam) char buf[MODEL_STACK_MAX_SIZE];
			auto* ms = reinterpret_cast<ModelStackWithAutoParam*>(buf);
			ms->song = nullptr;
			ms->setTimelineCounter(nullptr);
			NoteRow nr;
			nr.setLoopLengthIfIndependent(effLen);
			ms->setNoteRow(&nr);
			bindOracle(ms, &ap, &apDoc);
			return ap.homogenizeRegion(ms, startPos, length, startValue, interpL, interpR, effLen, reversed, cut);
		};

		struct N {
			int32_t pos;
			int32_t value;
			bool interpolated;
		};
		const std::vector<std::vector<N>> configs = {
		    {},
		    {{48, 7, false}},
		    {{0, 100, true}, {32, -50, true}, {64, 200, false}, {96, -200, true}},
		};
		struct Case {
			int32_t startPos, length, startValue;
			bool interpL, interpR, reversed;
			int32_t cut;
		};
		constexpr int32_t MAXCUT = 2147483647;
		const Case cases[] = {
		    {16, 32, 500, true, true, false, MAXCUT},    // forward non-wrap
		    {0, 64, -300, false, true, false, MAXCUT},   // forward non-wrap, from 0
		    {32, 16, 111, true, false, false, MAXCUT},   // forward non-wrap, stepped right
		    {60, 40, -7, false, false, false, MAXCUT},   // forward non-wrap
		    {16, 200, 999, true, true, false, 100},      // forward clip-cut
		    {0, 300, -1, false, true, false, 64},        // forward clip-cut
		    {96, 32, 42, true, true, true, 0},           // reversed non-wrap
		    {100, 50, -5, false, true, true, 0},         // reversed non-wrap
		    {16, 48, 77, true, true, true, -1000000000}, // reversed wrap
		    {96, 48, 33, true, true, true, 64},          // reversed clip-cut
		};
		const int32_t effLen = 128;
		for (const auto& cfg : configs) {
			for (const auto& c : cases) {
				AutoParam ap;
				deluge::project::DocAutoParam apDoc;
				DocAutoParam doc{};
				for (const N& n : cfg) {
					setNodeIn(apDoc, n.pos, n.value, n.interpolated);
					deluge::project::setNodeAtPos(doc, n.pos, n.value, n.interpolated);
				}
				// Reserve headroom so the engine's internal insertAt can't reallocate the node buffer mid-op.
				// Without this the engine can hit a latent dangling-pointer bug (it captures a raw pointer to a
				// pre-existing left-edge node, then a right-edge insert reallocates, so the left node's updated
				// value is written to freed memory and the edit is silently lost) — which would make it a
				// non-deterministic oracle. The DOM port is index-based and immune, so it is the correct result.
				(void)(&apDoc)->nodes.reserveExtra(4);
				int32_t engRet = engineHomogenize(ap, apDoc, c.startPos, c.length, c.startValue, c.interpL, c.interpR,
				                                  effLen, c.reversed, c.cut);
				int32_t domRet = deluge::project::homogenizeRegion(doc, c.startPos, c.length, c.startValue, c.interpL,
				                                                   c.interpR, effLen, c.reversed,
				                                                   /*recordingLinearly=*/false, c.cut);
				check(engRet == domRet, "homogenizeRegion: return index matches engine oracle");
				expectDocMatchesEngine(apDoc, doc, "homogenizeRegion: nodes match engine oracle");
			}
		}

		// forward-wrap-no-cut branch (not engine-oracle'd — needs a Clip). Region [112,144) in a 128 loop wraps
		// to end at 16; with recordingLinearly the clip extends instead (length clamps to 16, end node at 0).
		// Assert invariants that follow directly from the code (edgePositions[kLeft]=startPos gets startValue).
		auto isSortedInLoop = [](const DocAutoParam& d, int32_t loopLen) -> bool {
			for (size_t i = 0; i < d.nodes.size(); i++) {
				if (d.nodes[i].pos < 0 || d.nodes[i].pos >= loopLen) {
					return false;
				}
				if (i > 0 && d.nodes[i].pos <= d.nodes[i - 1].pos) {
					return false;
				}
			}
			return true;
		};
		auto hasNodeAt = [](const DocAutoParam& d, int32_t pos) -> bool {
			for (const auto& n : d.nodes) {
				if (n.pos == pos) {
					return true;
				}
			}
			return false;
		};
		{
			DocAutoParam doc{};
			deluge::project::setNodeAtPos(doc, 0, 10, true);
			deluge::project::setNodeAtPos(doc, 64, 20, true);
			int32_t left = deluge::project::homogenizeRegion(doc, 112, 32, 555, true, true, 128, false,
			                                                 /*recordingLinearly=*/false, 2147483647);
			check(doc.nodes[left].pos == 112 && doc.nodes[left].value == 555, "homogenizeRegion: fwd-wrap start node");
			check(hasNodeAt(doc, 16), "homogenizeRegion: fwd-wrap end node at 16");
			check(isSortedInLoop(doc, 128), "homogenizeRegion: fwd-wrap stays sorted/in-loop");
		}
		{
			DocAutoParam doc{};
			deluge::project::setNodeAtPos(doc, 0, 10, true);
			deluge::project::setNodeAtPos(doc, 64, 20, true);
			int32_t left = deluge::project::homogenizeRegion(doc, 112, 32, 555, true, true, 128, false,
			                                                 /*recordingLinearly=*/true, 2147483647);
			check(doc.nodes[left].pos == 112 && doc.nodes[left].value == 555,
			      "homogenizeRegion: fwd-linrec start node");
			check(hasNodeAt(doc, 0), "homogenizeRegion: fwd-linrec end node at 0");
			check(isSortedInLoop(doc, 128), "homogenizeRegion: fwd-linrec stays sorted/in-loop");
		}
	}

	// Phase 1: deleteTime + moveRegionHorizontally (pure region kernels) — engine-oracle diff. Both are callable
	// with the light NoteRow harness: deleteTime uses the model-stack only via getValueAtPos (→getLoopLength);
	// moveRegionHorizontally touches it only through the guarded action snapshot, so action=nullptr keeps it away.
	{
		using deluge::project::DocAutoParam;
		struct N {
			int32_t pos;
			int32_t value;
			bool interpolated;
		};
		const std::vector<std::vector<N>> configs = {
		    {},
		    {{48, 7, false}},
		    {{0, 10, true}, {32, 20, true}, {64, 30, false}, {96, -40, true}},
		};
		const int32_t loopLen = 128;

		auto buildStack = [](char* buf, int32_t effLen, NoteRow& nr) -> ModelStackWithAutoParam* {
			auto* ms = reinterpret_cast<ModelStackWithAutoParam*>(buf);
			ms->song = nullptr;
			ms->setTimelineCounter(nullptr);
			nr.setLoopLengthIfIndependent(effLen);
			ms->setNoteRow(&nr);
			return ms;
		};

		// deleteTime oracle diff.
		const struct {
			int32_t startPos, len;
		} deleteCases[] = {{32, 16}, {0, 64}, {60, 40}, {90, 40}, {16, 8}, {0, 128}};
		for (const auto& cfg : configs) {
			for (const auto& dc : deleteCases) {
				AutoParam ap;
				deluge::project::DocAutoParam apDoc;
				DocAutoParam doc{};
				for (const N& n : cfg) {
					setNodeIn(apDoc, n.pos, n.value, n.interpolated);
					deluge::project::setNodeAtPos(doc, n.pos, n.value, n.interpolated);
				}
				(void)(&apDoc)->nodes.reserveExtra(4);
				alignas(ModelStackWithAutoParam) char buf[MODEL_STACK_MAX_SIZE];
				NoteRow nr;
				ModelStackWithAutoParam* ms = buildStack(buf, loopLen, nr);
				bindOracle(ms, &ap, &apDoc);
				ap.deleteTime(dc.startPos, dc.len, ms);
				deluge::project::deleteTime(doc, dc.startPos, dc.len, loopLen);
				expectDocMatchesEngine(apDoc, doc, "deleteTime: matches engine oracle");
			}
		}

		// moveRegionHorizontally oracle diff.
		const struct {
			int32_t pos, length, offset;
		} moveCases[] = {{32, 16, 1},   {32, 16, -1}, {0, 32, 1},  {0, 32, -1}, {100, 40, 1},
		                 {100, 40, -1}, {60, 40, -1}, {60, 40, 1}, {0, 128, 1}, {96, 40, 1}};
		for (const auto& cfg : configs) {
			for (const auto& mc : moveCases) {
				AutoParam ap;
				deluge::project::DocAutoParam apDoc;
				DocAutoParam doc{};
				for (const N& n : cfg) {
					setNodeIn(apDoc, n.pos, n.value, n.interpolated);
					deluge::project::setNodeAtPos(doc, n.pos, n.value, n.interpolated);
				}
				(void)(&apDoc)->nodes.reserveExtra(4);
				alignas(ModelStackWithAutoParam) char buf[MODEL_STACK_MAX_SIZE];
				NoteRow nr;
				ModelStackWithAutoParam* ms = buildStack(buf, loopLen, nr);
				bindOracle(ms, &ap, &apDoc);
				ap.moveRegionHorizontally(ms, mc.pos, mc.length, mc.offset, loopLen, nullptr);
				deluge::project::moveRegionHorizontally(doc, mc.pos, mc.length, mc.offset, loopLen);
				expectDocMatchesEngine(apDoc, doc, "moveRegionHorizontally: matches engine oracle");
			}
		}
	}

	// Phase 1: deleteNodesWithinRegion (pure kernel) — diffed against the (now-fixed) engine node-erase logic,
	// replicated here because the full engine method ends in notifyParamModifiedInSomeWay (UI/song globals) and
	// isn't callable with the light harness. Includes the wrap case that exposed engine bug #3.
	{
		using deluge::project::DocAutoParam;
		struct N {
			int32_t pos;
			int32_t value;
			bool interpolated;
		};
		// Replica of the engine's (fixed) node-erase logic — tail-before-front in the wrap branch.
		auto engineErase = [](AutoParam& ap, deluge::project::DocAutoParam& apDoc, int32_t pos, int32_t length,
		                      int32_t effLen) {
			if (!ap.isAutomated(*(&apDoc))) {
				return;
			}
			if (length >= effLen) {
				(&apDoc)->nodes.erase((&apDoc)->nodes.begin(), (&apDoc)->nodes.end());
				return;
			}
			int32_t st0 = pos, st1 = pos + length;
			bool wrapping = (st1 >= effLen);
			if (wrapping) {
				st0 = st1 - effLen;
				st1 = pos;
			}
			int32_t ri0 = (&apDoc)->nodes.firstAtOrAfter(st0);
			int32_t ri1 = (&apDoc)->nodes.firstAtOrAfter(st1);
			if (wrapping) {
				int32_t numAtEnd = static_cast<int32_t>((&apDoc)->nodes.size()) - ri1;
				if (numAtEnd) {
					(&apDoc)->nodes.erase((&apDoc)->nodes.begin() + ri1, (&apDoc)->nodes.begin() + ri1 + numAtEnd);
				}
				if (ri0) {
					(&apDoc)->nodes.erase((&apDoc)->nodes.begin(), (&apDoc)->nodes.begin() + ri0);
				}
			}
			else {
				(&apDoc)->nodes.erase((&apDoc)->nodes.begin() + ri0, (&apDoc)->nodes.begin() + ri1);
			}
		};

		const std::vector<std::vector<N>> configs = {
		    {{5, 1, false}, {40, 2, false}, {90, 3, false}, {110, 4, false}}, // bug #3 repro (after+before wrap)
		    {{0, 10, true}, {32, 20, true}, {64, 30, false}, {96, -40, true}},
		    {{48, 7, false}},
		    {},
		};
		const struct {
			int32_t pos, length;
		} cases[] = {{100, 40}, {32, 16}, {0, 64}, {60, 40}, {90, 50}, {0, 128}, {110, 30}};
		const int32_t effLen = 128;
		for (const auto& cfg : configs) {
			for (const auto& c : cases) {
				AutoParam ap;
				deluge::project::DocAutoParam apDoc;
				DocAutoParam doc{};
				for (const N& n : cfg) {
					setNodeIn(apDoc, n.pos, n.value, n.interpolated);
					deluge::project::setNodeAtPos(doc, n.pos, n.value, n.interpolated);
				}
				engineErase(ap, apDoc, c.pos, c.length, effLen);
				deluge::project::deleteNodesWithinRegion(doc, c.pos, c.length, effLen);
				expectDocMatchesEngine(apDoc, doc, "deleteNodesWithinRegion: matches fixed engine node logic");
			}
		}

		// Explicit bug #3 assertion: the after+before-wrap region deletes BOTH edge groups.
		DocAutoParam doc{};
		deluge::project::setNodeAtPos(doc, 5, 1, false);
		deluge::project::setNodeAtPos(doc, 40, 2, false);
		deluge::project::setNodeAtPos(doc, 90, 3, false);
		deluge::project::setNodeAtPos(doc, 110, 4, false);
		deluge::project::deleteNodesWithinRegion(doc, 100, 40, 128);
		check(doc.nodes.size() == 2 && doc.nodes[0].pos == 40 && doc.nodes[1].pos == 90,
		      "deleteNodesWithinRegion: wrap deletes both after- and before-wrap nodes (bug #3 fixed)");
	}

	// Unautomated: current value only, no nodes.
	roundTrip("unautomated", 123456, {});

	// Automation with interpolated + non-interpolated nodes.
	roundTrip("automated", 777, {{0, 100, false}, {48, -250000000, true}, {96, 250000000, false}});

	// Full patch-cable value range (nodes stored in [-2^30, 2^30-1]).
	roundTrip("patch-cable range", 0, {{0, -1073741824, true}, {64, 1073741823, false}});

	// Extreme current value (full int32 range), no automation.
	roundTrip("int32 extremes", 2147483647, {});
	roundTrip("int32 min", -2147483647 - 1, {});

	// Phase 2 DocParamSet kind + per-param round-trip is now covered by the Phase 3 block below (live
	// ParamManager -> DocParamManager -> ParamManager); the old raw AutoParam[]-array projection helpers were
	// retired with M2-F (no engine AutoParam holds node storage).

	// Phase 3: a live ParamManager round-trips its present collections (patched + unpatched) through
	// DocParamManager and projects faithfully into another already-set-up manager.
	{
		ParamManagerForTimeline pm;
		check(pm.setupWithPatching() == Error::NONE, "pm setupWithPatching");
		pm.getPatchedParamSet()->params[5].currentValue = 12345;
		setNodeIn(*pm.getPatchedParamSet()->docForParam(5), 0, 100, true);
		setNodeIn(*pm.getPatchedParamSet()->docForParam(5), 32, -500, false);
		pm.getUnpatchedParamSet()->params[2].currentValue = 678;

		// A patch cable (Phase 3b): source -> destination param, with an automatable amount.
		PatchCableSet* pcs = pm.getPatchCableSet();
		pcs->numPatchCables = 1;
		pcs->patchCables[0].from = PatchSource::ENVELOPE_0;
		pcs->patchCables[0].destinationParamDescriptor.setToHaveParamOnly(7);
		pcs->patchCables[0].polarity = Polarity::BIPOLAR;
		pcs->patchCables[0].param.currentValue = 536870912;   // amount, patch-cable range (engine cursor)
		pcs->docForParam(0)->current_value = 536870912;       // Option I: store holds the persisted scalar
		setNodeIn(*pcs->docForParam(0), 0, -536870912, true); // M2-F: cable nodes live in the cable's own store

		deluge::project::DocParamManager doc = deluge::project::toDocParamManager(pm);
		check(doc.patched.has_value(), "pm patched present");
		check(doc.unpatched.has_value(), "pm unpatched present");
		check(doc.patch_cables.size() == 1, "pm patch cable present");

		ParamManagerForTimeline pm2;
		check(pm2.setupWithPatching() == Error::NONE, "pm2 setupWithPatching");
		deluge::project::fromDocParamManager(doc, pm2);
		check(pm2.getPatchedParamSet()->params[5].currentValue == 12345, "pm patched currentValue");
		expectSameParamInSet(pm2.getPatchedParamSet(), pm.getPatchedParamSet(), 5, "pm patched nodes");
		check(pm2.getUnpatchedParamSet()->params[2].currentValue == 678, "pm unpatched currentValue");

		PatchCableSet* pcs2 = pm2.getPatchCableSet();
		check(pcs2->numPatchCables == 1, "pm2 patch cable count");
		check(pcs2->patchCables[0].from == PatchSource::ENVELOPE_0, "pm2 cable source");
		check(pcs2->patchCables[0].destinationParamDescriptor.data
		          == pcs->patchCables[0].destinationParamDescriptor.data,
		      "pm2 cable destination");
		check(pcs2->patchCables[0].polarity == Polarity::BIPOLAR, "pm2 cable polarity");
		check(pcs2->patchCables[0].param.currentValue == pcs->patchCables[0].param.currentValue,
		      "pm2 cable amount value");
		{
			const auto& n2 = pcs2->docForParam(0)->nodes;
			const auto& n1 = pcs->docForParam(0)->nodes;
			check(n2.size() == n1.size(), "pm2 cable amount node count");
			for (size_t i = 0; i < n1.size() && i < n2.size(); i++) {
				check(n2[i].pos == n1[i].pos && n2[i].value == n1[i].value && n2[i].interpolated == n1[i].interpolated,
				      "pm2 cable amount node");
			}
		}
	}

	// Phase 5c-1: the DOM can *drive* the live manager — projecting a captured DocParamManager into a
	// manager that holds DIFFERENT/stale state fully restores it, including the summary automation flags
	// (set for newly-automated params, cleared for params that were automated only in the target).
	{
		ParamManagerForTimeline src;
		check(src.setupWithPatching() == Error::NONE, "5c1 src setup");
		src.getPatchedParamSet()->params[5].currentValue = 999;
		setNodeIn(*src.getPatchedParamSet()->docForParam(5), 0, 100, true); // automated in src
		setNodeIn(*src.getPatchedParamSet()->docForParam(5), 48, -200, false);
		src.getUnpatchedParamSet()->params[3].currentValue = 4242; // not automated in src

		deluge::project::DocParamManager doc = deluge::project::toDocParamManager(src);

		// Target starts with conflicting state: param 5 NOT automated, but param 9 IS automated (must clear).
		ParamManagerForTimeline dst;
		check(dst.setupWithPatching() == Error::NONE, "5c1 dst setup");
		setNodeIn(*dst.getPatchedParamSet()->docForParam(9), 0, 7, true);
		dst.getPatchedParamSet()->paramHasAutomationNow(dst.getPatchedParamSetSummary(), 9);
		dst.getPatchedParamSet()->params[5].currentValue = -1; // stale value

		deluge::project::fromDocParamManager(doc, dst);

		ParamCollectionSummary* psum = dst.getPatchedParamSetSummary();
		// param 5 restored + its automation flag set
		expectSameParamInSet(dst.getPatchedParamSet(), src.getPatchedParamSet(), 5, "5c1 param5 restored");
		check((psum->whichParamsAreAutomated[5 >> 5] & (1u << (5 & 31))) != 0, "5c1 param5 flag set");
		// param 9's stale automation cleared (nodes + flag)
		check(dst.getPatchedParamSet()->docForParam(9)->nodes.empty(), "5c1 param9 nodes cleared");
		check((psum->whichParamsAreAutomated[9 >> 5] & (1u << (9 & 31))) == 0, "5c1 param9 flag cleared");
		// unpatched value restored
		check(dst.getUnpatchedParamSet()->params[3].currentValue == 4242, "5c1 unpatched restored");
	}

	// Phase 4 brick: the generic patch-cable serializer must be byte-identical to the engine's
	// PatchCableSet::writePatchCablesToFile, including the range-cable (depthControlledBy) nesting.
	{
		ParamManagerForTimeline pm;
		check(pm.setupWithPatching() == Error::NONE, "pc-ser setup");
		PatchCableSet* pcs = pm.getPatchCableSet();
		pcs->numPatchCables = 3;
		// flat cable 0: ENVELOPE_0 -> param 7
		pcs->patchCables[0].from = PatchSource::ENVELOPE_0;
		pcs->patchCables[0].destinationParamDescriptor.setToHaveParamOnly(7);
		pcs->patchCables[0].polarity = Polarity::BIPOLAR;
		pcs->patchCables[0].param.currentValue = 536870912;
		pcs->docForParam(0)->current_value = 536870912;       // Option I: store holds the persisted scalar
		setNodeIn(*pcs->docForParam(0), 0, -536870912, true); // M2-F: cable nodes live in the cable's own store
		// flat cable 1: LFO_LOCAL_1 -> param 3
		pcs->patchCables[1].from = PatchSource::LFO_LOCAL_1;
		pcs->patchCables[1].destinationParamDescriptor.setToHaveParamOnly(3);
		pcs->patchCables[1].polarity = Polarity::UNIPOLAR;
		pcs->patchCables[1].param.currentValue = -100000;
		pcs->docForParam(1)->current_value = -100000;
		// range cable 2: LFO_LOCAL_2 controls the depth of cable 0 (param 7 + source ENVELOPE_0)
		pcs->patchCables[2].from = PatchSource::LFO_LOCAL_2;
		pcs->patchCables[2].destinationParamDescriptor.setToHaveParamAndSource(7, PatchSource::ENVELOPE_0);
		pcs->patchCables[2].polarity = Polarity::BIPOLAR;
		pcs->patchCables[2].param.currentValue = 12345;
		pcs->docForParam(2)->current_value = 12345;

		static char engineBuf[16384];
		static char domBuf[16384];

		smSerializer.setMemoryBuffer(engineBuf, sizeof(engineBuf));
		smSerializer.reset();
		pcs->writePatchCablesToFile(smSerializer, true);
		int32_t engineLen = smSerializer.bytesWritten();

		deluge::project::DocParamManager docpm = deluge::project::toDocParamManager(pm);
		smSerializer.setMemoryBuffer(domBuf, sizeof(domBuf));
		smSerializer.reset();
		deluge::project::serializePatchCables(docpm.patch_cables, smSerializer, true);
		int32_t domLen = smSerializer.bytesWritten();

		check(engineLen == domLen, "patch-cable serialize length matches engine");
		check(engineLen == domLen && std::memcmp(engineBuf, domBuf, engineLen) == 0,
		      "patch-cable serialize byte-identical to engine");

		// And the DOM serialize -> deserialize round-trip recovers the same cables (as a set; the
		// engine/DOM read orders range cables before the cable they control).
		const std::vector<deluge::project::DocPatchCable>& original = docpm.patch_cables;
		static char rtBuf[16384];
		smSerializer.setMemoryBuffer(rtBuf, sizeof(rtBuf));
		smSerializer.reset();
		smSerializer.writeOpeningTagBeginning("song");
		smSerializer.writeOpeningTagEnd();
		deluge::project::serializePatchCables(original, smSerializer, true);
		smSerializer.writeClosingTag("song");
		int32_t rtLen = smSerializer.bytesWritten();
		rtBuf[rtLen] = '\0';

		std::vector<deluge::project::DocPatchCable> recovered;
		XMLDeserializer reader(reinterpret_cast<uint8_t*>(rtBuf), rtLen);
		reader.openXMLFile(nullptr, "song");
		const char* tag;
		while (*(tag = reader.readNextTagOrAttributeName())) {
			if (!strcmp(tag, "patchCables")) {
				deluge::project::deserializePatchCables(recovered, reader, 2147483647);
			}
			reader.exitTag();
		}

		check(recovered.size() == original.size(), "patch-cable round-trip count");
		for (const auto& oc : original) {
			bool found = false;
			for (const auto& rc : recovered) {
				if (cableMatches(oc, rc)) {
					found = true;
					break;
				}
			}
			check(found, "patch-cable round-trip recovers cable");
		}
	}

	// Phase 4: writeDocParamAsAttribute (the owner-block building block) is byte-identical to
	// ParamSet::writeParamAsAttribute — automated param, plain param, and the onlyIfContainsSomething
	// skip (empty vs non-empty).
	{
		ParamManagerForTimeline pm;
		check(pm.setupWithPatching() == Error::NONE, "attr setup");
		UnpatchedParamSet* unp = pm.getUnpatchedParamSet();
		unp->params[2].currentValue = 424242; // automated
		setNodeIn(*unp->docForParam(2), 0, 10, true);
		setNodeIn(*unp->docForParam(2), 40, -20, false);
		unp->params[5].currentValue = -7; // plain, non-empty
		unp->params[8].currentValue = 0;  // empty

		deluge::project::DocParamSet doc = deluge::project::toDocParamSet(*unp);

		struct Case {
			const char* name;
			int p;
			bool onlyIf;
		};
		const Case cases[] = {{"alpha", 2, false}, {"beta", 5, false}, {"gamma", 8, true}, {"delta", 5, true}};
		for (const Case& cs : cases) {
			static char attrEngine[8192];
			static char attrDom[8192];
			smSerializer.setMemoryBuffer(attrEngine, sizeof(attrEngine));
			smSerializer.reset();
			unp->writeParamAsAttribute(smSerializer, cs.name, cs.p, true, cs.onlyIf);
			int32_t el = smSerializer.bytesWritten();
			smSerializer.setMemoryBuffer(attrDom, sizeof(attrDom));
			smSerializer.reset();
			deluge::project::writeDocParamAsAttribute(doc, cs.name, cs.p, smSerializer, true, cs.onlyIf);
			int32_t dl = smSerializer.bytesWritten();
			check(el == dl && std::memcmp(attrEngine, attrDom, el) == 0, "writeParamAsAttribute byte-identical");
		}
	}

	// Phase 4a: the ModControllableAudio shared param block (flat attrs + <equalizer>) serializes
	// byte-identical to the engine's static writeParamAttributesToFile / writeParamTagsToFile.
	{
		ParamManagerForTimeline pm;
		check(pm.setupWithPatching() == Error::NONE, "mca setup");
		UnpatchedParamSet* unp = pm.getUnpatchedParamSet();
		unp->params[params::UNPATCHED_STUTTER_RATE].currentValue = 111;
		unp->params[params::UNPATCHED_BITCRUSHING].currentValue = 222;
		setNodeIn(*unp->docForParam(params::UNPATCHED_ARP_GATE), 0, 50, true);
		unp->params[params::UNPATCHED_BASS].currentValue = -333; // equalizer group

		deluge::project::DocParamManager doc = deluge::project::toDocParamManager(pm);

		static char engBuf[16384];
		static char domBuf[16384];

		smSerializer.setMemoryBuffer(engBuf, sizeof(engBuf));
		smSerializer.reset();
		ModControllableAudio::writeParamAttributesToFile(smSerializer, &pm, true);
		int32_t engAttrLen = smSerializer.bytesWritten();
		smSerializer.setMemoryBuffer(domBuf, sizeof(domBuf));
		smSerializer.reset();
		deluge::project::serializeModControllableParamAttributes(*doc.unpatched, smSerializer, true);
		int32_t domAttrLen = smSerializer.bytesWritten();
		check(engAttrLen == domAttrLen && std::memcmp(engBuf, domBuf, engAttrLen) == 0,
		      "ModControllableAudio attributes byte-identical");

		smSerializer.setMemoryBuffer(engBuf, sizeof(engBuf));
		smSerializer.reset();
		ModControllableAudio::writeParamTagsToFile(smSerializer, &pm, true);
		int32_t engTagLen = smSerializer.bytesWritten();
		smSerializer.setMemoryBuffer(domBuf, sizeof(domBuf));
		smSerializer.reset();
		deluge::project::serializeModControllableParamTags(*doc.unpatched, smSerializer, true);
		int32_t domTagLen = smSerializer.bytesWritten();
		check(engTagLen == domTagLen && std::memcmp(engBuf, domBuf, engTagLen) == 0,
		      "ModControllableAudio tags byte-identical");
	}

	// Phase 5b-1: the ModControllableAudio block round-trips through the DOM reader — serialize via the DOM
	// into a wrapped <params> element, DOM-read it back into a fresh manager, and confirm values + the
	// automation summary flag match.
	{
		ParamManagerForTimeline pm;
		check(pm.setupWithPatching() == Error::NONE, "mca-rt setup");
		UnpatchedParamSet* unp = pm.getUnpatchedParamSet();
		unp->params[params::UNPATCHED_STUTTER_RATE].currentValue = 111;
		unp->params[params::UNPATCHED_BITCRUSHING].currentValue = 222;
		setNodeIn(*unp->docForParam(params::UNPATCHED_ARP_GATE), 0, 50, true); // automated
		setNodeIn(*unp->docForParam(params::UNPATCHED_ARP_GATE), 48, -99, false);
		unp->params[params::UNPATCHED_BASS].currentValue = -333; // equalizer group
		unp->params[params::UNPATCHED_ARP_SPREAD_OCTAVE].currentValue = 444;

		deluge::project::DocParamSet doc = deluge::project::toDocParamSet(*unp);

		static char rtBuf[16384];
		smSerializer.setMemoryBuffer(rtBuf, sizeof(rtBuf));
		smSerializer.reset();
		smSerializer.writeOpeningTagBeginning("params");
		deluge::project::serializeModControllableParamAttributes(doc, smSerializer, true);
		smSerializer.writeOpeningTagEnd();
		deluge::project::serializeModControllableParamTags(doc, smSerializer, true);
		smSerializer.writeClosingTag("params");
		int32_t rtLen = smSerializer.bytesWritten();
		rtBuf[rtLen] = '\0';

		ParamManagerForTimeline pm2;
		check(pm2.setupWithPatching() == Error::NONE, "mca-rt setup2");
		UnpatchedParamSet* unp2 = pm2.getUnpatchedParamSet();
		ParamCollectionSummary* sum2 = pm2.getUnpatchedParamSetSummary();
		XMLDeserializer reader(reinterpret_cast<uint8_t*>(rtBuf), rtLen);
		reader.openXMLFile(nullptr, "params");
		const char* tag;
		while (*(tag = reader.readNextTagOrAttributeName())) {
			if (!deluge::project::readModControllableParamTag(reader, tag, *unp2, *sum2, 2147483647)) {
				reader.exitTag();
			}
		}

		const int32_t ids[] = {params::UNPATCHED_STUTTER_RATE, params::UNPATCHED_BITCRUSHING,
		                       params::UNPATCHED_ARP_GATE, params::UNPATCHED_BASS, params::UNPATCHED_ARP_SPREAD_OCTAVE};
		for (int32_t id : ids) {
			expectSameParamInSet(unp2, unp, id, "mca-rt param");
		}
		check(
		    (sum2->whichParamsAreAutomated[params::UNPATCHED_ARP_GATE >> 5] & (1u << (params::UNPATCHED_ARP_GATE & 31)))
		        != 0,
		    "mca-rt automation flag set");
	}

	// Phase 4b: the GlobalEffectable param block (global flat attrs wrapping the shared ModControllableAudio
	// attrs + the <delay>/<lpf>/<hpf> groups) serializes byte-identical to the engine's static
	// GlobalEffectable::writeParamAttributesToFile / writeParamTagsToFile, including the conditional
	// pitchAdjust / sidechainCompressorVolume attrs.
	{
		ParamManagerForTimeline pm;
		check(pm.setupWithPatching() == Error::NONE, "ge setup");
		UnpatchedParamSet* unp = pm.getUnpatchedParamSet();
		unp->params[params::UNPATCHED_REVERB_SEND_AMOUNT].currentValue = 123;
		unp->params[params::UNPATCHED_VOLUME].currentValue = 456;
		unp->params[params::UNPATCHED_PITCH_ADJUST].currentValue = 789; // exercises the conditional attr
		setNodeIn(*unp->docForParam(params::UNPATCHED_SIDECHAIN_VOLUME), 0, 99, false); // automated ⇒ conditional fires
		unp->params[params::UNPATCHED_LPF_FREQ].currentValue = -111;                    // <lpf> group
		unp->params[params::UNPATCHED_DELAY_RATE].currentValue = 222;                   // <delay> group
		unp->params[params::UNPATCHED_STUTTER_RATE].currentValue = 333;                 // shared block
		unp->params[params::UNPATCHED_LPF_MORPH].currentValue = 444;                    // community attr
		authorStoreFromEngine(pm); // the flipped save wrapper reads the store

		deluge::project::DocParamManager doc = deluge::project::toDocParamManager(pm);

		static char engBuf[16384];
		static char domBuf[16384];

		smSerializer.setMemoryBuffer(engBuf, sizeof(engBuf));
		smSerializer.reset();
		GlobalEffectable::writeParamAttributesToFile(smSerializer, &pm, true);
		int32_t engAttrLen = smSerializer.bytesWritten();
		smSerializer.setMemoryBuffer(domBuf, sizeof(domBuf));
		smSerializer.reset();
		deluge::project::serializeGlobalEffectableParamAttributes(*doc.unpatched, smSerializer, true);
		int32_t domAttrLen = smSerializer.bytesWritten();
		check(engAttrLen == domAttrLen && std::memcmp(engBuf, domBuf, engAttrLen) == 0,
		      "GlobalEffectable attributes byte-identical");

		smSerializer.setMemoryBuffer(engBuf, sizeof(engBuf));
		smSerializer.reset();
		GlobalEffectable::writeParamTagsToFile(smSerializer, &pm, true);
		int32_t engTagLen = smSerializer.bytesWritten();
		smSerializer.setMemoryBuffer(domBuf, sizeof(domBuf));
		smSerializer.reset();
		deluge::project::serializeGlobalEffectableParamTags(*doc.unpatched, smSerializer, true);
		int32_t domTagLen = smSerializer.bytesWritten();
		check(engTagLen == domTagLen && std::memcmp(engBuf, domBuf, engTagLen) == 0,
		      "GlobalEffectable tags byte-identical");
	}

	// Phase 5b-2: the GlobalEffectable param block round-trips through the DOM reader (flat attrs +
	// delay/lpf/hpf groups + shared MCA), and the read path still accepts the legacy nested <lpf morph>.
	{
		ParamManagerForTimeline pm;
		check(pm.setupWithPatching() == Error::NONE, "ge-rt setup");
		UnpatchedParamSet* unp = pm.getUnpatchedParamSet();
		unp->params[params::UNPATCHED_REVERB_SEND_AMOUNT].currentValue = 123;
		unp->params[params::UNPATCHED_PITCH_ADJUST].currentValue = 789;          // conditional attr
		setNodeIn(*unp->docForParam(params::UNPATCHED_LPF_FREQ), 0, -111, true); // <lpf> group, automated
		unp->params[params::UNPATCHED_DELAY_RATE].currentValue = 222;            // <delay> group
		unp->params[params::UNPATCHED_HPF_RES].currentValue = -55;               // <hpf> group
		unp->params[params::UNPATCHED_LPF_MORPH].currentValue = 444;             // flat community attr
		unp->params[params::UNPATCHED_STUTTER_RATE].currentValue = 333;          // shared MCA attr
		unp->params[params::UNPATCHED_BASS].currentValue = -333;                 // equalizer group

		deluge::project::DocParamSet doc = deluge::project::toDocParamSet(*unp);

		static char rtBuf[16384];
		smSerializer.setMemoryBuffer(rtBuf, sizeof(rtBuf));
		smSerializer.reset();
		smSerializer.writeOpeningTagBeginning("params");
		deluge::project::serializeGlobalEffectableParamAttributes(doc, smSerializer, true);
		smSerializer.writeOpeningTagEnd();
		deluge::project::serializeGlobalEffectableParamTags(doc, smSerializer, true);
		smSerializer.writeClosingTag("params");
		int32_t rtLen = smSerializer.bytesWritten();
		rtBuf[rtLen] = '\0';

		ParamManagerForTimeline pm2;
		check(pm2.setupWithPatching() == Error::NONE, "ge-rt setup2");
		UnpatchedParamSet* unp2 = pm2.getUnpatchedParamSet();
		ParamCollectionSummary* sum2 = pm2.getUnpatchedParamSetSummary();
		XMLDeserializer reader(reinterpret_cast<uint8_t*>(rtBuf), rtLen);
		reader.openXMLFile(nullptr, "params");
		const char* tag;
		while (*(tag = reader.readNextTagOrAttributeName())) {
			if (!deluge::project::readGlobalEffectableParamTag(reader, tag, *unp2, *sum2, 2147483647)) {
				reader.exitTag();
			}
		}

		const int32_t ids[] = {
		    params::UNPATCHED_REVERB_SEND_AMOUNT, params::UNPATCHED_PITCH_ADJUST, params::UNPATCHED_LPF_FREQ,
		    params::UNPATCHED_DELAY_RATE,         params::UNPATCHED_HPF_RES,      params::UNPATCHED_LPF_MORPH,
		    params::UNPATCHED_STUTTER_RATE,       params::UNPATCHED_BASS};
		for (int32_t id : ids) {
			expectSameParamInSet(unp2, unp, id, "ge-rt param");
		}

		// Legacy back-compat: a hand-crafted <lpf morph="..."> (the writer emits flat lpfMorph, but old
		// files nested it) must still load into UNPATCHED_LPF_MORPH.
		deluge::project::DocParamSet morphDoc = deluge::project::toDocParamSet(*unp);
		morphDoc.params[params::UNPATCHED_LPF_MORPH].current_value = 0x5A5A;
		static char legacyBuf[4096];
		smSerializer.setMemoryBuffer(legacyBuf, sizeof(legacyBuf));
		smSerializer.reset();
		smSerializer.writeOpeningTagBeginning("params");
		smSerializer.writeOpeningTagEnd();
		const deluge::project::DocParamAttr lpfMorphMember[] = {{"morph", params::UNPATCHED_LPF_MORPH}};
		deluge::project::writeDocParamGroup(smSerializer, "lpf", lpfMorphMember, morphDoc, true);
		smSerializer.writeClosingTag("params");
		int32_t legacyLen = smSerializer.bytesWritten();
		legacyBuf[legacyLen] = '\0';

		ParamManagerForTimeline pm3;
		check(pm3.setupWithPatching() == Error::NONE, "ge-rt setup3");
		UnpatchedParamSet* unp3 = pm3.getUnpatchedParamSet();
		ParamCollectionSummary* sum3 = pm3.getUnpatchedParamSetSummary();
		XMLDeserializer legacyReader(reinterpret_cast<uint8_t*>(legacyBuf), legacyLen);
		legacyReader.openXMLFile(nullptr, "params");
		while (*(tag = legacyReader.readNextTagOrAttributeName())) {
			if (!deluge::project::readGlobalEffectableParamTag(legacyReader, tag, *unp3, *sum3, 2147483647)) {
				legacyReader.exitTag();
			}
		}
		check(unp3->params[params::UNPATCHED_LPF_MORPH].currentValue == 0x5A5A, "ge-rt legacy <lpf morph> loads");
	}

	// Phase 4c: the ExpressionParamSet (MPE) block serializes byte-identical to the engine's
	// ExpressionParamSet::writeToFile (the <expressionData> group with pitchBend/yExpression/pressure).
	{
		ParamManagerForTimeline pm;
		check(pm.setupWithPatching() == Error::NONE, "expr setup");
		ExpressionParamSet* e = pm.getOrCreateExpressionParamSet();
		check(e != nullptr, "expr param set created");
		e->params[0].currentValue = 12345;            // pitchBend
		setNodeIn(*e->docForParam(2), 0, 6789, true); // pressure, automated
		// yExpression (index 1) left neutral ⇒ exercises the per-dimension skip.
		authorStoreFromEngine(*e); // the flipped ExpressionParamSet::writeToFile reads the store

		deluge::project::DocParamManager doc = deluge::project::toDocParamManager(pm);
		check(doc.expression.has_value(), "expr present in doc");

		static char engBuf[16384];
		static char domBuf[16384];

		smSerializer.setMemoryBuffer(engBuf, sizeof(engBuf));
		smSerializer.reset();
		bool engWrote = e->writeToFile(smSerializer, false);
		int32_t engLen = smSerializer.bytesWritten();
		smSerializer.setMemoryBuffer(domBuf, sizeof(domBuf));
		smSerializer.reset();
		bool domWrote = deluge::project::serializeExpressionParams(*doc.expression, smSerializer, false, true);
		int32_t domLen = smSerializer.bytesWritten();
		check(engWrote == domWrote, "expression written-flag matches");
		check(engLen == domLen && std::memcmp(engBuf, domBuf, engLen) == 0, "ExpressionParamSet byte-identical");
	}

	// Phase 5b-4: the ExpressionParamSet (MPE) block round-trips through the DOM reader, and the read path
	// still accepts the legacy `channelPressure` alias (pressure / dimension 2).
	{
		ParamManagerForTimeline pm;
		check(pm.setupWithPatching() == Error::NONE, "expr-rt setup");
		ExpressionParamSet* e = pm.getOrCreateExpressionParamSet();
		check(e != nullptr, "expr-rt set created");
		e->params[0].currentValue = 12345;            // pitchBend
		setNodeIn(*e->docForParam(2), 0, 6789, true); // pressure, automated

		deluge::project::DocParamSet doc = deluge::project::toDocParamSet(*e);

		static char rtBuf[16384];
		smSerializer.setMemoryBuffer(rtBuf, sizeof(rtBuf));
		smSerializer.reset();
		smSerializer.writeOpeningTagBeginning("song");
		smSerializer.writeOpeningTagEnd();
		deluge::project::serializeExpressionParams(doc, smSerializer, false, true);
		smSerializer.writeClosingTag("song");
		int32_t rtLen = smSerializer.bytesWritten();
		rtBuf[rtLen] = '\0';

		ParamManagerForTimeline pm2;
		check(pm2.setupWithPatching() == Error::NONE, "expr-rt setup2");
		ExpressionParamSet* e2 = pm2.getOrCreateExpressionParamSet();
		ParamCollectionSummary* esum2 = pm2.getExpressionParamSetSummary();
		XMLDeserializer reader(reinterpret_cast<uint8_t*>(rtBuf), rtLen);
		reader.openXMLFile(nullptr, "song");
		const char* tag;
		while (*(tag = reader.readNextTagOrAttributeName())) {
			if (!strcmp(tag, "expressionData")) {
				reader.match('{');
				const char* inner;
				while (*(inner = reader.readNextTagOrAttributeName())) {
					if (!deluge::project::readExpressionParamTag(reader, inner, *e2, *esum2, 2147483647)) {
						reader.exitTag();
					}
				}
				reader.exitTag("expressionData", true);
			}
			else {
				reader.exitTag();
			}
		}
		expectSameParamInSet(e2, e, 0, "expr-rt pitchBend");
		expectSameParamInSet(e2, e, 2, "expr-rt pressure");
		check((esum2->whichParamsAreAutomated[2 >> 5] & (1u << (2 & 31))) != 0, "expr-rt pressure automation flag");

		// Legacy back-compat: <channelPressure> loads into dimension 2 via the engine tail in readFromFile.
		ParamManagerForTimeline pm3;
		check(pm3.setupWithPatching() == Error::NONE, "expr-rt setup3");
		ExpressionParamSet* e3 = pm3.getOrCreateExpressionParamSet();
		ParamCollectionSummary* esum3 = pm3.getExpressionParamSetSummary();
		static char legacyBuf[2048];
		const char* legacyXml = "<expressionData\n channelPressure=\"0x00001A2B\" />\n";
		std::strcpy(legacyBuf, legacyXml);
		int legacyLen = static_cast<int>(std::strlen(legacyBuf));
		XMLDeserializer legacyReader(reinterpret_cast<uint8_t*>(legacyBuf), legacyLen);
		legacyReader.openXMLFile(nullptr, "expressionData");
		e3->readFromFile(legacyReader, esum3, 2147483647);
		check(e3->params[2].currentValue == 0x00001A2B, "expr-rt legacy channelPressure loads");
	}

	// Phase 4d: the Sound param block serializes byte-identical to the engine's Sound::writeParamsToFile
	// (patched + unpatched flat attrs, the shared ModControllableAudio attrs, the four <envelope*> groups,
	// the <patchCables> block, and the shared tags — including the mid-block writeOpeningTagEnd).
	{
		ParamManagerForTimeline pm;
		check(pm.setupWithPatching() == Error::NONE, "sound setup");
		PatchedParamSet* pp = pm.getPatchedParamSet();
		UnpatchedParamSet* up = pm.getUnpatchedParamSet();
		pp->params[params::LOCAL_OSC_A_VOLUME].currentValue = 12345;
		pp->params[params::LOCAL_PITCH_ADJUST].currentValue = 678;              // conditional attr — written
		setNodeIn(*pp->docForParam(params::LOCAL_ENV_0_ATTACK), 0, 4242, true); // envelope group, automated
		pp->params[params::GLOBAL_REVERB_AMOUNT].currentValue = -999;
		pp->params[params::LOCAL_LPF_MORPH].currentValue = 111; // community attr
		up->params[params::UNPATCHED_PORTAMENTO].currentValue = 222;
		up->params[params::UNPATCHED_STUTTER_RATE].currentValue = 333; // shared block

		// A patch cable so the <patchCables> block is exercised within the Sound layout.
		PatchCableSet* pcs = pm.getPatchCableSet();
		pcs->numPatchCables = 1;
		pcs->patchCables[0].from = PatchSource::LFO_GLOBAL_1;
		pcs->patchCables[0].destinationParamDescriptor.setToHaveParamOnly(params::LOCAL_LPF_FREQ);
		pcs->patchCables[0].polarity = Polarity::BIPOLAR;
		pcs->patchCables[0].param.currentValue = 268435456;
		// The flipped Sound::writeParamsToFile reads the store for params AND the cable amount.
		authorStoreFromEngine(pm);
		pcs->docForParam(0)->current_value = 268435456;

		deluge::project::DocParamManager doc = deluge::project::toDocParamManager(pm);
		check(doc.patched.has_value() && doc.unpatched.has_value(), "sound collections present");

		SoundDrum drum;

		static char engBuf[16384];
		static char domBuf[16384];

		smSerializer.setMemoryBuffer(engBuf, sizeof(engBuf));
		smSerializer.reset();
		drum.writeParamsToFile(smSerializer, &pm, true);
		int32_t engLen = smSerializer.bytesWritten();
		smSerializer.setMemoryBuffer(domBuf, sizeof(domBuf));
		smSerializer.reset();
		deluge::project::serializeSoundParams(*doc.patched, *doc.unpatched, doc.patch_cables, smSerializer, true);
		int32_t domLen = smSerializer.bytesWritten();
		check(engLen == domLen && std::memcmp(engBuf, domBuf, engLen) == 0, "Sound params byte-identical");
	}

	// Phase 5b-3: the Sound param block round-trips through the DOM reader (patched + unpatched flat attrs,
	// the <envelope*> groups, the shared MCA block). Patch cables are excluded here — they keep the engine
	// reader (pre-V3.2 back-compat) — so no cables are set.
	{
		ParamManagerForTimeline pm;
		check(pm.setupWithPatching() == Error::NONE, "sound-rt setup");
		PatchedParamSet* pp = pm.getPatchedParamSet();
		UnpatchedParamSet* up = pm.getUnpatchedParamSet();
		pp->params[params::LOCAL_OSC_A_VOLUME].currentValue = 12345;
		setNodeIn(*pp->docForParam(params::LOCAL_ENV_0_ATTACK), 0, 4242, true); // envelope group, automated
		pp->params[params::GLOBAL_REVERB_AMOUNT].currentValue = -999;
		pp->params[params::LOCAL_LPF_MORPH].currentValue = 111; // community attr
		pp->params[params::LOCAL_PITCH_ADJUST].currentValue = 678;
		up->params[params::UNPATCHED_PORTAMENTO].currentValue = 222;
		up->params[params::UNPATCHED_STUTTER_RATE].currentValue = 333; // shared MCA attr

		deluge::project::DocParamManager doc = deluge::project::toDocParamManager(pm);

		static char rtBuf[16384];
		smSerializer.setMemoryBuffer(rtBuf, sizeof(rtBuf));
		smSerializer.reset();
		smSerializer.writeOpeningTagBeginning("soundParams");
		deluge::project::serializeSoundParams(*doc.patched, *doc.unpatched, {}, smSerializer, true);
		smSerializer.writeClosingTag("soundParams", true);
		int32_t rtLen = smSerializer.bytesWritten();
		rtBuf[rtLen] = '\0';

		ParamManagerForTimeline pm2;
		check(pm2.setupWithPatching() == Error::NONE, "sound-rt setup2");
		PatchedParamSet* pp2 = pm2.getPatchedParamSet();
		UnpatchedParamSet* up2 = pm2.getUnpatchedParamSet();
		ParamCollectionSummary* psum2 = pm2.getPatchedParamSetSummary();
		ParamCollectionSummary* usum2 = pm2.getUnpatchedParamSetSummary();
		XMLDeserializer reader(reinterpret_cast<uint8_t*>(rtBuf), rtLen);
		reader.openXMLFile(nullptr, "soundParams");
		const char* tag;
		while (*(tag = reader.readNextTagOrAttributeName())) {
			if (!deluge::project::readSoundParamTag(reader, tag, *pp2, *psum2, *up2, *usum2, 2147483647)) {
				reader.exitTag();
			}
		}

		const int32_t patchedIds[] = {params::LOCAL_OSC_A_VOLUME, params::LOCAL_ENV_0_ATTACK,
		                              params::GLOBAL_REVERB_AMOUNT, params::LOCAL_LPF_MORPH,
		                              params::LOCAL_PITCH_ADJUST};
		for (int32_t id : patchedIds) {
			expectSameParamInSet(pp2, pp, id, "sound-rt patched");
		}
		expectSameParamInSet(up2, up, params::UNPATCHED_PORTAMENTO, "sound-rt portamento");
		expectSameParamInSet(up2, up, params::UNPATCHED_STUTTER_RATE, "sound-rt mca");
		check((psum2->whichParamsAreAutomated[params::LOCAL_ENV_0_ATTACK >> 5]
		       & (1u << (params::LOCAL_ENV_0_ATTACK & 31)))
		          != 0,
		      "sound-rt envelope automation flag set");
	}

	// Phase 4e: the Song value-override path. Song serializes its global-effectable block with a
	// valuesForOverride array (the "params in automation mode" snapshot); for an automated param the
	// override value is written in place of currentValue. The DOM serializer must match byte-for-byte.
	{
		ParamManagerForTimeline pm;
		check(pm.setupWithPatching() == Error::NONE, "override setup");
		UnpatchedParamSet* up = pm.getUnpatchedParamSet();
		setNodeIn(*up->docForParam(params::UNPATCHED_VOLUME), 0, 4242, true); // automated ⇒ override applies
		up->params[params::UNPATCHED_PAN].currentValue = 555;                 // not automated ⇒ override ignored
		setNodeIn(*up->docForParam(params::UNPATCHED_BASS), 0, 7, false);     // automated, equalizer group
		// An automated ModControllableAudio arp attr: the engine writes it WITHOUT the override (only the
		// first six MCA attrs honour valuesForOverride), so the override value must be ignored here.
		setNodeIn(*up->docForParam(params::UNPATCHED_ARP_GATE), 0, 13, true);
		setNodeIn(*up->docForParam(params::UNPATCHED_STUTTER_RATE), 0, 21, true); // override-eligible MCA attr
		authorStoreFromEngine(pm); // the flipped GlobalEffectable::writeParamAttributesToFile reads the store

		deluge::project::DocParamManager doc = deluge::project::toDocParamManager(pm);
		check(doc.unpatched.has_value(), "override unpatched present");

		std::vector<int32_t> ovr(doc.unpatched->params.size(), 0);
		ovr[params::UNPATCHED_VOLUME] = -123456; // distinct from currentValue (0) so the path is observable
		ovr[params::UNPATCHED_PAN] = 999;        // would differ, but PAN isn't automated ⇒ must be ignored
		ovr[params::UNPATCHED_BASS] = -77;
		ovr[params::UNPATCHED_ARP_GATE] = -555;     // must be ignored (arp attr, no override)
		ovr[params::UNPATCHED_STUTTER_RATE] = -999; // must apply (override-eligible MCA attr)

		static char engBuf[16384];
		static char domBuf[16384];

		smSerializer.setMemoryBuffer(engBuf, sizeof(engBuf));
		smSerializer.reset();
		GlobalEffectable::writeParamAttributesToFile(smSerializer, &pm, true, ovr.data());
		int32_t engAttrLen = smSerializer.bytesWritten();
		smSerializer.setMemoryBuffer(domBuf, sizeof(domBuf));
		smSerializer.reset();
		deluge::project::serializeGlobalEffectableParamAttributes(*doc.unpatched, smSerializer, true, ovr.data());
		int32_t domAttrLen = smSerializer.bytesWritten();
		check(engAttrLen == domAttrLen && std::memcmp(engBuf, domBuf, engAttrLen) == 0,
		      "GlobalEffectable override attributes byte-identical");

		smSerializer.setMemoryBuffer(engBuf, sizeof(engBuf));
		smSerializer.reset();
		GlobalEffectable::writeParamTagsToFile(smSerializer, &pm, true, ovr.data());
		int32_t engTagLen = smSerializer.bytesWritten();
		smSerializer.setMemoryBuffer(domBuf, sizeof(domBuf));
		smSerializer.reset();
		deluge::project::serializeGlobalEffectableParamTags(*doc.unpatched, smSerializer, true, ovr.data());
		int32_t domTagLen = smSerializer.bytesWritten();
		check(engTagLen == domTagLen && std::memcmp(engBuf, domBuf, engTagLen) == 0,
		      "GlobalEffectable override tags byte-identical");
	}

	// Phase 5c (Option B): the DOM-target reader lands the GlobalEffectable block in a DocParamSet (the
	// persistent DOM store); projecting that store into an engine must be identical to reading the same XML
	// straight into the engine (the 5b path). This proves the read-into-DOM → reproject-DOM→engine loop.
	{
		ParamManagerForTimeline pm;
		check(pm.setupWithPatching() == Error::NONE, "5cB setup");
		UnpatchedParamSet* unp = pm.getUnpatchedParamSet();
		unp->params[params::UNPATCHED_REVERB_SEND_AMOUNT].currentValue = 123;
		unp->params[params::UNPATCHED_PITCH_ADJUST].currentValue = 789;
		setNodeIn(*unp->docForParam(params::UNPATCHED_LPF_FREQ), 0, -111, true); // <lpf> group, automated
		setNodeIn(*unp->docForParam(params::UNPATCHED_LPF_FREQ), 48, 222, false);
		unp->params[params::UNPATCHED_DELAY_RATE].currentValue = 222;   // <delay> group
		unp->params[params::UNPATCHED_HPF_RES].currentValue = -55;      // <hpf> group
		unp->params[params::UNPATCHED_LPF_MORPH].currentValue = 444;    // flat community attr
		unp->params[params::UNPATCHED_STUTTER_RATE].currentValue = 333; // shared MCA attr
		unp->params[params::UNPATCHED_BASS].currentValue = -333;        // equalizer group

		deluge::project::DocParamSet src = deluge::project::toDocParamSet(*unp);
		static char rtBuf[16384];
		smSerializer.setMemoryBuffer(rtBuf, sizeof(rtBuf));
		smSerializer.reset();
		smSerializer.writeOpeningTagBeginning("params");
		deluge::project::serializeGlobalEffectableParamAttributes(src, smSerializer, true);
		smSerializer.writeOpeningTagEnd();
		deluge::project::serializeGlobalEffectableParamTags(src, smSerializer, true);
		smSerializer.writeClosingTag("params");
		int32_t rtLen = smSerializer.bytesWritten();
		rtBuf[rtLen] = '\0';

		// XMLDeserializer tokenizes its buffer in place, so each read path gets its own copy of the bytes.
		static char bufA[16384];
		static char bufB[16384];
		std::memcpy(bufA, rtBuf, rtLen + 1);
		std::memcpy(bufB, rtBuf, rtLen + 1);

		// Path A — engine reader (5b): XML -> engine.
		ParamManagerForTimeline pmEng;
		check(pmEng.setupWithPatching() == Error::NONE, "5cB engine setup");
		UnpatchedParamSet* unpEng = pmEng.getUnpatchedParamSet();
		ParamCollectionSummary* sumEng = pmEng.getUnpatchedParamSetSummary();
		{
			XMLDeserializer reader(reinterpret_cast<uint8_t*>(bufA), rtLen);
			reader.openXMLFile(nullptr, "params");
			const char* tag;
			while (*(tag = reader.readNextTagOrAttributeName())) {
				if (!deluge::project::readGlobalEffectableParamTag(reader, tag, *unpEng, *sumEng, 2147483647)) {
					reader.exitTag();
				}
			}
		}

		// Path B — DOM reader (Option B): XML -> DocParamSet store, then project store -> engine.
		deluge::project::DocParamSet store = deluge::project::toDocParamSet(*pm.getUnpatchedParamSet());
		for (auto& n : store.params) {
			n = deluge::project::DocAutoParam{}; // start empty; the reader fills present tags
		}
		{
			XMLDeserializer reader(reinterpret_cast<uint8_t*>(bufB), rtLen);
			reader.openXMLFile(nullptr, "params");
			const char* tag;
			while (*(tag = reader.readNextTagOrAttributeName())) {
				if (!deluge::project::readGlobalEffectableParamTagToDoc(reader, tag, store, 2147483647)) {
					reader.exitTag();
				}
			}
		}
		ParamManagerForTimeline pmDom;
		check(pmDom.setupWithPatching() == Error::NONE, "5cB dom setup");
		deluge::project::fromDocParamSet(store, *pmDom.getUnpatchedParamSet());

		const int32_t ids[] = {
		    params::UNPATCHED_REVERB_SEND_AMOUNT, params::UNPATCHED_PITCH_ADJUST, params::UNPATCHED_LPF_FREQ,
		    params::UNPATCHED_DELAY_RATE,         params::UNPATCHED_HPF_RES,      params::UNPATCHED_LPF_MORPH,
		    params::UNPATCHED_STUTTER_RATE,       params::UNPATCHED_BASS};
		for (int32_t id : ids) {
			expectSameParamInSet(pmDom.getUnpatchedParamSet(), pmEng.getUnpatchedParamSet(), id,
			                     "5cB dom-read == engine-read");
		}
	}

	// Phase 2 (2b): the DOM-first value edit primitive writes the store lane (authoritative) and reprojects it
	// into the engine runtime param, so both hold the new value; out-of-range is a safe no-op.
	{
		deluge::project::DocParamManager store;
		store.unpatched.emplace();
		store.unpatched->params.resize(10);
		AutoParam eng;
		bool ok = deluge::project::setUnpatchedValueDomFirst(store, 3, 42424, eng);
		check(ok, "setUnpatchedValueDomFirst: applied");
		check(store.unpatched->params[3].current_value == 42424, "setUnpatchedValueDomFirst: store lane written");
		check(eng.getCurrentValue() == 42424, "setUnpatchedValueDomFirst: engine reprojected from store");
		check(!deluge::project::setUnpatchedValueDomFirst(store, 99, 1, eng),
		      "setUnpatchedValueDomFirst: out-of-range is a no-op");
		deluge::project::DocParamManager empty;
		check(!deluge::project::setUnpatchedValueDomFirst(empty, 0, 1, eng),
		      "setUnpatchedValueDomFirst: unpopulated store is a no-op");
	}

	// M2-F (lazy owner store): a ParamSet ALWAYS resolves to a DOM store — the first owner-resolved access
	// (docForParam) lazily creates + binds the set's own store; the AutoParam's inline storage is never used. A
	// copy of a bound set gets its OWN deep copy of the store (independent nodes; no aliasing).
	{
		ParamManagerForTimeline pm;
		check(pm.setupWithPatching() == Error::NONE, "lazy setup");
		UnpatchedParamSet* unp = pm.getUnpatchedParamSet();
		check(unp->domStore() == nullptr, "lazy: unbound before first access");
		// First owner-resolved access lazily binds the store; edits land there.
		setNodeIn(*unp->docForParam(2), 0, 111, false);
		setNodeIn(*unp->docForParam(2), 48, -222, true);
		unp->params[5].currentValue = 4242; // unautomated

		deluge::project::DocParamSet* store = unp->domStore();
		check(store != nullptr, "lazy: store bound on first docForParam");
		check(static_cast<int32_t>(store->params.size()) == unp->getNumParams(), "lazy: store sized to count");
		check(unp->docForParam(2) == &store->params[2], "lazy: owner resolves the param to its store slot");
		check(store->params[2].nodes.size() == 2 && store->params[2].nodes[0].value == 111,
		      "lazy: authored nodes live in the store");
		// A subsequent edit (owner-resolved) lands in the store.
		setNodeIn(*unp->docForParam(2), 24, 999, false);
		check(store->params[2].nodes.size() == 3, "lazy: edit lands in the store");
		// bindDomStore is now just "ensure bound" — idempotent once resolved.
		unp->bindDomStore();
		check(unp->domStore() == store, "lazy: bindDomStore idempotent (no realloc)");

		// A copy of a bound set gets an INDEPENDENT deep copy of the store.
		UnpatchedParamSet copy(*unp);
		check(copy.domStore() != nullptr && copy.domStore() != store, "lazy: a copy owns an independent store");
		check(copy.docForParam(2) != &store->params[2], "lazy: copy's store slot is not aliased");
		check(copy.docForParam(2)->nodes.size() == store->params[2].nodes.size(), "lazy: copy has same node count");
	}

	// Phase C: ParamManager::bindMainParamSetDomStores binds ALL of a sound manager's automatable collections in
	// one call — unpatched + patched + expression (ParamSets) + patch cables (a non-ParamSet with its own store).
	// The corpus has no MPE data, so this is where expression binding is exercised.
	{
		// A bound ParamSet resolves each param to its owned store slot (owner-resolution), and the store holds the
		// authored nodes.
		auto liveBound = [](ParamSet* s) {
			return s->domStore() != nullptr && s->docForParam(0) == &s->domStore()->params[0];
		};
		ParamManagerForTimeline pm;
		check(pm.setupWithPatching() == Error::NONE, "helper: setup");
		ExpressionParamSet* expr = pm.getOrCreateExpressionParamSet();
		check(expr != nullptr, "helper: expression set created");
		// A patch cable so the cable path is exercised.
		PatchCableSet* pcs = pm.getPatchCableSet();
		pcs->numPatchCables = 1;
		pcs->patchCables[0].from = PatchSource::ENVELOPE_0;
		pcs->patchCables[0].destinationParamDescriptor.setToHaveParamOnly(7);
		setNodeIn(*pcs->docForParam(0), 0, 424242, true);

		pm.bindMainParamSetDomStores();
		check(liveBound(pm.getUnpatchedParamSet()), "helper: unpatched bound");
		check(liveBound(pm.getPatchedParamSet()), "helper: patched bound");
		check(liveBound(pm.getExpressionParamSet()), "helper: expression bound");
		// M2-F: patch cables are reordered by index at runtime, so they are NOT bound to a positional store —
		// the cable owns its node store inline (amount_) and owner-resolution returns it (with the authored node).
		check(pcs->domStore() == nullptr, "helper: patch cable set is not bound to a shared store");
		check(pcs->docForParam(0) == &pcs->patchCables[0].amount_, "helper: patch cable resolves to its inline store");
		check(pcs->docForParam(0)->nodes.size() == 1 && pcs->docForParam(0)->nodes[0].value == 424242,
		      "helper: patch cable automation intact");
	}

	// M2-F (MIDI-CC): the MIDIParamVector reorders/relocates by CC on every learn, so it is NOT bound to a
	// positional store — each CC owns its nodes inline (MIDIParam::nodes_) and they ride with the element through
	// the vector's growth. Owner-resolution (docParamForId(cc)) finds the CC's inline store, and the automation
	// survives learning a new CC. The corpus has no MIDI-CC automation, so this is the only coverage.
	{
		ParamManagerForTimeline pm;
		check(pm.setupMIDI() == Error::NONE, "midi: setup");
		MIDIParamCollection* mpc = pm.getMIDIParamCollection();
		setNodeIn(mpc->params.getOrCreateParamFromCC(10, 0)->nodes_, 0, 111, true);
		setNodeIn(mpc->params.getOrCreateParamFromCC(20, 0)->nodes_, 0, 222, false);
		check(mpc->params.size() == 2, "midi: two CCs authored");

		mpc->bindDomStore(); // no-op: ordered collections keep nodes inline
		check(mpc->domStore() == nullptr, "midi: not bound to a shared store");
		// Owner-resolution by CC returns each CC's inline store.
		check(mpc->docParamForId(10) == &mpc->params.getParamFromCC(10)->nodes_,
		      "midi: docParamForId resolves the CC's inline store");

		// Learn a third CC (15) — the MIDIParamVector grows/relocates its elements.
		mpc->params.getOrCreateParamFromCC(15, 0);
		check(mpc->params.size() == 3, "midi: third CC learned (vector grew)");
		// Re-resolve cc10 (relocated) — its automation is intact (nodes rode with the AutoParam's move) and
		// owner-resolution by CC still finds it, even though its array INDEX shifted.
		deluge::project::DocAutoParam* cc10 = mpc->docParamForId(10);
		check(cc10 != nullptr && cc10->nodes.size() == 1 && cc10->nodes[0].value == 111,
		      "midi: automation intact + owner-resolved after vector grow");
	}

	// M2-E (owner-resolution / C4): the DOM store survives a ParamBackup steal -> restore. The store is anchored
	// to the ParamSet (its RAII domStore_), and stealParamCollectionsFrom moves only the summary POINTER (the
	// ParamSet object never relocates), so owner-resolution keeps resolving the same store slots while parked and
	// after restore. This is the golden-blind lifetime the load/save harness cannot see.
	{
		ParamManagerForTimeline live;
		check(live.setupWithPatching() == Error::NONE, "steal: live setup");
		live.getUnpatchedParamSet()->bindDomStore();
		UnpatchedParamSet* liveUnp = live.getUnpatchedParamSet();
		deluge::project::DocParamSet* storeAddr = liveUnp->domStore();
		check(storeAddr != nullptr, "steal: live set is bound");
		setNodeIn(*liveUnp->docForParam(3), 0, 555, true);
		setNodeIn(*liveUnp->docForParam(3), 48, -666, false);
		void* setAddrBefore = liveUnp; // the ParamSet object's address — must not change across steal

		// A synth clip binds BOTH its main sets — bind the patched set too, so we prove they survive the steal
		// together (their two separate DOM stores each travel with their own ParamSet).
		live.getPatchedParamSet()->bindDomStore();
		PatchedParamSet* livePat = live.getPatchedParamSet();
		deluge::project::DocParamSet* patStoreAddr = livePat->domStore();
		check(patStoreAddr != nullptr && patStoreAddr != storeAddr, "steal: patched set separately bound");
		setNodeIn(*livePat->docForParam(5), 0, 777, true);
		void* patSetAddrBefore = livePat;

		deluge::project::ParamBackup backup;
		auto* mc = reinterpret_cast<ModControllableAudio*>(0x1000); // pure map keys; never dereferenced
		auto* clipKey = reinterpret_cast<Clip*>(0x2000);

		// Steal the live manager's collections into the backup ("park" it, as clip teardown does).
		backup.backUp(mc, clipKey, &live);
		ParamManager* parked = backup.getForExactClip(mc, clipKey); // peek, no stealInto
		check(parked != nullptr, "steal: parked entry present");
		UnpatchedParamSet* parkedUnp = parked->getUnpatchedParamSet();
		check(static_cast<void*>(parkedUnp) == setAddrBefore, "steal: ParamSet object did not relocate");
		check(parkedUnp->domStore() == storeAddr, "steal: DOM store address unchanged while parked");
		check(parkedUnp->docForParam(3) == &storeAddr->params[3],
		      "steal: owner still resolves the store slot while parked");
		check(storeAddr->params[3].nodes.size() == 2, "steal: automation intact while parked");
		// An edit while parked lands in the same DOM store (owner-resolved).
		setNodeIn(*parkedUnp->docForParam(3), 24, 999, false);
		check(storeAddr->params[3].nodes.size() == 3, "steal: edit while parked lands in the DOM store");
		// The patched set travelled too, with its own store.
		PatchedParamSet* parkedPat = parked->getPatchedParamSet();
		check(static_cast<void*>(parkedPat) == patSetAddrBefore, "steal: patched ParamSet did not relocate");
		check(parkedPat->domStore() == patStoreAddr, "steal: patched store unchanged while parked");
		check(parkedPat->docForParam(5) == &patStoreAddr->params[5],
		      "steal: patched owner still resolves its store slot while parked");
		check(patStoreAddr->params[5].nodes.size() == 1, "steal: patched automation intact while parked");

		// Restore back into the (now-emptied) live manager, as solicitParamManager does on re-add.
		ParamManager* restored = backup.getForExactClip(mc, clipKey, &live); // steals collections back, erases entry
		check(restored == &live, "steal: restored into the live manager");
		UnpatchedParamSet* restUnp = live.getUnpatchedParamSet();
		check(static_cast<void*>(restUnp) == setAddrBefore, "steal: restored the same ParamSet object");
		check(restUnp->domStore() == storeAddr, "steal: same DOM store after restore");
		check(restUnp->docForParam(3)->nodes.size() == 3 && restUnp->docForParam(3)->nodes[0].value == 555,
		      "steal: automation survived the round trip");
		PatchedParamSet* restPat = live.getPatchedParamSet();
		check(static_cast<void*>(restPat) == patSetAddrBefore && restPat->domStore() == patStoreAddr,
		      "steal: patched set + its store survived restore");
		check(restPat->docForParam(5)->nodes.size() == 1 && restPat->docForParam(5)->nodes[0].value == 777,
		      "steal: patched automation survived restore");
		check(backup.getForExactClip(mc, clipKey) == nullptr, "steal: backup entry consumed by restore");
		// `live` (holding the ParamSet + its RAII DOM store) is destructed at scope exit: params_ die before
		// domStore_, so nothing outlives the store — a clean teardown (no dangle / double free).
	}

	{
		// Movement 2: flattenReverse oracle diff — reverses node positions within the loop; current_value is
		// left untouched (only the node array is rebuilt).
		struct N {
			int32_t pos;
			int32_t value;
			bool interpolated;
		};
		const std::vector<std::vector<N>> flattenConfigs = {
		    {{0, 100, true}, {24, -50, true}, {40, 200, false}},       // node at 0 + interior + stepped
		    {{8, 10, true}, {32, -10, true}},                          // nothing at 0
		    {{0, 7, false}},                                           // single stepped node at 0
		    {{16, 5, true}},                                           // single node, nothing at 0
		    {{0, 1, true}, {1, 2, true}, {2, 3, false}, {3, 4, true}}, // dense run, exercises tight wrap
		};
		const int32_t reverseLengths[] = {48, 4, 1};
		for (const auto& cfg : flattenConfigs) {
			for (int32_t length : reverseLengths) {
				AutoParam ap;
				deluge::project::DocAutoParam apDoc;
				ap.currentValue = 42;
				DocAutoParam doc{};
				doc.current_value = 42;
				for (const N& n : cfg) {
					setNodeIn(apDoc, n.pos, n.value, n.interpolated);
					deluge::project::setNodeAtPos(doc, n.pos, n.value, n.interpolated);
				}
				ap.flattenReverse(*(&apDoc), length);
				deluge::project::flattenReverse(doc, length);
				check(ap.currentValue == doc.current_value,
				      "flattenReverse: current_value untouched, matches engine oracle");
				expectDocMatchesEngine(apDoc, doc, "flattenReverse: matches engine oracle");
			}
		}

		// reverseDirectionWithLength == 0 is a documented no-op.
		{
			AutoParam ap;
			deluge::project::DocAutoParam apDoc;
			ap.currentValue = 7;
			DocAutoParam doc{};
			doc.current_value = 7;
			setNodeIn(apDoc, 8, 5, true);
			deluge::project::setNodeAtPos(doc, 8, 5, true);
			ap.flattenReverse(*(&apDoc), 0);
			deluge::project::flattenReverse(doc, 0);
			check(ap.currentValue == doc.current_value, "flattenReverse: length==0 leaves current_value alone");
			expectDocMatchesEngine(apDoc, doc, "flattenReverse: reverseDirectionWithLength==0 is a no-op");
		}

		// Empty nodes: also a no-op, and mustn't crash indexing oldNodes[0]/oldNodes[numNodes-1].
		{
			AutoParam ap;
			deluge::project::DocAutoParam apDoc;
			DocAutoParam doc{};
			ap.flattenReverse(*(&apDoc), 48);
			deluge::project::flattenReverse(doc, 48);
			expectDocMatchesEngine(apDoc, doc, "flattenReverse: empty nodes is a no-op");
		}
	}

	{
		// Phase 1: trimToLength (pure node-trim kernel) — engine-oracle diff via the NoteRow model-stack harness.
		// trimToLength only touches the ModelStack through getValueAtPos(0) (→getLoopLength), so it's reachable
		// with the same light harness as deleteTime; action=nullptr keeps the undo-snapshot wiring out of the
		// way (same trick as moveRegionHorizontally's oracle) and drives the engine down its "basic trim" path —
		// the only path this kernel ports (undo-snapshot bookkeeping is engine wiring, not node mutation).
		using deluge::project::DocAutoParam;
		struct N {
			int32_t pos;
			int32_t value;
			bool interpolated;
		};
		const std::vector<std::vector<N>> configs = {
		    {},
		    {{48, 7, false}},
		    {{0, 10, true}, {32, 20, true}, {64, 30, false}, {96, -40, true}}, // node already at pos 0
		    {{16, 5, true}, {48, 15, false}, {80, -25, true}},                 // no node at pos 0 -> needNewNodeAt0
		};
		const int32_t loopLen = 128;
		const int32_t trimLens[] = {200, 128, 100, 80, 64, 32, 16, 8, 0};

		auto buildStack = [](char* buf, int32_t effLen, NoteRow& nr) -> ModelStackWithAutoParam* {
			auto* ms = reinterpret_cast<ModelStackWithAutoParam*>(buf);
			ms->song = nullptr;
			ms->setTimelineCounter(nullptr);
			nr.setLoopLengthIfIndependent(effLen);
			ms->setNoteRow(&nr);
			return ms;
		};

		for (const auto& cfg : configs) {
			for (int32_t newLen : trimLens) {
				AutoParam ap;
				deluge::project::DocAutoParam apDoc;
				DocAutoParam doc{};
				for (const N& n : cfg) {
					setNodeIn(apDoc, n.pos, n.value, n.interpolated);
					deluge::project::setNodeAtPos(doc, n.pos, n.value, n.interpolated);
				}
				(void)(&apDoc)->nodes.reserveExtra(4);
				alignas(ModelStackWithAutoParam) char buf[MODEL_STACK_MAX_SIZE];
				NoteRow nr;
				ModelStackWithAutoParam* ms = buildStack(buf, loopLen, nr);
				bindOracle(ms, &ap, &apDoc);
				ap.trimToLength(static_cast<uint32_t>(newLen), /*action=*/nullptr, ms);
				deluge::project::trimToLength(doc, newLen, loopLen);
				expectDocMatchesEngine(apDoc, doc, "trimToLength: matches engine oracle");
			}
		}
	}

	{
		// Movement 2 (M2-B): appendParam — concatenate/tile src onto dest (straight, reversed, and pingponged),
		// incl. dest-side overflow trim and the pingpong seam-node synthesis. Engine-oracle diff; no ModelStack
		// (AutoParam::appendParam doesn't take or consult one).
		using deluge::project::DocAutoParam;
		struct N {
			int32_t pos;
			int32_t value;
			bool interpolated;
		};

		// Source configs: empty (no-op early return); node at pos 0 (nothingAtZero=false — seam skipped); nothing
		// at 0 but interpolated (pingpong seam-node synthesis fires); nothing at 0 but stepped (seam guard blocks
		// synthesis because nodeAfterWrap isn't interpolated); a single node (wrap-index edge cases when indexing
		// left/right neighbours mod numToInsert==1).
		const std::vector<std::vector<N>> srcConfigs = {
		    {},
		    {{0, 50, true}, {20, -30, true}, {40, 10, false}},
		    {{8, 5, true}, {32, -15, true}},
		    {{8, 5, false}, {32, -15, true}},
		    {{16, 99, true}},
		};
		// Dest configs: empty; one existing node; and a node beyond oldLength (exercises deleteNodesBeyondPos
		// trimming dest's own overflow before the append, as happens after arranger recording).
		const std::vector<std::vector<N>> destConfigs = {
		    {},
		    {{0, 1, true}},
		    {{0, 1, true}, {60, 2, true}}, // 60 > oldLength(48): must get trimmed away before the append
		};
		const int32_t oldLength = 48;
		for (const auto& sc : srcConfigs) {
			for (const auto& dc : destConfigs) {
				for (int32_t reverseLen : {0, 48}) {
					for (bool pingpong : {false, true}) {
						AutoParam ap;
						deluge::project::DocAutoParam apDoc;
						AutoParam otherAp;
						deluge::project::DocAutoParam otherApDoc;
						DocAutoParam doc{};
						DocAutoParam otherDoc{};
						for (const N& n : dc) {
							setNodeIn(apDoc, n.pos, n.value, n.interpolated);
							deluge::project::setNodeAtPos(doc, n.pos, n.value, n.interpolated);
						}
						for (const N& n : sc) {
							setNodeIn(otherApDoc, n.pos, n.value, n.interpolated);
							deluge::project::setNodeAtPos(otherDoc, n.pos, n.value, n.interpolated);
						}

						ap.appendParam(*(&apDoc), *(&otherApDoc), oldLength, reverseLen, pingpong);
						deluge::project::appendParam(doc, otherDoc, oldLength, reverseLen, pingpong);

						check(ap.currentValue == doc.current_value,
						      "appendParam: current_value untouched, matches engine oracle");
						expectDocMatchesEngine(apDoc, doc, "appendParam: matches engine oracle");
					}
				}
			}
		}
	}

	{
		// Phase 1: nudgeNonInterpolatingNodesAtPos (single-node nudge, pure kernel) — engine-oracle diff. `action`
		// is nullptr at every call, and the engine only touches `modelStack` inside `if (action)`, so `modelStack`
		// is never dereferenced either — no ModelStack/NoteRow harness needed here, unlike deleteTime /
		// moveRegionHorizontally.
		using deluge::project::DocAutoParam;
		struct N {
			int32_t pos;
			int32_t value;
			bool interpolated;
		};
		const int32_t lengthBeforeLoop = 128;

		const std::vector<std::vector<N>> configs = {
		    {{48, 100, false}}, // lone node, interior — no-wrap moves only
		    {{0, 77, false}},   // lone node at the loop edge — wrap cases below must hit the guard, not run
		    {{0, 10, false}, {127, 99, false}}, // 2 nodes at the edges — wrap collapses the array to empty
		    {{0, 10, false}, {1, 20, false}, {64, 30, true}, {126, 40, false}, {127, 50, false}}, // wrap->new-node,
		                                                                                          // wrap->merge,
		                                                                                          // no-wrap collide,
		                                                                                          // interpolated no-op
		    {{0, 10, false}, {32, 20, false}, {64, 30, false}, {96, -40, true}}, // spread + no-wrap moves +
		                                                                         // interpolated no-op
		};

		const struct {
			int32_t pos, offset;
		} nudgeCases[] = {
		    {48, 1}, {48, -1}, // plain no-wrap nudge, both directions
		    {0, -1}, {127, 1}, // wrap at the loop edges
		    {1, -1},           // no-wrap collision with the adjacent node at pos 0
		    {32, 1}, {96, -1}, // no-wrap, no colliding neighbour
		    {64, 1}, {64, -1}, // node at pos is interpolated -> must be a no-op
		    {50, 1}, {50, -1}, // no node at pos -> no-op
		};

		for (const auto& cfg : configs) {
			for (const auto& nc : nudgeCases) {
				// Skip any case that would hit the engine's single-node FREEZE_WITH_ERROR("E335") (a wrap when the
				// whole node array is down to just the node being nudged). ALPHA_OR_BETA_VERSION is on in this
				// build, so the engine side would abort the test process rather than return a diffable result;
				// the kernel instead safely no-ops (see risks), so there's nothing meaningful to diff anyway.
				if (cfg.size() == 1 && cfg[0].pos == nc.pos && !cfg[0].interpolated) {
					int32_t newPos = nc.pos + nc.offset;
					if (newPos < 0 || newPos >= lengthBeforeLoop) {
						continue;
					}
				}

				AutoParam ap;
				deluge::project::DocAutoParam apDoc;
				DocAutoParam doc{};
				for (const N& n : cfg) {
					setNodeIn(apDoc, n.pos, n.value, n.interpolated);
					deluge::project::setNodeAtPos(doc, n.pos, n.value, n.interpolated);
				}
				(void)(&apDoc)->nodes.reserveExtra(4);

				// M2-E: nudge now resolves nodes via modelStack->doc(); with action=nullptr it uses no other
				// modelStack field, so a minimal stack carrying only autoParam suffices.
				alignas(ModelStackWithAutoParam) char nbuf[MODEL_STACK_MAX_SIZE];
				auto* nms = reinterpret_cast<ModelStackWithAutoParam*>(nbuf);
				bindOracle(nms, &ap, &apDoc);
				ap.nudgeNonInterpolatingNodesAtPos(nc.pos, nc.offset, lengthBeforeLoop, nullptr, nms);
				deluge::project::nudgeNonInterpolatingNodesAtPos(doc, nc.pos, nc.offset, lengthBeforeLoop);
				expectDocMatchesEngine(apDoc, doc, "nudgeNonInterpolatingNodesAtPos: matches engine oracle");
			}
		}
	}

	{
		// Phase 1: setValueForRegion (region-write op) — engine-oracle diff. The real AutoParam::setValueForRegion
		// unconditionally ends in modelStack->paramCollection->notifyParamModifiedInSomeWay(...), which needs a real
		// ParamCollection the light NoteRow harness doesn't have (same reason the deleteNodesWithinRegion block above
		// uses a hand replica instead of calling the engine method directly). So this block transcribes the method's
		// control flow (auto_param.cpp's AutoParam::setValueForRegion, minus Action/notify/interpolation-cursor
		// wiring) into a local lambda, but that lambda still calls the REAL ap.homogenizeRegion for the region-collapse
		// step, so that half stays genuinely engine-exercised (on top of its own dedicated oracle block above).
		using deluge::project::DocAutoParam;

		struct N {
			int32_t pos;
			int32_t value;
			bool interpolated;
		};

		// Returns the value-branch the engine tail takes (nullopt on the no-op / OOM early returns), so the DOM
		// kernel's RegionWriteResult.currentValueEffect can be checked against it across the whole matrix.
		auto engineSetValueForRegion = [](AutoParam& ap, deluge::project::DocAutoParam& apDoc,
		                                  ModelStackWithAutoParam* ms, uint32_t pos, uint32_t length, int32_t value,
		                                  int32_t effLen, bool interpL, bool interpR, bool clockActive, int32_t livePos,
		                                  bool reversed) -> std::optional<deluge::project::RegionCurrentValueEffect> {
			using deluge::project::RegionCurrentValueEffect;
			bindOracle(ms, &ap, &apDoc);
			if (pos >= static_cast<uint32_t>(effLen)) {
				return std::nullopt;
			}
			if (length == static_cast<uint32_t>(effLen)) {
				(&apDoc)->nodes.erase((&apDoc)->nodes.begin(), (&apDoc)->nodes.end());
				ap.currentValue = value;
				return RegionCurrentValueEffect::Set;
			}
			int32_t firstI = ap.homogenizeRegion(ms, static_cast<int32_t>(pos), static_cast<int32_t>(length), value,
			                                     interpL, interpR, effLen, /*reversed=*/false);
			if (firstI == -1) {
				return std::nullopt;
			}
			if (!clockActive) {
				ap.currentValue = value;
				return RegionCurrentValueEffect::Set;
			}
			int32_t mostRecentI = (&apDoc)->nodes.firstAtOrAfter(livePos + static_cast<int32_t>(!reversed)) - 1;
			if (mostRecentI == -1) {
				mostRecentI = static_cast<int32_t>((&apDoc)->nodes.size()) - 1;
			}
			if (mostRecentI == firstI) {
				ap.currentValue = value;
				return RegionCurrentValueEffect::SetAndResetInterp;
			}
			return RegionCurrentValueEffect::NotifyOnly;
		};

		const std::vector<std::vector<N>> configs = {
		    {},
		    {{48, 7, false}},
		    {{0, 10, true}, {32, 20, true}, {64, 30, false}, {96, -40, true}},
		};

		struct Case {
			uint32_t pos, length;
			int32_t value;
			bool interpL, interpR;
		};
		// Every non-full-region case keeps pos+length strictly below effLen, so the real ap.homogenizeRegion call
		// inside engineSetValueForRegion never takes the loop-wrap branch (which needs a live Clip for
		// getCurrentlyRecordingLinearly and would dereference a null timeline counter under this light harness — the
		// same restriction the dedicated homogenizeRegion oracle block above documents). recordingLinearly is passed
		// as false to the DOM kernel below for the same reason; it is inert for every case actually exercised here.
		const Case cases[] = {
		    {0, 128, 999, true, true},  // full-region collapse
		    {16, 32, 500, true, true},  // normal, mid-clip
		    {0, 64, -300, false, true}, // normal, from 0
		    {96, 16, 42, true, false},  // normal, near the end (96+16=112 < 128, no wrap)
		    {150, 32, 7, true, true},   // pos beyond effectiveLength -> no-op
		};
		const int32_t effLen = 128;
		const bool clockActives[] = {false, true};
		const int32_t livePositions[] = {20, 100}; // 20 sits inside [16,48); 100 sits outside every non-full case
		const bool reversedFlags[] = {false, true};

		for (const auto& cfg : configs) {
			for (const auto& c : cases) {
				for (bool clockActive : clockActives) {
					for (int32_t livePos : livePositions) {
						for (bool reversed : reversedFlags) {
							AutoParam ap;
							deluge::project::DocAutoParam apDoc;
							DocAutoParam doc{};
							for (const N& n : cfg) {
								setNodeIn(apDoc, n.pos, n.value, n.interpolated);
								deluge::project::setNodeAtPos(doc, n.pos, n.value, n.interpolated);
							}
							// Same reallocation-safety reserve the homogenizeRegion oracle block above needs, for
							// the same reason (the engine's homogenizeRegion has a latent dangling-pointer bug on
							// mid-op reallocation; the DOM port is index-based and immune).
							(void)(&apDoc)->nodes.reserveExtra(4);

							alignas(ModelStackWithAutoParam) char buf[MODEL_STACK_MAX_SIZE];
							auto* ms = reinterpret_cast<ModelStackWithAutoParam*>(buf);
							ms->song = nullptr;
							ms->setTimelineCounter(nullptr);
							NoteRow nr;
							nr.setLoopLengthIfIndependent(effLen);
							ms->setNoteRow(&nr);

							auto expectedEffect =
							    engineSetValueForRegion(ap, apDoc, ms, c.pos, c.length, c.value, effLen, c.interpL,
							                            c.interpR, clockActive, livePos, reversed);
							auto domRet = deluge::project::setValueForRegion(
							    doc, c.pos, c.length, c.value, effLen, c.interpL, c.interpR,
							    /*recordingLinearly=*/false, clockActive, livePos, reversed);

							check(domRet.has_value() == (c.pos < static_cast<uint32_t>(effLen)),
							      "setValueForRegion: nullopt iff pos is at/past effectiveLength");
							check(domRet.has_value() == expectedEffect.has_value(),
							      "setValueForRegion: has_value matches engine oracle");
							check(!domRet.has_value() || domRet->currentValueEffect == *expectedEffect,
							      "setValueForRegion: currentValueEffect matches engine branch");
							check(ap.currentValue == doc.current_value,
							      "setValueForRegion: current_value matches engine oracle");
							expectDocMatchesEngine(apDoc, doc, "setValueForRegion: nodes match engine oracle");
						}
					}
				}
			}
		}

		// Spot checks on the automationChanged signal itself (the wiring seam's notify(..., automationChanged, ...)
		// argument), which the matrix above only checks indirectly via has_value().
		{
			DocAutoParam doc{};
			auto r = deluge::project::setValueForRegion(doc, 0, 128, 999, 128, true, true, false, true, 0, false);
			check(r.has_value() && !r->automationChanged
			          && r->currentValueEffect == deluge::project::RegionCurrentValueEffect::Set,
			      "setValueForRegion: full-region collapse reports automationChanged=false, effect Set");
			check(doc.current_value == 999 && doc.nodes.empty(),
			      "setValueForRegion: full-region collapse clears nodes and sets current_value");
		}
		{
			DocAutoParam doc{};
			auto r = deluge::project::setValueForRegion(doc, 200, 32, 5, 128, true, true, false, true, 0, false);
			check(!r.has_value(), "setValueForRegion: pos at/past effectiveLength is a no-op (nullopt)");
			check(doc.current_value == 0 && doc.nodes.empty(), "setValueForRegion: no-op leaves the param untouched");
		}
		{
			DocAutoParam doc{};
			auto r = deluge::project::setValueForRegion(doc, 16, 32, 500, 128, true, true, false, false, 0, false);
			check(r.has_value() && r->automationChanged
			          && r->currentValueEffect == deluge::project::RegionCurrentValueEffect::Set,
			      "setValueForRegion: normal homogenize-region (stopped) reports automationChanged=true, effect Set");
		}
	}

	// Phase 4: copyRegion + pasteRegion (automation copy/paste) — engine-oracle diff. AutoParam::copy only
	// touches the ModelStack via getValueAtPos (→getLoopLength), so it's callable with the light NoteRow
	// harness (like deleteTime/moveRegionHorizontally above); AutoParam::paste additionally ends in
	// notifyParamModifiedInSomeWay, which walks up through paramCollection/paramManager, so its destination
	// AutoParam has to be a real slot inside a real ParamManagerForTimeline (paste's automationChanged is
	// always true, but playbackHandler is idle in this harness, so the notify is a safe no-op).
	//
	// NOTE for the integrator: this block requires an added top-of-file include in
	// tests/projection/param_automation_test.cpp:
	//   #include "modulation/automation/copied_param_automation.h"
	// (needed to construct a real ::CopiedParamAutomation on the stack; not transitively included today).
	{
		using deluge::project::CopiedAutomation;
		using deluge::project::DocAutoParam;
		struct N {
			int32_t pos, value;
			bool interpolated;
		};
		const std::vector<std::vector<N>> srcConfigs = {
		    {},
		    {{48, 7, false}},
		    {{0, 10, true}, {32, 20, true}, {64, 30, false}, {96, -40, true}},
		    {{0, -1073741824, true}, {40, 1073741823, false}, {90, 12345, true}},
		};
		const std::vector<std::vector<N>> dstConfigs = {
		    {},
		    {{20, -5, true}, {70, 15, false}},
		};
		const int32_t loopLen = 128;
		constexpr int32_t kDstParamId = 2;

		auto lightStack = [](char* buf, int32_t effLen, NoteRow& nr) -> ModelStackWithAutoParam* {
			auto* ms = reinterpret_cast<ModelStackWithAutoParam*>(buf);
			ms->song = nullptr;
			ms->setTimelineCounter(nullptr);
			nr.setLoopLengthIfIndependent(effLen);
			ms->setNoteRow(&nr);
			return ms;
		};

		// Unlike lightStack, this walks the real chain (addTimelineCounter/addNoteRow/addOtherTwoThings/
		// getUnpatchedAutoParamFromId) so paramCollection/paramManager/autoParam all end up consistently set,
		// which paste's trailing notifyParamModifiedInSomeWay needs.
		auto fullStack = [](char* buf, int32_t effLen, NoteRow& nr,
		                    ParamManagerForTimeline& pm) -> ModelStackWithAutoParam* {
			auto* ms = reinterpret_cast<ModelStack*>(buf);
			ms->song = nullptr;
			auto* withTC = ms->addTimelineCounter(nullptr);
			auto* withNR = withTC->addNoteRow(0, &nr);
			nr.setLoopLengthIfIndependent(effLen);
			auto* with3 = withNR->addOtherTwoThings(nullptr, &pm);
			return with3->getUnpatchedAutoParamFromId(kDstParamId);
		};

		const struct {
			int32_t startPos, endPos;
		} copyCases[] = {{0, 32}, {16, 48}, {32, 96}, {64, 128}, {0, 128}};

		const struct {
			int32_t startPos, endPos;
			float scaleFactor;
		} pasteCases[] = {
		    {0, 32, 1.0f},  {16, 48, 1.0f},   {32, 96, 1.0f},
		    {0, 128, 1.0f}, {100, 170, 1.0f}, // wraps past loopLen (wrappedEndPos = 42)
		    {20, 60, 2.0f}, {10, 90, 0.5f},
		};

		for (const auto& srcCfg : srcConfigs) {
			for (bool isPatchCable : {false, true}) {
				for (const auto& cc : copyCases) {
					// Build matching engine + DOM source params.
					AutoParam ap;
					deluge::project::DocAutoParam apDoc;
					DocAutoParam doc{};
					for (const N& n : srcCfg) {
						setNodeIn(apDoc, n.pos, n.value, n.interpolated);
						deluge::project::setNodeAtPos(doc, n.pos, n.value, n.interpolated);
					}
					(void)(&apDoc)->nodes.reserveExtra(4);

					::CopiedParamAutomation ca; // engine automation clipboard carrier
					CopiedAutomation domCopied;

					alignas(ModelStackWithAutoParam) char cbuf[MODEL_STACK_MAX_SIZE];
					NoteRow cnr;
					ModelStackWithAutoParam* cms = lightStack(cbuf, loopLen, cnr);
					bindOracle(cms, &ap, &apDoc);
					ap.copy(cc.startPos, cc.endPos, &ca, isPatchCable, cms);
					deluge::project::copyRegion(doc, cc.startPos, cc.endPos, loopLen, isPatchCable, domCopied);

					check(ca.width == domCopied.width, "copyRegion: width matches engine oracle");
					check(ca.numNodes == static_cast<int32_t>(domCopied.nodes.size()),
					      "copyRegion: node count matches engine oracle");
					for (int32_t i = 0; i < ca.numNodes && i < static_cast<int32_t>(domCopied.nodes.size()); i++) {
						check(ca.nodes[i].pos == domCopied.nodes[i].pos, "copyRegion: node pos matches engine oracle");
						check(ca.nodes[i].value == domCopied.nodes[i].value,
						      "copyRegion: node value matches engine oracle");
						check(ca.nodes[i].interpolated == domCopied.nodes[i].interpolated,
						      "copyRegion: node interpolated matches engine oracle");
					}

					if (ca.numNodes == 0) {
						if (ca.nodes) {
							delugeDealloc(ca.nodes);
						}
						continue; // nothing copied — paste requires a non-empty clipboard (an ALPHA_OR_BETA_VERSION
						          // FREEZE_WITH_ERROR precondition in the engine).
					}

					for (const auto& dstCfg : dstConfigs) {
						for (const auto& pc : pasteCases) {
							ParamManagerForTimeline pm;
							check(pm.setupWithPatching() == Error::NONE, "copy_paste: pm setup");
							AutoParam& ap2 = pm.getUnpatchedParamSet()->params[kDstParamId];
							// M2-F: ap2 lives in a ParamSet, so its nodes are owner-resolved (the paste op below runs
							// on a real ModelStack). Author + inspect via the set's docForParam, not ap2's ownDoc_.
							deluge::project::DocAutoParam* ap2Doc = pm.getUnpatchedParamSet()->docForParam(kDstParamId);
							DocAutoParam doc2{};
							for (const N& n : dstCfg) {
								setNodeIn(*ap2Doc, n.pos, n.value, n.interpolated);
								deluge::project::setNodeAtPos(doc2, n.pos, n.value, n.interpolated);
							}
							(void)ap2Doc->nodes.reserveExtra(4);

							alignas(ModelStackWithAutoParam) char pbuf[MODEL_STACK_MAX_SIZE];
							NoteRow pnr;
							ModelStackWithAutoParam* msap = fullStack(pbuf, loopLen, pnr, pm);
							ap2.paste(pc.startPos, pc.endPos, pc.scaleFactor, msap, &ca, isPatchCable);
							deluge::project::pasteRegion(doc2, pc.startPos, pc.endPos, pc.scaleFactor, loopLen,
							                             isPatchCable, domCopied);
							expectSameDoc(*pm.getUnpatchedParamSet()->docForParam(kDstParamId), doc2,
							              "pasteRegion: matches engine oracle");
						}
					}

					if (ca.nodes) {
						delugeDealloc(ca.nodes);
					}
				}
			}
		}
	}

	// Phase 2: stealNodes + insertStolenNodes (region steal-out/splice-back with loop wrap) — engine-oracle
	// diff. stealNodes is invoked directly on the real AutoParam (action=nullptr keeps its undo-snapshot away,
	// same as the deleteTime/moveRegionHorizontally block above). insertStolenNodes's real engine method ends in
	// an UNCONDITIONAL notifyParamModifiedInSomeWay touching modelStack->paramCollection/paramManager, which the
	// light NoteRow-based ModelStackWithAutoParam harness doesn't set up (would crash) — so, exactly like the
	// deleteNodesWithinRegion block further below, we diff against a same-file replica that calls the SAME node
	// primitives (ap.stealNodes / nodes().insertSorted) the real method does, minus that notify tail.
	{
		using deluge::project::DocAutoParam;
		using deluge::project::StolenNodes;
		struct N {
			int32_t pos;
			int32_t value;
			bool interpolated;
		};
		const std::vector<std::vector<N>> configs = {
		    {},
		    {{48, 7, false}},
		    {{0, 10, true}, {32, 20, true}, {64, 30, false}, {96, -40, true}},
		    {{5, 1, false}, {40, 2, false}, {90, 3, false}, {110, 4, false}}, // spans a loop-wrap region
		};
		const int32_t loopLen = 128;

		auto buildStack = [](char* buf, int32_t effLen, NoteRow& nr) -> ModelStackWithAutoParam* {
			auto* ms = reinterpret_cast<ModelStackWithAutoParam*>(buf);
			ms->song = nullptr;
			ms->setTimelineCounter(nullptr);
			nr.setLoopLengthIfIndependent(effLen);
			ms->setNoteRow(&nr);
			return ms;
		};

		// Replica of AutoParam::insertStolenNodes, minus its unconditional notifyParamModifiedInSomeWay tail.
		auto engineInsertStolen = [](AutoParam& ap, deluge::project::DocAutoParam& apDoc, ModelStackWithAutoParam* ms,
		                             int32_t pos, int32_t regionLength, int32_t loopLength,
		                             StolenParamNodes* stolenNodeRecord) {
			ap.stealNodes(ms, pos, regionLength, loopLength, nullptr); // clear the destination area, unrecorded
			for (int32_t sourceI = 0; sourceI < stolenNodeRecord->num; sourceI++) {
				ParamNode* stolenNode = &stolenNodeRecord->nodes[sourceI];
				if (stolenNode->pos >= regionLength) {
					break;
				}
				int32_t destPos = stolenNode->pos + pos;
				if (destPos >= loopLength) {
					destPos -= loopLength;
				}
				int32_t destI = (&apDoc)->nodes.insertSorted(destPos).value_or(-1);
				if (destI == -1) {
					break;
				}
				ParamNode* destNode = &(&apDoc)->nodes[destI];
				*destNode = *stolenNode;
				destNode->pos = destPos;
			}
		};

		const struct {
			int32_t srcPos, srcLen, dstPos, dstLen;
		} cases[] = {
		    {32, 16, 64, 16},  // same length, no wrap either side
		    {32, 16, 64, 8},   // destination shorter — trailing stolen nodes dropped
		    {100, 40, 20, 40}, // source region wraps the loop
		    {20, 20, 100, 40}, // destination region wraps the loop
		    {0, 128, 0, 128},  // whole-loop steal + reinsert
		    {90, 50, 90, 50},  // steal and reinsert at the same spot
		};

		for (const auto& cfg : configs) {
			for (const auto& c : cases) {
				AutoParam ap;
				deluge::project::DocAutoParam apDoc;
				DocAutoParam doc{};
				for (const N& n : cfg) {
					setNodeIn(apDoc, n.pos, n.value, n.interpolated);
					deluge::project::setNodeAtPos(doc, n.pos, n.value, n.interpolated);
				}
				(void)(&apDoc)->nodes.reserveExtra(4);
				alignas(ModelStackWithAutoParam) char buf[MODEL_STACK_MAX_SIZE];
				NoteRow nr;
				ModelStackWithAutoParam* ms = buildStack(buf, loopLen, nr);
				bindOracle(ms, &ap, &apDoc);

				// --- stealNodes: engine call direct vs. the DOM kernel; diff both the stolen record and the
				// residual timeline.
				StolenParamNodes engineRecord{};
				ap.stealNodes(ms, c.srcPos, c.srcLen, loopLen, nullptr, &engineRecord);
				StolenNodes domRecord;
				deluge::project::stealNodes(doc, c.srcPos, c.srcLen, loopLen, &domRecord);

				check(engineRecord.num == static_cast<int32_t>(domRecord.size()),
				      "stealNodes: stolen count matches engine oracle");
				int32_t numToCompare = engineRecord.num < static_cast<int32_t>(domRecord.size())
				                           ? engineRecord.num
				                           : static_cast<int32_t>(domRecord.size());
				for (int32_t i = 0; i < numToCompare; i++) {
					check(engineRecord.nodes[i].pos == domRecord[i].pos,
					      "stealNodes: stolen node pos matches engine oracle");
					check(engineRecord.nodes[i].value == domRecord[i].value,
					      "stealNodes: stolen node value matches engine oracle");
					check(engineRecord.nodes[i].interpolated == domRecord[i].interpolated,
					      "stealNodes: stolen node interpolated matches engine oracle");
				}
				expectDocMatchesEngine(apDoc, doc, "stealNodes: residual timeline matches engine oracle");

				// --- insertStolenNodes: splice the stolen record back in at a (possibly different) destination.
				engineInsertStolen(ap, apDoc, ms, c.dstPos, c.dstLen, loopLen, &engineRecord);
				deluge::project::insertStolenNodes(doc, c.dstPos, c.dstLen, loopLen, domRecord);
				check(ap.currentValue == doc.current_value,
				      "insertStolenNodes: current_value untouched, matches engine oracle");
				expectDocMatchesEngine(apDoc, doc, "insertStolenNodes: matches engine oracle");
			}
		}
	}

	// Movement 2 (M2-A): shiftParamVolumeByDB — dB-domain shift of current_value + node values; oracle diff.
	// (float math, but the engine op and kernel run identical code, so they agree on each arch.)
	{
		struct N {
			int32_t pos;
			int32_t value;
			bool interpolated;
		};
		const std::vector<std::vector<N>> cfgs = {
		    {},
		    {{0, 1000000000, true}, {48, 100000000, false}},
		    {{0, -500000000, true}, {48, 2000000000, false}},
		};
		const int32_t cvs[] = {0, 500000000, -1000000000, 2000000000};
		const float offs[] = {0.0f, 1.0f, 3.5f, 6.0f, 100.0f};
		for (const auto& cfg : cfgs) {
			for (int32_t cv : cvs) {
				for (float off : offs) {
					AutoParam ap;
					deluge::project::DocAutoParam apDoc;
					ap.currentValue = cv;
					deluge::project::DocAutoParam doc{};
					doc.current_value = cv;
					for (const N& n : cfg) {
						setNodeIn(apDoc, n.pos, n.value, n.interpolated);
						deluge::project::setNodeAtPos(doc, n.pos, n.value, n.interpolated);
					}
					ap.shiftParamVolumeByDB(*(&apDoc), off);
					deluge::project::shiftParamVolumeByDB(doc, off);
					check(ap.currentValue == doc.current_value, "shiftParamVolumeByDB: current_value matches engine");
					expectDocMatchesEngine(apDoc, doc, "shiftParamVolumeByDB: nodes match engine");
				}
			}
		}
	}

	// Movement 2 (M2-A): makeInterpolationGoodAgain — inserts gradient nodes; oracle diff over CC-style configs.
	// Non-pathological inputs (ascending positions < clipLength, no wrap collision) so both op + kernel do real
	// insertion work and agree bit-for-bit. quantizationRShift 22 (pitch bend) + 25 (other MIDI), as the callers use.
	{
		struct N {
			int32_t pos;
			int32_t value;
			bool interpolated;
		};
		struct Cfg {
			int32_t clipLength;
			std::vector<N> nodes;
		};
		const int32_t kNeg = -2147483647; // small ~ -64
		const int32_t kPos = 2147483647;  // small ~ +64
		const std::vector<Cfg> cfgs = {
		    {192, {}},                                                   // empty
		    {192, {{0, 100000000, true}}},                               // single node — early return
		    {192, {{0, kNeg, true}, {96, kPos, true}}},                  // steep full-range sweep (inserts many)
		    {192, {{0, 0, true}, {48, 500000000, true}}},                // gentle non-steep ramp
		    {192, {{0, 0, false}, {48, 500000000, true}}},               // leading stepped node then interp
		    {384, {{0, kNeg, true}, {120, 0, true}, {300, kPos, true}}}, // three-node up-ramp
		    {192,
		     {{0, 0, true}, {96, 400000000, true}, {190, 0, true}}}, // node[0] interp: exercises the loop wrap-read
		};
		const int32_t rshifts[] = {22, 25};
		for (const auto& cfg : cfgs) {
			for (int32_t rs : rshifts) {
				AutoParam ap;
				deluge::project::DocAutoParam apDoc;
				deluge::project::DocAutoParam doc{};
				for (const N& n : cfg.nodes) {
					setNodeIn(apDoc, n.pos, n.value, n.interpolated);
					deluge::project::setNodeAtPos(doc, n.pos, n.value, n.interpolated);
				}
				ap.makeInterpolationGoodAgain(*(&apDoc), cfg.clipLength, rs);
				bool ok = deluge::project::makeInterpolationGoodAgain(doc, cfg.clipLength, rs);
				check(ok, "makeInterpolationGoodAgain: kernel completes (no RAM failure)");
				expectDocMatchesEngine(apDoc, doc, "makeInterpolationGoodAgain: nodes match engine");
			}
		}
	}

	// Movement 2 (M2-A): recordLiveValue — the recording branch's node mutation (setNodeAtPos OR homogenizeRegion
	// + reversed fixup, then current_value=value). No standalone engine method exists (it's inline in
	// setCurrentValueInResponseToUserInput), so — as with setValueForRegion above — a local lambda transcribes the
	// engine control flow (auto_param.cpp lines ~245-280 + the getOut currentValue=value) while calling the REAL
	// ap.setNodeAtPos / ap.homogenizeRegion, keeping that half genuinely engine-exercised.
	{
		using deluge::project::DocAutoParam;
		struct N {
			int32_t pos;
			int32_t value;
			bool interpolated;
		};

		auto engineRecordLiveValue = [](AutoParam& ap, deluge::project::DocAutoParam& apDoc,
		                                ModelStackWithAutoParam* ms, int32_t livePos, int32_t value,
		                                int32_t ticksToClear, bool doMPE, bool interpL, int32_t effLen, bool reversed,
		                                int32_t cut) -> int32_t {
			bindOracle(ms, &ap, &apDoc);
			int32_t leftI;
			if (doMPE || ticksToClear == 0) {
				leftI = ap.setNodeAtPos(*(&apDoc), livePos, value, interpL);
			}
			else {
				leftI = ap.homogenizeRegion(ms, livePos, ticksToClear, value, interpL, /*interpR=*/true, effLen,
				                            reversed, cut);
				if (reversed && leftI != -1) {
					int32_t iFurtherRight = leftI + 2;
					if (iFurtherRight >= static_cast<int32_t>((&apDoc)->nodes.size())) {
						iFurtherRight -= static_cast<int32_t>((&apDoc)->nodes.size());
					}
					if (iFurtherRight >= static_cast<int32_t>((&apDoc)->nodes.size())) {
						iFurtherRight -= static_cast<int32_t>((&apDoc)->nodes.size());
					}
					(&apDoc)->nodes[iFurtherRight].interpolated = true;
				}
			}
			ap.currentValue = value;
			return leftI;
		};

		const std::vector<std::vector<N>> configs = {
		    {},
		    {{48, 7, false}},
		    {{0, 10, true}, {32, 20, true}, {64, 30, false}, {96, -40, true}},
		};
		struct Case {
			int32_t livePos, ticksToClear, value;
			bool doMPE, interpL, reversed;
			int32_t cut;
		};
		constexpr int32_t MAXCUT = 2147483647;
		const Case cases[] = {
		    {40, 0, 500, false, true, false, MAXCUT},   // ticksToClear==0 -> setNodeAtPos, forward
		    {40, 32, 500, true, true, false, MAXCUT},   // doMPE -> setNodeAtPos even with a region
		    {40, 0, -300, false, false, false, MAXCUT}, // setNodeAtPos, stepped
		    {16, 32, 500, false, true, false, MAXCUT},  // homogenize forward non-wrap
		    {0, 64, -300, false, false, false, MAXCUT}, // homogenize forward from 0
		    {60, 40, -7, false, false, false, MAXCUT},  // homogenize forward, 60+40=100<128
		    {96, 32, 42, false, true, true, 0},         // homogenize reversed + fixup
		    {100, 50, -5, false, false, true, 0},       // homogenize reversed + fixup
		    {96, 48, 33, false, true, true, 64},        // homogenize reversed clip-cut + fixup
		};
		const int32_t effLen = 128;
		for (const auto& cfg : configs) {
			for (const auto& c : cases) {
				AutoParam ap;
				deluge::project::DocAutoParam apDoc;
				DocAutoParam doc{};
				for (const N& n : cfg) {
					setNodeIn(apDoc, n.pos, n.value, n.interpolated);
					deluge::project::setNodeAtPos(doc, n.pos, n.value, n.interpolated);
				}
				// Reserve headroom: the engine's homogenizeRegion has a latent dangling-pointer bug on mid-op
				// reallocation (the DOM port is index-based and immune) — same guard the blocks above use.
				(void)(&apDoc)->nodes.reserveExtra(4);

				alignas(ModelStackWithAutoParam) char buf[MODEL_STACK_MAX_SIZE];
				auto* ms = reinterpret_cast<ModelStackWithAutoParam*>(buf);
				ms->song = nullptr;
				ms->setTimelineCounter(nullptr);
				NoteRow nr;
				nr.setLoopLengthIfIndependent(effLen);
				ms->setNoteRow(&nr);

				int32_t engRet = engineRecordLiveValue(ap, apDoc, ms, c.livePos, c.value, c.ticksToClear, c.doMPE,
				                                       c.interpL, effLen, c.reversed, c.cut);
				int32_t domRet = deluge::project::recordLiveValue(doc, c.livePos, c.value, c.ticksToClear, c.doMPE,
				                                                  c.interpL, effLen, c.reversed,
				                                                  /*recordingLinearly=*/false, c.cut);
				check(engRet == domRet, "recordLiveValue: return index matches engine oracle");
				check(ap.currentValue == doc.current_value, "recordLiveValue: current_value matches engine");
				expectDocMatchesEngine(apDoc, doc, "recordLiveValue: nodes match engine oracle");
			}
		}
	}

	// S4.5b (retire-on-death): BackedParams::removeOwner must drop EXACTLY one owner's entries (every clip
	// override + any shared default), contiguously, leaving other owners untouched — including an owner with the
	// SAME serial number but a different kind (instrument(7) vs drum(7)), which must NOT collide because OwnerId
	// orders by kind before serial.
	{
		deluge::project::BackedParams bp;
		const deluge::project::OwnerId inst = deluge::project::OwnerId::instrument(7);
		const deluge::project::OwnerId drum = deluge::project::OwnerId::drum(7);
		const deluge::project::OwnerId other = deluge::project::OwnerId::instrument(9);
		bp.getOrCreate(inst, 1);
		bp.getOrCreate(inst, 2);
		bp.getOrCreate(inst, std::nullopt);
		bp.getOrCreate(drum, 1);
		bp.getOrCreate(other, 1);
		check(bp.by_owner.size() == 5, "removeOwner: seeded 5 entries");
		bp.removeOwner(inst);
		check(bp.find(inst, 1) == nullptr && bp.find(inst, 2) == nullptr && bp.find(inst, std::nullopt) == nullptr,
		      "removeOwner: all of the owner's entries erased");
		check(bp.find(drum, 1) != nullptr, "removeOwner: same-serial different-kind owner untouched");
		check(bp.find(other, 1) != nullptr, "removeOwner: other owner untouched");
		check(bp.by_owner.size() == 2, "removeOwner: exactly the owner's entries erased");
	}

	if (failures == 0) {
		std::fprintf(stdout, "param_automation projection: OK\n");
		std::fflush(stdout);
		// _exit (not return) to skip deluge_app's global static destructors, which assume a fully
		// set-up app (e.g. a non-null display) — the same reason the golden harness uses _exit.
		_exit(0);
	}
	std::fprintf(stderr, "param_automation projection: %d failure(s)\n", failures);
	std::fflush(stderr);
	_exit(1);
}
