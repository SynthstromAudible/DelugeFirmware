# readFromTree slice 4 — clip view-state via `readShellFromTree` — Design

**Status:** design, approved for planning
**Branch:** `deluge-project-param`
**Date:** 2026-07-09
**Parent:** readFromTree slice 3 (`docs/superpowers/specs/2026-07-09-readfromtree-slice3-empty-base-design.md`), which emptied `Clip::readTagFromFile` and added the reusable `ValueTreeDeserializer::readChildObject` primitive. The base clip is now fully tree-native; this slice starts peeling the **subclass** `readFromFile` bodies, beginning with the pure view-state/UI attributes.

## Goal

Introduce `virtual Clip::readShellFromTree(const Value& clipNode)` — the scaffold that lets each clip subclass read its own fields tree-natively from the clip `Value` node — and use it to move the pure view-state/UI attributes out of `InstrumentClip::readFromFile` and `AudioClip::readFromFile`. Golden bit-identity is the gate.

## Why a virtual (the mechanism)

Base-clip shell reads live inline in `readClipsFromFile` (slices 1-3), but they are construction-coupled (`id` is read before the ctor) and song-coupled (`beingEdited`/`selected`/`isSyncScaleClip` call `song->…`). Subclass fields are neither — they are pure field writes on the constructed clip. A `virtual readShellFromTree(clipNode)`, called right after construction, is the natural home: each subclass reads its own view-state, `readFromFile` keeps shrinking, and later slices extend the same overrides. It takes only `clipNode` (no `song`) — this slice's fields need nothing else; a later slice can widen the signature if a song-coupled field moves in.

## Scope

**In:** the `readShellFromTree` virtual (base + `InstrumentClip` override; `AudioClip` needs none), the pure view-state/UI attributes, and the `readClipsFromFile` call site. **Out:** instrument-reference/preset binding (`instrumentPreset*`, `midiChannel`, `sound`/`kit`/`instrument` — side-effecting), AudioClip data attributes (`filePath`, sample positions, `transpose` — sample/ordering coupling), and all nested content (`soundParams`/`kitParams`/`midiParams`/`noteRows`/expression — later slices, reusing `readChildObject`). The existing inline base-shell reads in `readClipsFromFile` stay put.

## Design

### 1. The virtual

`src/deluge/model/clip/clip.h` — declare (forward-declare `deluge::storage::Value` to keep the header light):
```cpp
virtual void readShellFromTree(const deluge::storage::Value& clipNode);
```

`src/deluge/model/clip/clip.cpp` — base implementation reads the six fields shared by both subclasses today (all `int`-valued attributes; enum ones cast exactly as the deleted handlers did):
```cpp
void Clip::readShellFromTree(const deluge::storage::Value& clipNode) {
	using deluge::storage::Value;
	if (const Value* n = clipNode.find("onAutomationInstrumentClipView")) {
		onAutomationClipView = stringToInt(n->str().c_str());
	}
	if (const Value* n = clipNode.find("lastSelectedParamID")) {
		lastSelectedParamID = stringToInt(n->str().c_str());
	}
	if (const Value* n = clipNode.find("lastSelectedParamKind")) {
		lastSelectedParamKind = static_cast<deluge::modulation::params::Kind>(stringToInt(n->str().c_str()));
	}
	if (const Value* n = clipNode.find("lastSelectedParamShortcutX")) {
		lastSelectedParamShortcutX = stringToInt(n->str().c_str());
	}
	if (const Value* n = clipNode.find("lastSelectedParamShortcutY")) {
		lastSelectedParamShortcutY = stringToInt(n->str().c_str());
	}
	if (const Value* n = clipNode.find("lastSelectedParamArrayPosition")) {
		lastSelectedParamArrayPosition = stringToInt(n->str().c_str());
	}
}
```

### 2. InstrumentClip override

`src/deluge/model/clip/instrument_clip.{h,cpp}` — override reads the base fields then the InstrumentClip view attrs, preserving every cast and the one side effect (`crossScreenEditLevel` sets `wrapEditing = true`):
```cpp
void InstrumentClip::readShellFromTree(const deluge::storage::Value& clipNode) {
	Clip::readShellFromTree(clipNode);
	using deluge::storage::Value;
	if (const Value* n = clipNode.find("clipName")) {
		name = n->str();
	}
	if (const Value* n = clipNode.find("inKeyMode")) {
		inScaleMode = stringToInt(n->str().c_str());
	}
	if (const Value* n = clipNode.find("yScroll")) {
		yScroll = stringToInt(n->str().c_str());
	}
	if (const Value* n = clipNode.find("keyboardLayout")) {
		keyboardState.currentLayout = static_cast<KeyboardLayoutType>(stringToInt(n->str().c_str()));
	}
	if (const Value* n = clipNode.find("yScrollKeyboard")) {
		keyboardState.isomorphic.scrollOffset = stringToInt(n->str().c_str());
	}
	if (const Value* n = clipNode.find("keyboardRowInterval")) {
		keyboardState.isomorphic.rowInterval = stringToInt(n->str().c_str());
	}
	if (const Value* n = clipNode.find("drumsScrollOffset")) {
		keyboardState.drums.scroll_offset = stringToInt(n->str().c_str());
	}
	if (const Value* n = clipNode.find("drumsZoomLevel")) {
		keyboardState.drums.zoom_level = stringToInt(n->str().c_str());
	}
	if (const Value* n = clipNode.find("inKeyScrollOffset")) {
		keyboardState.inKey.scrollOffset = stringToInt(n->str().c_str());
	}
	if (const Value* n = clipNode.find("inKeyRowInterval")) {
		keyboardState.inKey.rowInterval = stringToInt(n->str().c_str());
	}
	if (const Value* n = clipNode.find("crossScreenEditLevel")) {
		wrapEditLevel = stringToInt(n->str().c_str());
		wrapEditing = true;
	}
	if (const Value* n = clipNode.find("onKeyboardScreen")) {
		onKeyboardScreen = stringToInt(n->str().c_str());
	}
	if (const Value* n = clipNode.find("lastSelectedInstrumentType")) {
		lastSelectedOutputType = static_cast<OutputType>(stringToInt(n->str().c_str()));
	}
	if (const Value* n = clipNode.find("lastSelectedPatchSource")) {
		lastSelectedPatchSource = static_cast<PatchSource>(stringToInt(n->str().c_str()));
	}
	if (const Value* n = clipNode.find("affectEntire")) {
		affectEntire = stringToInt(n->str().c_str());
	}
}
```

### 3. Call site + handler removal

`src/deluge/model/song/song.cpp` `readClipsFromFile` — after construction and the existing inline base-shell reads, before `readFromFile`:
```cpp
newClip->readShellFromTree(*clipNode);
```
Then remove the moved handlers: the six shared ones from **both** `InstrumentClip::readFromFile` and `AudioClip::readFromFile`, and the ~15 InstrumentClip view handlers from `InstrumentClip::readFromFile`. Those tags reappear in the streaming pass and fall through to `exitTag()` — skipped, exactly as the slice-1-3 attributes already are.

## Equivalence argument

Every moved read is a pure field write with no dependency on anything else in `readFromFile` (they set UI/view members the instrument/note/param reads never consult). Running them right after construction instead of mid-parse is therefore equivalent, and the tag order in the file is irrelevant to a `find()`-keyed read. `crossScreenEditLevel`'s `wrapEditing = true` fires iff the tag is present, identical to the handler. `song_roundtrip` bit-identity is the backstop.

## Testing — the gate

`song_roundtrip` `OK=16 REGRESS=0`, `edit_undo_redo` `OK=40`, `param_record` `OK=3`, `json_roundtrip` `OK=38`, `./dbt test` 15/15, firmware links. Corpus: `export DELUGE_SONG_CORPUS="/home/kate/Deluge Backup/SONGS"`. Coverage: these attributes are ubiquitous across the corpus (yScroll/clipName/inKeyMode/lastSelected* on essentially every instrument clip), so the corpus exercises the whole group.

## Risks

1. **A moved field IS consulted during `readFromFile`** — the equivalence argument says none are; the bit-identity golden catches any that is. Lowest-risk class (pure view-state).
2. **`name` type** — `clipName` reads into `name`, a `std::string` (the streaming reader takes `std::string&`), so `name = n->str()` is a direct copy.
3. **Enum casts** — `KeyboardLayoutType`/`params::Kind`/`OutputType`/`PatchSource` casts are copied verbatim from the handlers; a mismatch would shift the serialized enum value and REGRESS.
