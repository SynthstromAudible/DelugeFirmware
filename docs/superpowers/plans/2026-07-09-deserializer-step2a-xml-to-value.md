# Record-First Deserializer — Step 2a (XML → `Value`) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax.

**Goal:** Produce a `Value` tree from a Deluge XML file (`parseXml`) and make `ValueTreeDeserializer` replay it to legacy `readFromFile` code identically to reading the XML directly (a new **XML mode**), proven token-for-token against the live `XMLDeserializer`.

**Architecture:** `parseXml` is a standalone recursive-descent parser → an all-ordered-maps `Value` (no arrays, no heuristics). `ValueTreeDeserializer` gains a source-format mode; **XML mode** mirrors `XMLDeserializer`'s contract (permissive `match`, implicit descent, depth-pop `exitTag`). Fidelity is proven by a **host-sim differential harness** comparing the adapter's token stream to the live `XMLDeserializer`'s over real fragments.

**Tech Stack:** C++23/26. `parseXml` + XML-mode unit checks are CppSpec (`tests/spec/*_spec.cpp`, added to `deluge_spec` in `tests/spec/CMakeLists.txt`, run via `./dbt test`). The differential harness is a **host-sim** binary (build-sim), because it needs the FatFS-heavy real `XMLDeserializer` which the lean `deluge_spec` cannot link — mirror the `edit_undo_redo` target in `sim/CMakeLists.txt`.

## Global Constraints

- CppSpec convention: `describe <var>("...", ${...}); CPPSPEC_SPEC(<var>);` with `<var>` ≠ the file stem; source `.cpp`s added to the explicit `deluge_spec` list in `tests/spec/CMakeLists.txt`.
- `Value` from Step 1 (`src/deluge/storage/value.h`, namespace `deluge::storage`): `makeScalar/makeMap`, `set`, `find/entries/str/isMap/isScalar`. **No `Array` in XML trees** — every element is a `Map`; attributes and text-only elements are `Scalar` entries; repeated child names are repeated entries.
- Scalars store **raw value text** (matching `parseJson`).
- Idiomatic C++23/26 (`std::variant`, `enum class`, `std::optional`, `std::string_view`, no C-casts).
- Single fresh `git commit` per task — never chain `--amend` (clang-format hook reformats+fails; recover with a new commit).
- Additive only — do NOT modify `readFromFile` or any load path (that is Step 2b).

## File structure

- Create `src/deluge/storage/parse_xml.{h,cpp}` — `parseXml`.
- Modify `src/deluge/storage/value_tree_deserializer.{h,cpp}` — add `SourceFormat` enum + XML-mode behavior.
- Create `tests/spec/parse_xml_spec.cpp` + `tests/spec/value_tree_deserializer_xml_spec.cpp`.
- Modify `tests/spec/CMakeLists.txt` — add `parse_xml.cpp` to `deluge_spec`.
- Create `tests/golden/xml_replay_diff.cpp` — the differential harness.
- Modify `sim/CMakeLists.txt` — add the `xml_replay_diff` host-sim target (mirror `edit_undo_redo` at line 519).

---

### Task 1: `parseXml → Value`

**Files:** Create `src/deluge/storage/parse_xml.{h,cpp}`; Modify `tests/spec/CMakeLists.txt`; Spec `tests/spec/parse_xml_spec.cpp`.

**Interfaces — Produces:** `std::optional<deluge::storage::Value> deluge::storage::parseXml(std::string_view xml);` — parses one XML document. An element → `Map`; each attribute `name="val"` → a `Scalar` entry `(name → val)`; a text-only element `<n>text</n>` → a `Scalar` entry `(n → text)`; a structured child element → a nested `Map` entry; repeated child names → repeated ordered entries. Self-closing `<n/>` → an empty `Map` entry. Skips the `<?xml …?>` prolog. Returns `std::nullopt` on malformed input. Values are raw text with XML entities unescaped (`&amp;`→`&`, `&lt;`→`<`, `&gt;`→`>`, `&quot;`→`"`, `&apos;`→`'`).

- [ ] **Step 1: Write the failing spec** — `tests/spec/parse_xml_spec.cpp`:
```cpp
#include "storage/parse_xml.h"
#include "storage/value.h"
#include <cppspec.hpp>

// clang-format off
describe parse_xml("deluge::storage::parseXml", ${
	using deluge::storage::parseXml;
	it("maps attributes to ordered scalar entries", _{
		auto v = parseXml(R"(<clip section="0" length="384"/>)");
		expect(v.has_value()).to_be_true();
		expect(v->isMap()).to_be_true();
		expect(v->find("clip")->isMap()).to_be_true();          // top level is the element
		const auto* clip = v->find("clip");
		expect(clip->entries()[0].first).to_equal(std::string("section"));  // attribute order
		expect(clip->find("length")->str()).to_equal(std::string("384"));
	});
	it("maps a text-only element to a scalar", _{
		auto v = parseXml(R"(<param><cc>0</cc><value>0x0000</value></param>)");
		const auto* p = v->find("param");
		expect(p->find("cc")->str()).to_equal(std::string("0"));
		expect(p->find("value")->str()).to_equal(std::string("0x0000")); // raw hex text
	});
	it("keeps repeated child names as repeated entries", _{
		auto v = parseXml(R"(<clips><clip a="1"/><clip a="2"/></clips>)");
		const auto* clips = v->find("clips");
		expect(clips->entries().size()).to_equal(std::size_t{2});
		expect(clips->entries()[0].first).to_equal(std::string("clip"));
		expect(clips->entries()[1].first).to_equal(std::string("clip"));
		expect(clips->entries()[1].second.find("a")->str()).to_equal(std::string("2"));
	});
	it("unescapes entities and returns nullopt on malformed input", _{
		expect(parseXml(R"(<n>a&amp;b</n>)")->find("n")->str()).to_equal(std::string("a&b"));
		expect(parseXml("<n>unterminated").has_value()).to_be_false();
	});
});
```
- [ ] **Step 2: Add `parse_xml.cpp` to `deluge_spec`** in `tests/spec/CMakeLists.txt` (append `../../src/deluge/storage/parse_xml.cpp`).
- [ ] **Step 3: Run, verify fail** — `./dbt test 2>&1 | tail -20` → FAIL (no `parse_xml.h`).
- [ ] **Step 4: Implement `parse_xml.{h,cpp}`** — recursive descent: skip prolog/whitespace; read `<name`; loop attributes (`name="value"` / `name='value'`) as scalar entries; on `/>` close the (empty-or-attribute-only) element; on `>` read content — either text (→ the element becomes a scalar of that text if there are no child elements and it has no attributes; if it has attributes AND text, keep attributes and add the text under a conventional key — but the Deluge format never mixes, so treat "has child elements" → Map, "text only, no attributes" → Scalar) then `</name>`, or nested child elements (recurse) until `</name>`. Return `std::nullopt` on any structural error. Unescape the five predefined entities in values.
- [ ] **Step 5: Run, verify pass** — `./dbt test 2>&1 | tail -20` → `parse_xml` passes, others green.
- [ ] **Step 6: Commit** — `git commit -q -m "storage: parseXml -> Value (deserializer step 2a)"`.

---

### Task 2: `SourceFormat` mode enum (additive plumbing)

**Files:** Modify `src/deluge/storage/value_tree_deserializer.{h,cpp}`.

**Interfaces — Produces:** `enum class deluge::storage::SourceFormat : uint8_t { Json, Xml };` and `ValueTreeDeserializer(const Value& root, SourceFormat format = SourceFormat::Json)`. A private `SourceFormat format_` field. **No behavior change yet** — `format_` is stored and unused; every method behaves exactly as today (JSON semantics). The existing one-arg construction sites keep compiling via the default.

- [ ] **Step 1: Add the enum + ctor param + field.** In `value_tree_deserializer.h`: define `SourceFormat` (namespace `deluge::storage`), change the ctor to `explicit ValueTreeDeserializer(const Value& root, SourceFormat format = SourceFormat::Json) : root_(&root), format_(format) { reset(); }`, add `SourceFormat format_;` to the private members.
- [ ] **Step 2: Run, verify no regression** — `./dbt test 2>&1 | tail -20` → all green (13/13); the Step-1 `value_tree_deserializer_spec` and `build_value_spec` still pass unchanged (default `Json` preserves behavior).
- [ ] **Step 3: Commit** — `git commit -q -m "storage: ValueTreeDeserializer SourceFormat mode enum (deserializer step 2a)"`.

---

### Task 3: XML mode + host-sim differential harness (the fidelity deliverable)

**Files:** Modify `src/deluge/storage/value_tree_deserializer.{h,cpp}` (XML-mode branches); Create `tests/golden/xml_replay_diff.cpp`; Modify `sim/CMakeLists.txt`; Spec `tests/spec/value_tree_deserializer_xml_spec.cpp` (basic XML-mode mechanics).

**Interfaces:**
- Consumes: `parseXml` (Task 1); `SourceFormat` (Task 2); `Value`; the real `XMLDeserializer` (`storage/storage_manager.h`, ctor `XMLDeserializer(uint8_t* inbuf, size_t buflen)`).
- Produces: XML-mode replay in `ValueTreeDeserializer`. When `format_ == Xml`: `match(*)` returns `true` (permissive, no-op); `readNextTagOrAttributeName()` returns the current frame's next entry key (staging its value) or `""` at frame end without popping, and **implicitly descends** into a pending `Map` when called again; `exitTag` pops to the caller depth; value reads serve the pending scalar. JSON mode (`format_ == Json`) is untouched.

**This task is developed against the differential harness** — the real `XMLDeserializer` is ground truth. Implement XML mode by making the harness fragments pass; the CppSpec below is a coarse mechanics check only.

- [ ] **Step 1: Add the differential harness `tests/golden/xml_replay_diff.cpp`** — for each fragment, run a fixed *script* on both readers and compare transcripts:
```cpp
#include "storage/parse_xml.h"
#include "storage/value_tree_deserializer.h"
#include "storage/storage_manager.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
using deluge::storage::parseXml;
using deluge::storage::ValueTreeDeserializer;
using deluge::storage::SourceFormat;

static int failures = 0;
using Transcript = std::vector<std::string>;

// A script issues the exact call sequence readFromFile would make for a fragment's shape,
// appending each observable return to `t`. Same script runs on both readers.
template <typename Script>
static void diff(const char* name, const char* xml, Script script) {
	Transcript a, b;
	{ XMLDeserializer x((uint8_t*)xml, (size_t)strlen(xml)); script(x, a); }
	auto tree = parseXml(xml);
	if (!tree) { printf("FAIL %s: parseXml nullopt\n", name); failures++; return; }
	{ ValueTreeDeserializer v(*tree, SourceFormat::Xml); script(v, b); }
	if (a != b) {
		failures++;
		printf("FAIL %s: transcripts differ (xml=%zu adapter=%zu)\n", name, a.size(), b.size());
		for (size_t i = 0; i < a.size() || i < b.size(); i++)
			printf("  [%zu] xml=%s | adapter=%s\n", i,
			       i < a.size() ? a[i].c_str() : "<end>", i < b.size() ? b[i].c_str() : "<end>");
	}
}

int main() {
	// A single attribute-only element: <clip section="0" length="384"/>
	diff("attr-only", R"(<clip section="0" length="384"/>)", [](Deserializer& r, Transcript& t) {
		t.push_back(r.readNextTagOrAttributeName());         // "clip"
		r.match('{');                                        // enter clip
		t.push_back(r.readNextTagOrAttributeName());         // "section"
		t.push_back(std::to_string(r.readTagOrAttributeValueInt())); // 0
		t.push_back(r.readNextTagOrAttributeName());         // "length"
		t.push_back(std::to_string(r.readTagOrAttributeValueInt())); // 384
		t.push_back(r.readNextTagOrAttributeName());         // "" (end)
		r.exitTag(nullptr, true);
	});
	// Nested element, text child, and a container of repeated children go here as further diff(...) cases,
	// each scripted to the shape readFromFile would read (see the spec's fragment list).
	printf(failures ? "XML_REPLAY_DIFF: %d FAIL\n" : "XML_REPLAY_DIFF: OK\n", failures);
	return failures ? 1 : 0;
}
```
- [ ] **Step 2: Add the `xml_replay_diff` host-sim target to `sim/CMakeLists.txt`** — copy the `edit_undo_redo` block (line 519): same `host_*` sources, `${REPO_ROOT}/tests/golden/xml_replay_diff.cpp`, same `target_include_directories` and `target_link_libraries(... deluge_app deluge_scheduler ${SIM_ALLOC_LINK})` (+ the `RT_LIB` guard).
- [ ] **Step 3: Build + run, verify it fails** — `cmake --build build-sim --target xml_replay_diff 2>&1 | tail -3 && build-sim/xml_replay_diff` → FAIL (XML mode not implemented → transcripts differ / the adapter runs JSON semantics).
- [ ] **Step 4: Implement XML mode** in `value_tree_deserializer.{h,cpp}` — branch each affected method on `format_`. Iterate against the harness until `XML_REPLAY_DIFF: OK`. Add the remaining fragment scripts (nested element; text-content element; container of repeated children; a deeply nested one; a `0x` hex value) as you go, each scripted to its shape. JSON mode paths must remain unchanged.
- [ ] **Step 5: Add a coarse CppSpec mechanics check** — `tests/spec/value_tree_deserializer_xml_spec.cpp` (register `CPPSPEC_SPEC(value_tree_deserializer_xml)`): build a small all-maps `Value` by hand, construct `ValueTreeDeserializer{tree, SourceFormat::Xml}`, and assert `readNextTagOrAttributeName` walks entries + returns `""` at end, `match('{')` returns `true`, and implicit descent works. (Ground-truth fidelity lives in the harness; this just guards the mechanics in the fast suite.)
- [ ] **Step 6: Run all gates** — `cmake --build build-sim --target xml_replay_diff && build-sim/xml_replay_diff` → `OK`; `./dbt test 2>&1 | tail -20` → all green (JSON mode + the new XML spec).
- [ ] **Step 7: Commit** — `git commit -q -m "storage: ValueTreeDeserializer XML mode + differential harness (deserializer step 2a)"`.

---

## Self-Review

**Spec coverage.** Task 1 = `parseXml` (spec Component 1). Task 2 = the `SourceFormat` mode enum (Component 2, plumbing). Task 3 = XML-mode replay (Component 2, behavior) + the differential harness (Component 3). `parseXml`'s full-surface handling (hex, self-closing, escaping, `preview` blob) is in Task 1's implement step + specs; the `preview`-blob and deeply-nested fragments are in Task 3's harness. `firmwareVersion`, the swap, JSON-fixture validation, and format detection are explicitly Step 2b (spec Scope/Out) — not here.

**Placeholder scan.** Task 1 and 2 are fully stepped with concrete code/commands. Task 3 is necessarily iterative (XML mode is *developed against* the harness) — that is stated, not a hidden TODO; the harness scaffold + one worked fragment/script are concrete, and the remaining fragments are enumerated (from the spec) for the implementer to script to shape.

**Type consistency.** `parseXml → std::optional<Value>`; `SourceFormat { Json, Xml }`; `ValueTreeDeserializer(const Value&, SourceFormat = Json)`; harness uses `XMLDeserializer(uint8_t*, size_t)` and `ValueTreeDeserializer(*tree, SourceFormat::Xml)`. `Value` API (`find/entries/str/isMap`) matches Step 1.

**Note for the executor.** Task 3 is the fidelity-critical, iterative task — dispatch it on a strong model and review it hard (the whole-branch review of Step 1 caught a Critical exactly here). The harness `OK` + `./dbt test` green are its gates; the real backstop is Step 2b's whole-song golden.
