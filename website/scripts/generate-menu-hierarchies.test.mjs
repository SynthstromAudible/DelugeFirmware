import { strict as assert } from "node:assert"
import fs from "node:fs"
import { test } from "node:test"
import {
  MENU_TREES,
  UNDOCUMENTED_ROOTS,
  buildReference,
  clipSettingsRoot,
  convertTree,
  fillSlot,
  placeRoots,
} from "./generate-menu-hierarchies.mjs"

const leaf = (id, name, extra = {}) => ({
  id,
  name,
  title: name,
  type: "basic",
  ...extra,
})
const submenu = (id, name, children, extra = {}) => ({
  children,
  id,
  name,
  title: name,
  type: "submenu",
  ...extra,
})
const labels = (node) => node.children.map((c) => c.oled)
const noTags = new Map()

test("a slot item's star is its place among the siblings that share its name", () => {
  assert.equal(fillSlot("Envelope *", 2, 4), "Envelope 3")
  assert.equal(fillSlot("Channel Track**", 0, 16), "Channel Track01")
  assert.equal(fillSlot("CV output *", 0, 1), "CV output *")
})

test("slot siblings become one row per slot", () => {
  const root = submenu(1, "Sound", [
    leaf(2, "Envelope *"),
    leaf(3, "Envelope *"),
  ])
  assert.deepEqual(labels(convertTree(root, "s", noTags)), [
    "Envelope 1",
    "Envelope 2",
  ])
})

test("siblings of the same shape are one row", () => {
  const root = submenu(1, "Randomizer", [
    leaf(2, "Gate Spread"),
    leaf(3, "Gate Spread", { available_when: "is_midi_out" }),
    submenu(4, "Bend range", [leaf(5, "Normal")]),
    leaf(6, "Bend range"),
  ])
  assert.deepEqual(labels(convertTree(root, "s", noTags)), [
    "Gate Spread",
    "Bend range",
    "Bend range",
  ])
})

test("a page a select press opens is a row of the first item that opens it", () => {
  const depth = leaf(9, "Modulate depth")
  const opener = (id, name) =>
    leaf(id, name, {
      select_chain: [
        { ...depth, select_chain: [{ ...leaf(id, name), ref: true }] },
      ],
    })
  const root = submenu(1, "Sound", [opener(2, "Volume"), opener(3, "Pan")])
  const tree = convertTree(root, "s", noTags)
  assert.deepEqual(labels(tree.children[0]), ["Modulate depth"])
  assert.deepEqual(labels(tree.children[0].children[0]), [])
  assert.deepEqual(labels(tree.children[1]), [])
})

test("two pages of the same shape that one item opens are one row", () => {
  const root = submenu(1, "Preset", [
    leaf(2, "Preset", {
      select_chain: [leaf(3, "Octave Mode"), leaf(4, "Octave Mode")],
    }),
  ])
  assert.deepEqual(labels(convertTree(root, "s", noTags).children[0]), [
    "Octave Mode",
  ])
})

test("a page a list names is not repeated where a select press opens it", () => {
  const voices = leaf(3, "Max Voices")
  const root = submenu(1, "Voice", [
    leaf(2, "Polyphony", { select_chain: [voices] }),
    voices,
  ])
  const tree = convertTree(root, "s", noTags)
  assert.deepEqual(labels(tree), ["Polyphony", "Max Voices"])
  assert.deepEqual(labels(tree.children[0]), [])
})

test("keys are the label path, made unique within the section", () => {
  const root = submenu(1, "Sound", [
    submenu(2, "Bend range", [leaf(3, "Normal")]),
    leaf(4, "Bend range"),
  ])
  const tree = convertTree(root, "s", noTags)
  assert.deepEqual(
    [
      tree.children[0].key,
      tree.children[0].children[0].key,
      tree.children[1].key,
    ],
    ["bend-range", "bend-range-normal", "bend-range-2"],
  )
})

test("a section's own tag entry wins over the key's", () => {
  const root = submenu(1, "Audio clip", [leaf(2, "Actions"), leaf(3, "Tplts")])
  const tags = new Map([
    ["actions", "c1.3"],
    ["audioClipMenu/actions", "c1.2"],
  ])
  const tree = convertTree(root, "audioClipMenu", tags)
  assert.equal(tree.children[0].tag, "c1.2")
  assert.equal(tree.children[1].tag, "c1.0")
  assert.equal(convertTree(root, "kitFxMenu", tags).children[0].tag, "c1.3")
})

test("a node's values are option rows before its children and its pages", () => {
  const root = submenu(1, "Sound", [
    leaf(2, "Octave Mode", {
      options: ["Up", "Down"],
      select_chain: [leaf(3, "Note Mode")],
    }),
  ])
  const mode = convertTree(root, "s", noTags).children[0]
  assert.deepEqual(labels(mode), ["Up", "Down", "Note Mode"])
  assert.deepEqual(
    mode.children.map((r) => [r.key, r.option ?? false]),
    [
      ["octave-mode-up", true],
      ["octave-mode-down", true],
      ["octave-mode-note-mode", false],
    ],
  )
})

test("a section hides the rows its availability names", () => {
  const root = submenu(1, "CV instrument", [
    leaf(2, "Device Definition", { available_when: "is_midi_out" }),
    leaf(3, "Play direction"),
  ])
  assert.deepEqual(labels(convertTree(root, "cv", noTags, ["is_midi_out"])), [
    "Play direction",
  ])
})

const everyRoot = () => {
  const counts = new Map()
  const roots = MENU_TREES.map(({ name }) => {
    counts.set(name, (counts.get(name) ?? 0) + 1)
    return submenu(counts.get(name), name, [])
  })
  return [...roots, ...UNDOCUMENTED_ROOTS.map((name) => leaf(99, name))]
}

test("every root of the dump is placed or named as undocumented", () => {
  assert.equal(placeRoots(everyRoot()).length, MENU_TREES.length)
  assert.throws(
    () => placeRoots([...everyRoot(), submenu(100, "Looper", [])]),
    /does not place: "Looper"/,
  )
  assert.throws(
    () => placeRoots(everyRoot().filter((r) => r.name !== "Settings")),
    /no root "Settings"/,
  )
})

test("the clip settings menu reads its labels from the English strings", () => {
  const english = {
    STRING_FOR_CONVERT_TO_AUDIO: "Convert to Audio",
    STRING_FOR_CLIP_MODE: "Clip Mode",
    STRING_FOR_DEFAULT_LAUNCH: "Infinite",
    STRING_FOR_FILL_LAUNCH: "Fill",
    STRING_FOR_ONCE_LAUNCH: "Once",
    STRING_FOR_CLIP_NAME: "Clip Name",
  }
  const tree = convertTree(clipSettingsRoot(english), "c", noTags)
  assert.deepEqual(labels(tree), ["Convert to Audio", "Clip Mode", "Clip Name"])
  assert.deepEqual(labels(tree.children[1]), ["Infinite", "Fill", "Once"])
  assert.throws(
    () => clipSettingsRoot({}),
    /has no STRING_FOR_CONVERT_TO_AUDIO/,
  )
})

test("the reference is the sections, the clip settings menu, and its inputs", () => {
  const english = {
    STRING_FOR_CONVERT_TO_AUDIO: "a",
    STRING_FOR_CLIP_MODE: "b",
    STRING_FOR_DEFAULT_LAUNCH: "c",
    STRING_FOR_FILL_LAUNCH: "d",
    STRING_FOR_ONCE_LAUNCH: "e",
    STRING_FOR_CLIP_NAME: "f",
  }
  const reference = JSON.parse(buildReference(everyRoot(), english, {}))
  assert.deepEqual(Object.keys(reference.trees), [
    ...MENU_TREES.map((t) => t.treeKey),
    "songClipSettingsMenu",
  ])
  assert.deepEqual(Object.keys(reference.sourceFiles), [
    "menuTree",
    "english",
    "menuTags",
  ])
})

const committedDump = JSON.parse(
  fs.readFileSync(
    new URL("../src/data/generated/menu-tree.json", import.meta.url),
    "utf8",
  ),
)
const at = (names) => {
  let nodes = committedDump
  let node
  for (const name of names) {
    node = nodes.find((n) => n.name === name)
    assert.ok(node, `the dump has no ${names.join(" > ")}`)
    nodes = [...(node.children ?? []), ...(node.select_chain ?? [])]
  }
  return node
}

test("the committed dump names the values each kind of list offers", () => {
  // static names
  assert.deepEqual(
    at(["Settings", "Community Features", "Shortcut Overlay"]).options,
    ["Off", "On - Menu only", "On - Menu + Clip"],
  )
  // toggles, a Community Feature's and any other, as our toggle page draws them
  assert.deepEqual(
    at(["Settings", "Community Features", "Drum Randomizer"]).options,
    ["Off", "On"],
  )
  assert.deepEqual(at(["Sound", "Osc mixer", "Oscillator sync"]).options, [
    "Off",
    "On",
  ])
  // a names hook, asked with no owner
  assert.deepEqual(at(["Sound", "Play direction"]).options, [
    "Forward",
    "Reversed",
    "Ping-Pong",
  ])
  // a setting row's name table
  assert.deepEqual(at(["Sound", "Oscillator 1", "Type"]).options.slice(0, 3), [
    "Sine",
    "Triangle",
    "Square",
  ])
})

test("every root of the committed dump is placed, the nameless start marker editor as undocumented", () => {
  assert.equal(placeRoots(committedDump).length, MENU_TREES.length)
  assert.ok(committedDump.some((root) => root.name === ""))
})

test("the committed dump names the values a custom page or a worded number lists", () => {
  // an Int whose page spells each value as a word
  assert.deepEqual(at(["Note Editor", "Fill"]).options, [
    "OFF",
    "NOT FILL",
    "FILL",
  ])
  assert.deepEqual(at(["Row Editor", "Fill"]).options, [
    "OFF",
    "NOT FILL",
    "FILL",
  ])
  // custom pages
  assert.deepEqual(
    at(["MIDI instr", "Device Definition", "File Linked"]).options,
    ["Disabled", "Enabled"],
  )
  assert.deepEqual(at(["Perform FX", "Editing Mode"]).options, [
    "Disabled",
    "Value",
    "Param",
  ])
  const source = at(["Audio clip", "Audio source"]).options
  assert.equal(source.length, 9)
  assert.deepEqual(
    [source[0], source[5], source[8]],
    ["Disabled", "USB input", "Specific Track"],
  )
  for (const scales of [
    at(["Song", "Active Scales"]),
    at(["Settings", "Defaults", "Scale", "Active Scales"]),
  ]) {
    assert.equal(scales.options.length, 16)
    assert.deepEqual(scales.options.slice(0, 2), ["MAJOR", "MINOR"])
    assert.equal(scales.options[15], "HIRAJOSHI")
  }
})
