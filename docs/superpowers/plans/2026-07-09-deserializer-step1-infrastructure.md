# Record-First Deserializer — Step 1 (Infrastructure) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax.

**Goal:** Build the four format-agnostic pieces the record-first deserializer needs — `Value` (the tree), `parseJson`, `buildValue` (XML→tree over the existing tokenizer), and `ValueTreeDeserializer` (re-streams the existing `Deserializer` interface from a tree) — each unit-tested in isolation. No load path is rewired yet (that is Step 2).

**Architecture:** A `Value` is `orderedMap | array | rawStringScalar`. `parseJson` builds one from JSON bytes; `buildValue` builds one by driving any `Deserializer` (used with `XMLDeserializer`). `ValueTreeDeserializer` is a `Deserializer` cursor over a `Value`, so existing `readFromFile` code can later run tree-backed. Pure additive; nothing consumes these yet.

**Tech Stack:** C++23; CppUTest unit tests in `tests/unit/` (run via `./dbt test`); the existing `Deserializer`/`XMLDeserializer` in `src/deluge/storage/storage_manager.h`.

## Global Constraints

- Reads must eventually serve **both XML and JSON**; `Value` and `ValueTreeDeserializer` are format-agnostic, `buildValue` is the XML path (throwaway at JSON-only cutover), `parseJson` is the permanent JSON path. (Spec: *Hard constraint*.)
- Scalars are **raw strings**, parsed on demand by the adapter — no types baked into the tree. (Spec: *Architecture / Value*.)
- The map is **insertion-ordered** (a vector of pairs), never `std::map` — the adapter must replay tags in file order. (Spec: *Architecture / Value*.)
- New C++ is idiomatic C++23 (`enum class`, `std::variant`, `std::optional`, `std::string_view`, no C-casts). (Repo convention.)
- Commit each task with a single fresh `git commit` — never chain `--amend` (pre-commit clang-format reformats + fails the first commit; recover with a new commit).
- Additive only this step — do NOT modify any load path or `readFromFile`.

## File structure

- Create `src/deluge/storage/value.h` / `value.cpp` — the `Value` tree type (`deluge::storage::Value`).
- Create `src/deluge/storage/parse_json.h` / `parse_json.cpp` — `parseJson`.
- Create `src/deluge/storage/build_value.h` / `build_value.cpp` — `buildValue(Deserializer&)`.
- Create `src/deluge/storage/value_tree_deserializer.h` / `value_tree_deserializer.cpp` — `ValueTreeDeserializer`.
- Create `tests/unit/value_tests.cpp`, `tests/unit/parse_json_tests.cpp`, `tests/unit/value_tree_deserializer_tests.cpp`, `tests/unit/build_value_tests.cpp`.
- Modify `tests/unit/CMakeLists.txt` only if the `GLOB_RECURSE` doesn't already pick up new `tests/unit/*.cpp` and `src/deluge/storage/*.cpp` (verify first; the existing glob likely covers both).

---

### Task 1: `Value` tree type

**Files:** Create `src/deluge/storage/value.h`, `value.cpp`; Test `tests/unit/value_tests.cpp`.

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

	// Scalar (asserts isScalar):
	[[nodiscard]] const std::string& str() const;

	// Map (ordered; assert isMap): set appends a (key,value) pair preserving order.
	void set(std::string key, Value value);
	[[nodiscard]] const Value* find(std::string_view key) const; // nullptr if absent
	[[nodiscard]] const std::vector<std::pair<std::string, Value>>& entries() const;

	// Array (assert isArray):
	void append(Value value);
	[[nodiscard]] const std::vector<Value>& items() const;

private:
	std::variant<std::string, std::vector<std::pair<std::string, Value>>, std::vector<Value>> data_;
};
} // namespace deluge::storage
```
(`std::variant` over an incomplete `Value` inside `std::vector`/`std::pair` is well-formed in C++23. `str()`/`entries()`/`items()`/`find()`/`set()`/`append()` assert the active alternative via `std::get`.)

- [ ] **Step 1: Failing test** — `tests/unit/value_tests.cpp`:
```cpp
#include "CppUTest/TestHarness.h"
#include "storage/value.h"
using deluge::storage::Value;

TEST_GROUP(ValueTest){};

TEST(ValueTest, scalarHoldsString) {
	Value v = Value::makeScalar("42");
	CHECK(v.isScalar());
	STRCMP_EQUAL("42", v.str().c_str());
}
TEST(ValueTest, mapPreservesInsertionOrder) {
	Value m = Value::makeMap();
	m.set("id", Value::makeScalar("7"));
	m.set("section", Value::makeScalar("2"));
	CHECK(m.isMap());
	CHECK_EQUAL(2, (int)m.entries().size());
	STRCMP_EQUAL("id", m.entries()[0].first.c_str());       // order, not sorted
	STRCMP_EQUAL("section", m.entries()[1].first.c_str());
	STRCMP_EQUAL("7", m.find("id")->str().c_str());
	POINTERS_EQUAL(nullptr, (void*)m.find("missing"));
}
TEST(ValueTest, arrayAppends) {
	Value a = Value::makeArray();
	a.append(Value::makeScalar("a"));
	a.append(Value::makeScalar("b"));
	CHECK(a.isArray());
	CHECK_EQUAL(2, (int)a.items().size());
	STRCMP_EQUAL("b", a.items()[1].str().c_str());
}
```

- [ ] **Step 2: Run, verify it fails to compile/link** — `./dbt test` → FAIL (`storage/value.h` not found / undefined symbols).
- [ ] **Step 3: Implement `value.h` + `value.cpp`** to the interface above. `find` linear-scans `entries()`. `set`/`append` `std::get<>` the vector alternative (default-construct the right alternative in `makeMap`/`makeArray`).
- [ ] **Step 4: Run, verify pass** — `./dbt test` → the `ValueTest` group passes.
- [ ] **Step 5: Commit** — `git add -A && git commit -q -m "storage: Value tree type (record-first deserializer step 1)"`.

---

### Task 2: `parseJson`

**Files:** Create `src/deluge/storage/parse_json.h`, `parse_json.cpp`; Test `tests/unit/parse_json_tests.cpp`.

**Interfaces:**
- Consumes: `deluge::storage::Value` (Task 1).
- Produces: `std::optional<Value> deluge::storage::parseJson(std::string_view json);` — `std::nullopt` on malformed input. Objects → ordered `Map` (keys in document order); arrays → `Array`; strings/numbers/bools/null → `Scalar` holding the **raw token text** (e.g. `"7"`, `"true"`, `"1.5"`, `"hello"` without quotes for strings) so it round-trips through the same `readTagOrAttributeValue*` parsing the file path uses.

- [ ] **Step 1: Failing test** — `tests/unit/parse_json_tests.cpp`:
```cpp
#include "CppUTest/TestHarness.h"
#include "storage/parse_json.h"
using deluge::storage::parseJson;
using deluge::storage::Value;

TEST_GROUP(ParseJsonTest){};

TEST(ParseJsonTest, objectOrderedWithScalars) {
	auto v = parseJson(R"({"id":7,"section":2})");
	CHECK(v.has_value());
	CHECK(v->isMap());
	STRCMP_EQUAL("id", v->entries()[0].first.c_str());   // insertion order
	STRCMP_EQUAL("7", v->find("id")->str().c_str());     // raw token text
}
TEST(ParseJsonTest, nestedArrayAndObject) {
	auto v = parseJson(R"({"clips":[{"id":1},{"id":2}]})");
	CHECK(v.has_value());
	const Value* clips = v->find("clips");
	CHECK(clips->isArray());
	CHECK_EQUAL(2, (int)clips->items().size());
	STRCMP_EQUAL("2", clips->items()[1].find("id")->str().c_str());
}
TEST(ParseJsonTest, stringUnquoted) {
	auto v = parseJson(R"({"name":"kick"})");
	STRCMP_EQUAL("kick", v->find("name")->str().c_str());  // scalar text, no quotes
}
TEST(ParseJsonTest, malformedReturnsNullopt) {
	CHECK_FALSE(parseJson(R"({"id":)").has_value());
}
```

- [ ] **Step 2: Run, verify fail** — `./dbt test` → FAIL (undefined `parseJson`).
- [ ] **Step 3: Implement** a recursive-descent parser: skip whitespace; dispatch on `{` (object), `[` (array), `"` (string → unescape into scalar text), else number/`true`/`false`/`null` (scalar raw text up to delimiter). Return `std::nullopt` on any structural error (unterminated, missing `:`/`,`, trailing garbage). Keep it self-contained (no dependency on the streaming interface).
- [ ] **Step 4: Run, verify pass** — `./dbt test` → `ParseJsonTest` passes.
- [ ] **Step 5: Commit** — `git commit -q -m "storage: parseJson -> Value (record-first deserializer step 1)"`.

---

### Task 3: `ValueTreeDeserializer` (the adapter)

**Files:** Create `src/deluge/storage/value_tree_deserializer.h`, `value_tree_deserializer.cpp`; Test `tests/unit/value_tree_deserializer_tests.cpp`.

**Interfaces:**
- Consumes: `Value` (Task 1); `Deserializer` (`src/deluge/storage/storage_manager.h`).
- Produces: `class ValueTreeDeserializer final : public Deserializer` constructed from `const Value&` (the node it walks). Implements all 14 pure virtuals by walking the node:
  - `readNextTagOrAttributeName()` — advance to the next map entry (or array element) at the current level; return the entry key (empty `""` = no more at this level, matching the file reader's end signal). Remember the current entry's value as "pending".
  - `match('{')` / `match('[')` — if the pending value is a Map/Array, descend into it and return `true`; else return `false` (no consume).
  - `readTagOrAttributeValue()` — return the pending scalar's `str().c_str()`.
  - `readTagOrAttributeValueInt()` — `std::stol` (base 10) of the pending scalar. `readTagOrAttributeValueHex(err)` — parse `0x…`/hex, `err` on failure. `readTagOrAttributeValueHexBytes(bytes,max)` — decode the hex-pair string into `bytes`, return count.
  - `prepareToReadTagOrAttributeValueOneCharAtATime()` / `readNextCharOfTagOrAttributeValue()` / `getNumCharsRemainingInValueBeforeEndOfCluster()` / `readNextCharsOfTagOrAttributeValue(n)` — serve characters from the pending scalar string.
  - `readTagOrAttributeValueString(std::string&)` — copy the pending scalar into the out-string, return `Error::NONE`.
  - `exitTag(name, closeObject)` — pop back up one level (to the parent map/array cursor).
  - `tryReadingFirmwareTagFromFile(name, ignore)` — return `Error::NONE` (firmware-version handling is a Step-2 concern; unit tests don't exercise it).
  - `reset()` — reset the cursor to the root node's start.
  Internally: a small explicit stack of `{const Value* node; size_t index}` frames, since the tree is walked, not streamed.

- [ ] **Step 1: Failing test** — `tests/unit/value_tree_deserializer_tests.cpp` (build a tree by hand, assert the emitted sequence matches tree order):
```cpp
#include "CppUTest/TestHarness.h"
#include "storage/value.h"
#include "storage/value_tree_deserializer.h"
using deluge::storage::Value;
using deluge::storage::ValueTreeDeserializer;

TEST_GROUP(ValueTreeDeserializerTest){};

TEST(ValueTreeDeserializerTest, replaysScalarsInOrder) {
	Value clip = Value::makeMap();
	clip.set("id", Value::makeScalar("7"));
	clip.set("section", Value::makeScalar("2"));
	ValueTreeDeserializer d{clip};
	d.match('{');                                   // enter the map
	STRCMP_EQUAL("id", d.readNextTagOrAttributeName());
	CHECK_EQUAL(7, d.readTagOrAttributeValueInt());
	STRCMP_EQUAL("section", d.readNextTagOrAttributeName());
	CHECK_EQUAL(2, d.readTagOrAttributeValueInt());
	STRCMP_EQUAL("", d.readNextTagOrAttributeName()); // end of level
}
TEST(ValueTreeDeserializerTest, descendsIntoNestedObject) {
	Value root = Value::makeMap();
	Value inner = Value::makeMap();
	inner.set("y", Value::makeScalar("5"));
	root.set("noteRow", std::move(inner));
	ValueTreeDeserializer d{root};
	d.match('{');
	STRCMP_EQUAL("noteRow", d.readNextTagOrAttributeName());
	CHECK(d.match('{'));                             // pending value is a map -> descend
	STRCMP_EQUAL("y", d.readNextTagOrAttributeName());
	CHECK_EQUAL(5, d.readTagOrAttributeValueInt());
	d.exitTag();                                     // back up
	STRCMP_EQUAL("", d.readNextTagOrAttributeName());
}
TEST(ValueTreeDeserializerTest, matchBraceFalseOnScalar) {
	Value m = Value::makeMap();
	m.set("id", Value::makeScalar("7"));
	ValueTreeDeserializer d{m};
	d.match('{');
	d.readNextTagOrAttributeName();                  // "id", pending scalar
	CHECK_FALSE(d.match('{'));                        // scalar -> no descend, no consume
	CHECK_EQUAL(7, d.readTagOrAttributeValueInt());
}
```

- [ ] **Step 2: Run, verify fail** — `./dbt test` → FAIL (undefined `ValueTreeDeserializer`).
- [ ] **Step 3: Implement** the cursor + stack. Keep unimplemented-in-tests methods correct but minimal (hex/char-at-a-time per the interface notes). If `Deserializer` has non-pure members or a required base ctor, satisfy them.
- [ ] **Step 4: Run, verify pass** — `./dbt test` → `ValueTreeDeserializerTest` passes.
- [ ] **Step 5: Commit** — `git commit -q -m "storage: ValueTreeDeserializer adapter (record-first deserializer step 1)"`.

---

### Task 4: `buildValue` (generic tree-build over a `Deserializer`)

**Files:** Create `src/deluge/storage/build_value.h`, `build_value.cpp`; Test `tests/unit/build_value_tests.cpp`.

**Interfaces:**
- Consumes: `Value` (Task 1); `Deserializer` (interface); `ValueTreeDeserializer` (Task 3, for the round-trip test).
- Produces: `Value deluge::storage::buildValue(Deserializer& reader);` — reads the current node from any `Deserializer`: loop `readNextTagOrAttributeName()`; for each name, `if (reader.match('{')) child = buildValue(reader)` (object) `else if (reader.match('[')) child = <array of buildValue>` `else child = Value::makeScalar(reader.readTagOrAttributeValue())`; append under the name (map) preserving order; `exitTag` as the interface requires. Ends the level when `readNextTagOrAttributeName()` returns empty.

- [ ] **Step 1: Failing test** — `tests/unit/build_value_tests.cpp` (round-trip through the adapter: `buildValue(ValueTreeDeserializer{v})` reproduces `v`, cross-validating both components without a real file):
```cpp
#include "CppUTest/TestHarness.h"
#include "storage/value.h"
#include "storage/value_tree_deserializer.h"
#include "storage/build_value.h"
using deluge::storage::Value;
using deluge::storage::ValueTreeDeserializer;
using deluge::storage::buildValue;

static Value sample() {
	Value clip = Value::makeMap();
	clip.set("id", Value::makeScalar("7"));
	Value nr = Value::makeMap();
	nr.set("y", Value::makeScalar("5"));
	clip.set("noteRow", std::move(nr));
	return clip;
}

TEST_GROUP(BuildValueTest){};

TEST(BuildValueTest, roundTripsThroughAdapter) {
	Value original = sample();
	ValueTreeDeserializer d{original};
	d.match('{');                       // position at the map, as the caller does before readFromFile
	Value rebuilt = buildValue(d);
	// structural equality
	STRCMP_EQUAL("7", rebuilt.find("id")->str().c_str());
	CHECK(rebuilt.find("noteRow")->isMap());
	STRCMP_EQUAL("5", rebuilt.find("noteRow")->find("y")->str().c_str());
	STRCMP_EQUAL("id", rebuilt.entries()[0].first.c_str());       // order preserved
	STRCMP_EQUAL("noteRow", rebuilt.entries()[1].first.c_str());
}
```

- [ ] **Step 2: Run, verify fail** — `./dbt test` → FAIL (undefined `buildValue`).
- [ ] **Step 3: Implement** `buildValue` per the interface. (Real-`XMLDeserializer` validation is intentionally deferred to Step 2, where golden load→save bit-identity exercises `buildValue` over the actual corpus — see spec *Testing*.)
- [ ] **Step 4: Run, verify pass** — `./dbt test` → `BuildValueTest` passes; full `./dbt test` stays green (no regressions).
- [ ] **Step 5: Commit** — `git commit -q -m "storage: buildValue generic tree-build (record-first deserializer step 1)"`.

---

## Self-Review

**Spec coverage.** Implements the spec's *Architecture* components 1–3 and the XML/JSON parsers (*hybrid*), each unit-tested in isolation per *Testing* (unit bullet). Deliberately excludes Step 2 (faithful-replay swap) and Step 3 (id-first clip) — their own plans. The `Value` order + raw-scalar constraints and the adapter's interface-fidelity are covered by Tasks 1 and 3. Real-XML `buildValue` and JSON-fixture round-trips are spec Step-2 items, noted where deferred.

**Placeholder scan.** No TBD/TODO; each task has concrete test code, an interface block, and exact commands. The hex/one-char-at-a-time adapter methods are specified by behaviour (serve from the scalar string) rather than shown line-by-line — acceptable, as they mirror the documented `Deserializer` contract and are not exercised until Step 2.

**Type consistency.** `Value` factories (`makeScalar`/`makeMap`/`makeArray`), accessors (`str`/`find`/`entries`/`items`), and mutators (`set`/`append`) are used identically across Tasks 1–4. `parseJson` returns `std::optional<Value>`; `buildValue` returns `Value`; `ValueTreeDeserializer` takes `const Value&`. Namespace `deluge::storage` throughout.

**Note on CMake:** verify `tests/unit/CMakeLists.txt` and the app build glob pick up new `src/deluge/storage/*.cpp` and `tests/unit/*.cpp`; if the `GLOB_RECURSE` is not `CONFIGURE_DEPENDS`, re-run CMake configure (`./dbt` handles this) after adding files.
