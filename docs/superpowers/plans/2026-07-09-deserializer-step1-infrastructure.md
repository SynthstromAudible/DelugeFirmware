# Record-First Deserializer — Step 1 (Infrastructure) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax.

**Goal:** Build the four format-agnostic pieces the record-first deserializer needs — `Value` (the tree), `parseJson`, `buildValue` (XML→tree over the existing tokenizer), and `ValueTreeDeserializer` (re-streams the existing `Deserializer` interface from a tree) — each specced in isolation with CppSpec. No load path is rewired yet (Step 2). A prerequisite Task 0 extracts the abstract `Deserializer` interface into a lean header so the adapter is testable without dragging in FatFS.

**Architecture:** A `Value` is `orderedMap | array | rawStringScalar`. `parseJson` builds one from JSON bytes; `buildValue` builds one by driving any `Deserializer` (used with `XMLDeserializer`). `ValueTreeDeserializer` is a `Deserializer` cursor over a `Value`, so existing `readFromFile` code can later run tree-backed. Pure additive; nothing consumes these yet.

**Tech Stack:** C++26 (CppSpec/`deluge_spec` build is `CXX_STANDARD 26`); CppSpec specs in `tests/spec/*_spec.cpp` (`describe`/`context`/`it`/`expect(...).to_equal(...)`), auto-globbed into the `all_specs` driver, run via CTest (`./dbt test`). The existing `Deserializer`/`XMLDeserializer` are in `src/deluge/storage/storage_manager.h`.

## Global Constraints

- Tests are **CppSpec** (`#include <cppspec.hpp>`, `describe name("...", ${ it("...", _{ expect(x).to_equal(y); }); });`), files named `*_spec.cpp` in `tests/spec/`. NOT CppUTest.
- New spec-tested source `.cpp` files must be **added to the `deluge_spec` STATIC library source list** in `tests/spec/CMakeLists.txt` (that list is explicit, not globbed). Spec files themselves are auto-globbed by `create_specs_driver`.
- Reads must eventually serve **both XML and JSON**; `Value`/`ValueTreeDeserializer` are format-agnostic, `buildValue` is the throwaway XML path, `parseJson` the permanent JSON path. (Spec: *Hard constraint*.)
- Scalars are **raw strings**, parsed on demand; the map is **insertion-ordered** (vector of pairs), never `std::map`. (Spec: *Architecture / Value*.)
- Idiomatic C++23/26 (`enum class`, `std::variant`, `std::optional`, `std::string_view`, no C-casts).
- Commit each task with a single fresh `git commit` — never chain `--amend` (pre-commit clang-format reformats + fails the first commit; recover with a new commit).
- Additive only — do NOT modify any load path or `readFromFile`. (Task 0 is a behaviour-neutral header move.)

## File structure

- Create `src/deluge/storage/deserializer.h` — the extracted abstract `Deserializer` interface (Task 0).
- Modify `src/deluge/storage/storage_manager.h` — include the new header, drop the inline `Deserializer` definition (Task 0).
- Create `src/deluge/storage/value.{h,cpp}` — `deluge::storage::Value`.
- Create `src/deluge/storage/parse_json.{h,cpp}` — `parseJson`.
- Create `src/deluge/storage/value_tree_deserializer.{h,cpp}` — `ValueTreeDeserializer`.
- Create `src/deluge/storage/build_value.{h,cpp}` — `buildValue`.
- Create `tests/spec/value_spec.cpp`, `parse_json_spec.cpp`, `value_tree_deserializer_spec.cpp`, `build_value_spec.cpp`.
- Modify `tests/spec/CMakeLists.txt` — add the four new `src/deluge/storage/*.cpp` to the `deluge_spec` library.

---

### Task 0: Extract the abstract `Deserializer` interface into a lean header

**Files:** Create `src/deluge/storage/deserializer.h`; Modify `src/deluge/storage/storage_manager.h`.

**Why:** `ValueTreeDeserializer` must inherit `Deserializer`, but `Deserializer` currently sits in `storage_manager.h`, which pulls in `fatfs/fatfs.hpp`, `libdeluge/scheduler.h`, `FileReader`/`FIL`. The lean `deluge_spec` test library can't take that. The abstract read interface has no FatFS dependency of its own — extracting it is behaviour-neutral and correct.

**Interfaces — Produces:** `class Deserializer` (the exact 14 pure virtuals it has today) in `deserializer.h`, needing only `definitions_cxx.hpp` (for `Error`), `<cstdint>`, `<string>`. `storage_manager.h` `#include`s it; `XMLDeserializer`/`JsonDeserializer`/`FileDeserializer` inherit it unchanged.

- [ ] **Step 1: Create `deserializer.h`** — move the `class Deserializer { … };` block verbatim (the 14 pure virtuals) into `src/deluge/storage/deserializer.h`, under `#pragma once`, with `#include "definitions_cxx.hpp"`, `<cstdint>`, `<string>`. Keep the same global namespace (no namespace) so no callers change.
- [ ] **Step 2: Update `storage_manager.h`** — replace the inline `class Deserializer {…}` with `#include "storage/deserializer.h"`.
- [ ] **Step 3: Build firmware to prove the move is neutral** — `./dbt build Debug 2>&1 | tail -2` → links (same elf size class as before; the move adds/removes no code).
- [ ] **Step 4: Run the existing specs to prove nothing regressed** — `./dbt test 2>&1 | tail -5` → existing specs pass.
- [ ] **Step 5: Commit** — `git commit -q -m "storage: extract abstract Deserializer interface into deserializer.h (deserializer step 1)"`.

---

### Task 1: `Value` tree type

**Files:** Create `src/deluge/storage/value.{h,cpp}`; Modify `tests/spec/CMakeLists.txt` (add `value.cpp` to `deluge_spec`); Spec `tests/spec/value_spec.cpp`.

**Interfaces — Produces:**
```cpp
namespace deluge::storage {
class Value {
public:
	enum class Type : uint8_t { Scalar, Map, Array };
	static Value makeScalar(std::string s);
	static Value makeMap();
	static Value makeArray();
	[[nodiscard]] Type type() const;
	[[nodiscard]] bool isScalar() const;
	[[nodiscard]] bool isMap() const;
	[[nodiscard]] bool isArray() const;
	[[nodiscard]] const std::string& str() const;                                  // scalar
	void set(std::string key, Value value);                                        // map, ordered
	[[nodiscard]] const Value* find(std::string_view key) const;                   // map, nullptr if absent
	[[nodiscard]] const std::vector<std::pair<std::string, Value>>& entries() const;
	void append(Value value);                                                      // array
	[[nodiscard]] const std::vector<Value>& items() const;
private:
	std::variant<std::string, std::vector<std::pair<std::string, Value>>, std::vector<Value>> data_;
};
} // namespace deluge::storage
```
(`std::variant` over an incomplete `Value` inside `std::vector`/`std::pair` is well-formed in C++23/26. Accessors `std::get<>` the active alternative; `find` linear-scans `entries()`.)

- [ ] **Step 1: Write the failing spec** — `tests/spec/value_spec.cpp`:
```cpp
#include "storage/value.h"
#include <cppspec.hpp>

// clang-format off
describe value_spec("deluge::storage::Value", ${
	using deluge::storage::Value;
	it("scalar holds its string", _{
		Value v = Value::makeScalar("42");
		expect(v.isScalar()).to_be_true();
		expect(v.str()).to_equal(std::string("42"));
	});
	it("map preserves insertion order and finds by key", _{
		Value m = Value::makeMap();
		m.set("id", Value::makeScalar("7"));
		m.set("section", Value::makeScalar("2"));
		expect(m.isMap()).to_be_true();
		expect(m.entries().size()).to_equal(std::size_t{2});
		expect(m.entries()[0].first).to_equal(std::string("id"));       // order, not sorted
		expect(m.entries()[1].first).to_equal(std::string("section"));
		expect(m.find("id")->str()).to_equal(std::string("7"));
		expect(m.find("missing") == nullptr).to_be_true();
	});
	it("array appends in order", _{
		Value a = Value::makeArray();
		a.append(Value::makeScalar("a"));
		a.append(Value::makeScalar("b"));
		expect(a.isArray()).to_be_true();
		expect(a.items().size()).to_equal(std::size_t{2});
		expect(a.items()[1].str()).to_equal(std::string("b"));
	});
});
```
- [ ] **Step 2: Add `value.cpp` to `deluge_spec`** in `tests/spec/CMakeLists.txt` (append `../../src/deluge/storage/value.cpp` to the `add_library(deluge_spec STATIC …)` list).
- [ ] **Step 3: Run, verify it fails** — `./dbt test 2>&1 | tail -5` → FAIL (no `value.h` / unresolved).
- [ ] **Step 4: Implement `value.{h,cpp}`** to the interface above.
- [ ] **Step 5: Run, verify pass** — `./dbt test 2>&1 | tail -5` → `value_spec` passes, others green.
- [ ] **Step 6: Commit** — `git commit -q -m "storage: Value tree type (deserializer step 1)"`.

---

### Task 2: `parseJson`

**Files:** Create `src/deluge/storage/parse_json.{h,cpp}`; Modify `tests/spec/CMakeLists.txt` (add `parse_json.cpp`); Spec `tests/spec/parse_json_spec.cpp`.

**Interfaces:**
- Consumes: `Value` (Task 1).
- Produces: `std::optional<deluge::storage::Value> deluge::storage::parseJson(std::string_view json);` — `std::nullopt` on malformed input. Objects → ordered `Map` (document order); arrays → `Array`; strings/numbers/bools/null → `Scalar` holding the **raw token text** (strings unescaped, without surrounding quotes) so it round-trips through the same `readTagOrAttributeValue*` parsing the file path uses.

- [ ] **Step 1: Write the failing spec** — `tests/spec/parse_json_spec.cpp`:
```cpp
#include "storage/parse_json.h"
#include "storage/value.h"
#include <cppspec.hpp>

// clang-format off
describe parse_json_spec("deluge::storage::parseJson", ${
	using deluge::storage::parseJson;
	it("parses an object with ordered scalar members", _{
		auto v = parseJson(R"({"id":7,"section":2})");
		expect(v.has_value()).to_be_true();
		expect(v->isMap()).to_be_true();
		expect(v->entries()[0].first).to_equal(std::string("id"));      // insertion order
		expect(v->find("id")->str()).to_equal(std::string("7"));        // raw token text
	});
	it("parses nested arrays and objects", _{
		auto v = parseJson(R"({"clips":[{"id":1},{"id":2}]})");
		expect(v.has_value()).to_be_true();
		const auto* clips = v->find("clips");
		expect(clips->isArray()).to_be_true();
		expect(clips->items().size()).to_equal(std::size_t{2});
		expect(clips->items()[1].find("id")->str()).to_equal(std::string("2"));
	});
	it("stores strings unquoted", _{
		auto v = parseJson(R"({"name":"kick"})");
		expect(v->find("name")->str()).to_equal(std::string("kick"));
	});
	it("returns nullopt on malformed input", _{
		expect(parseJson(R"({"id":)").has_value()).to_be_false();
	});
});
```
- [ ] **Step 2: Add `parse_json.cpp` to `deluge_spec`.**
- [ ] **Step 3: Run, verify fail** — `./dbt test 2>&1 | tail -5` → FAIL.
- [ ] **Step 4: Implement** recursive-descent: skip whitespace; dispatch on `{`/`[`/`"`/number-or-keyword; strings unescape into scalar text; return `std::nullopt` on any structural error (unterminated, missing `:`/`,`, trailing garbage). Self-contained, no streaming-interface dependency.
- [ ] **Step 5: Run, verify pass.**
- [ ] **Step 6: Commit** — `git commit -q -m "storage: parseJson -> Value (deserializer step 1)"`.

---

### Task 3: `ValueTreeDeserializer` (the adapter)

**Files:** Create `src/deluge/storage/value_tree_deserializer.{h,cpp}`; Modify `tests/spec/CMakeLists.txt` (add `value_tree_deserializer.cpp`); Spec `tests/spec/value_tree_deserializer_spec.cpp`.

**Interfaces:**
- Consumes: `Value` (Task 1); `Deserializer` (`storage/deserializer.h`, Task 0).
- Produces: `class ValueTreeDeserializer final : public Deserializer`, constructed from `const Value&`. Implements all 14 pure virtuals over an explicit stack of `{const Value* node; std::size_t index}` frames:
  - `readNextTagOrAttributeName()` — advance to the next map entry (or array element) at the current level; return the key (`""` = no more, matching the file reader's end signal); remember the entry's value as *pending*.
  - `match('{')`/`match('[')` — if *pending* is a Map/Array, push a frame descending into it and return `true`; else return `false` (no consume).
  - `readTagOrAttributeValue()` — `pending->str().c_str()`.
  - `readTagOrAttributeValueInt()` — base-10 parse of *pending*; `readTagOrAttributeValueHex(err)` — parse `0x…`/hex, `err` on failure; `readTagOrAttributeValueHexBytes(bytes,max)` — decode hex-pairs into `bytes`, return count.
  - `prepareToReadTagOrAttributeValueOneCharAtATime()`/`readNextCharOfTagOrAttributeValue()`/`getNumCharsRemainingInValueBeforeEndOfCluster()`/`readNextCharsOfTagOrAttributeValue(n)` — serve chars from *pending*'s string.
  - `readTagOrAttributeValueString(std::string&)` — copy *pending* out, `Error::NONE`.
  - `exitTag(name, closeObject)` — pop a frame.
  - `tryReadingFirmwareTagFromFile(name, ignore)` — `Error::NONE` (firmware-version handling is a Step-2 concern).
  - `reset()` — clear the stack to the root's start.

- [ ] **Step 1: Write the failing spec** — `tests/spec/value_tree_deserializer_spec.cpp` (hand-build a tree, assert the emitted sequence matches tree order):
```cpp
#include "storage/value.h"
#include "storage/value_tree_deserializer.h"
#include <cppspec.hpp>

// clang-format off
describe value_tree_deserializer_spec("ValueTreeDeserializer", ${
	using deluge::storage::Value;
	using deluge::storage::ValueTreeDeserializer;
	it("replays scalar members in order", _{
		Value clip = Value::makeMap();
		clip.set("id", Value::makeScalar("7"));
		clip.set("section", Value::makeScalar("2"));
		ValueTreeDeserializer d{clip};
		d.match('{');
		expect(std::string(d.readNextTagOrAttributeName())).to_equal(std::string("id"));
		expect(d.readTagOrAttributeValueInt()).to_equal(7);
		expect(std::string(d.readNextTagOrAttributeName())).to_equal(std::string("section"));
		expect(d.readTagOrAttributeValueInt()).to_equal(2);
		expect(std::string(d.readNextTagOrAttributeName())).to_equal(std::string(""));
	});
	it("descends into a nested object and pops back", _{
		Value root = Value::makeMap();
		Value inner = Value::makeMap();
		inner.set("y", Value::makeScalar("5"));
		root.set("noteRow", std::move(inner));
		ValueTreeDeserializer d{root};
		d.match('{');
		expect(std::string(d.readNextTagOrAttributeName())).to_equal(std::string("noteRow"));
		expect(d.match('{')).to_be_true();
		expect(std::string(d.readNextTagOrAttributeName())).to_equal(std::string("y"));
		expect(d.readTagOrAttributeValueInt()).to_equal(5);
		d.exitTag();
		expect(std::string(d.readNextTagOrAttributeName())).to_equal(std::string(""));
	});
	it("match('{') returns false (no consume) on a scalar", _{
		Value m = Value::makeMap();
		m.set("id", Value::makeScalar("7"));
		ValueTreeDeserializer d{m};
		d.match('{');
		d.readNextTagOrAttributeName();
		expect(d.match('{')).to_be_false();
		expect(d.readTagOrAttributeValueInt()).to_equal(7);
	});
});
```
- [ ] **Step 2: Add `value_tree_deserializer.cpp` to `deluge_spec`.** (Depends on Task 0's lean `deserializer.h`.)
- [ ] **Step 3: Run, verify fail** — `./dbt test 2>&1 | tail -5` → FAIL.
- [ ] **Step 4: Implement** the cursor + frame stack. Non-spec-exercised methods (hex bytes, char-at-a-time) implemented correctly per the interface notes.
- [ ] **Step 5: Run, verify pass.**
- [ ] **Step 6: Commit** — `git commit -q -m "storage: ValueTreeDeserializer adapter (deserializer step 1)"`.

---

### Task 4: `buildValue` (generic tree-build over a `Deserializer`)

**Files:** Create `src/deluge/storage/build_value.{h,cpp}`; Modify `tests/spec/CMakeLists.txt` (add `build_value.cpp`); Spec `tests/spec/build_value_spec.cpp`.

**Interfaces:**
- Consumes: `Value` (Task 1); `Deserializer` (Task 0); `ValueTreeDeserializer` (Task 3, for the round-trip).
- Produces: `deluge::storage::Value deluge::storage::buildValue(Deserializer& reader);` — loop `readNextTagOrAttributeName()`; for each name, `if (reader.match('{')) child = buildValue(reader)` (object) `else if (reader.match('[')) child = <Array of buildValue>` `else child = Value::makeScalar(reader.readTagOrAttributeValue())`; append under the name (map, ordered); `exitTag` as the interface requires; end the level when the name is `""`.

- [ ] **Step 1: Write the failing spec** — `tests/spec/build_value_spec.cpp` (round-trip through the adapter, cross-validating both without a real file):
```cpp
#include "storage/build_value.h"
#include "storage/value.h"
#include "storage/value_tree_deserializer.h"
#include <cppspec.hpp>

// clang-format off
describe build_value_spec("deluge::storage::buildValue", ${
	using deluge::storage::Value;
	using deluge::storage::ValueTreeDeserializer;
	using deluge::storage::buildValue;
	it("round-trips a tree through the adapter", _{
		Value clip = Value::makeMap();
		clip.set("id", Value::makeScalar("7"));
		Value nr = Value::makeMap();
		nr.set("y", Value::makeScalar("5"));
		clip.set("noteRow", std::move(nr));

		ValueTreeDeserializer d{clip};
		d.match('{');                                  // position at the map, as the loader will
		Value rebuilt = buildValue(d);
		expect(rebuilt.find("id")->str()).to_equal(std::string("7"));
		expect(rebuilt.find("noteRow")->isMap()).to_be_true();
		expect(rebuilt.find("noteRow")->find("y")->str()).to_equal(std::string("5"));
		expect(rebuilt.entries()[0].first).to_equal(std::string("id"));       // order preserved
		expect(rebuilt.entries()[1].first).to_equal(std::string("noteRow"));
	});
});
```
- [ ] **Step 2: Add `build_value.cpp` to `deluge_spec`.**
- [ ] **Step 3: Run, verify fail.**
- [ ] **Step 4: Implement** `buildValue` per the interface. (Real-`XMLDeserializer` validation is deferred to Step 2's golden load→save — see spec *Testing*.)
- [ ] **Step 5: Run, verify pass; full `./dbt test` green.**
- [ ] **Step 6: Commit** — `git commit -q -m "storage: buildValue generic tree-build (deserializer step 1)"`.

---

## Self-Review

**Spec coverage.** Implements the spec's *Architecture* components 1–3 + the hybrid parsers, each CppSpec-specced in isolation (*Testing*, unit bullet). Task 0 (lean `Deserializer` header) is an enabler the CppSpec lean-lib requires; behaviour-neutral. Steps 2 (faithful-replay swap) and 3 (id-first clip) are their own plans; real-`XMLDeserializer` `buildValue` and JSON-fixture round-trips are Step-2 items, noted where deferred.

**Placeholder scan.** No TBD/TODO; each task has concrete spec code, an interface block, exact `./dbt test` commands. Hex/char-at-a-time adapter methods are specified by behaviour (serve from the scalar string) — acceptable, mirroring the documented `Deserializer` contract, not exercised until Step 2.

**Type consistency.** `Value` factories (`makeScalar`/`makeMap`/`makeArray`) + accessors (`str`/`find`/`entries`/`items`) + mutators (`set`/`append`) used identically in Tasks 1–4. `parseJson` → `std::optional<Value>`; `buildValue` → `Value`; `ValueTreeDeserializer(const Value&)`. Namespace `deluge::storage` throughout. Specs use `describe/it/expect(...).to_equal/.to_be_true/.to_be_false`, files `*_spec.cpp`, C++26.

**CMake note.** `create_specs_driver` globs `*_spec.cpp` automatically, so new spec files need no registration — but the four new `src/deluge/storage/*.cpp` DO need adding to the explicit `deluge_spec` source list (Tasks 1–4 Step 2). If a spec needs `putchar_` (some do — see `string_spec.cpp`), add the same `extern "C" void putchar_(char c){ putchar(c); }` shim.
