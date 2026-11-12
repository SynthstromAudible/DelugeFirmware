// The website's menu reference (src/data/generated/menu-hierarchies.json), built from the firmware's own dump of its
// menu tree (src/data/generated/menu-tree.json, written by the ui-menu-dump gate's scripts/menu_dump_check.sh).
//
//   node scripts/generate-menu-hierarchies.mjs           rewrite menu-hierarchies.json
//   node scripts/generate-menu-hierarchies.mjs --check   fail if menu-hierarchies.json is not what it would write
import fs from "node:fs"
import path from "node:path"
import { fileURLToPath, pathToFileURL } from "node:url"

const repoRoot = path.resolve(
  path.dirname(fileURLToPath(import.meta.url)),
  "../..",
)

/** Every file the reference is built from, relative to the repository root. */
export const INPUTS = {
  menuTree: "website/src/data/generated/menu-tree.json",
  english: "src/deluge/gui/l10n/english.json",
  menuTags: "website/src/data/menu-hierarchy-tags.json",
}

export const OUTPUT = "website/src/data/generated/menu-hierarchies.json"

/**
 * The reference's sections, each one root of the dump. A root is named by its name and, where two roots share one
 * (the synth's and a kit row's "Sound"), by which of them it is in the dump's order. `hides` names the availability
 * predicates (`available_when`) that never hold in that section: the CV instrument's root lists the MIDI rows too.
 */
export const MENU_TREES = [
  { treeKey: "synthSoundMenu", name: "Sound", occurrence: 0 },
  { treeKey: "kitSoundDrumMenu", name: "Sound", occurrence: 1 },
  { treeKey: "midiInstrumentMenu", name: "MIDI instr", occurrence: 0 },
  { treeKey: "kitMidiDrumMenu", name: "MIDI", occurrence: 0 },
  { treeKey: "kitGateDrumMenu", name: "Gate", occurrence: 0 },
  { treeKey: "audioClipMenu", name: "Audio clip", occurrence: 0 },
  { treeKey: "performFxMenu", name: "Perform FX", occurrence: 0 },
  { treeKey: "songMenu", name: "Song", occurrence: 0 },
  { treeKey: "kitFxMenu", name: "Kit FX", occurrence: 0 },
  { treeKey: "settingsMenu", name: "Settings", occurrence: 0 },
  {
    treeKey: "cvInstrumentMenu",
    name: "CV instrument",
    occurrence: 0,
    hides: ["is_midi_out"],
  },
  { treeKey: "noteEditorMenu", name: "Note Editor", occurrence: 0 },
  { treeKey: "noteRowEditorMenu", name: "Row Editor", occurrence: 0 },
]

/**
 * Roots of the dump the reference has no section for: pages that open from an item of another menu. The nameless one
 * is the audio clip's marker editor on START, which only a shortcut opens and which, as on the device, has no name;
 * its END twin is listed under the audio clip as Waveform.
 */
export const UNDOCUMENTED_ROOTS = [
  "Note range",
  "DX7 presets",
  "Sample recorder",
  "",
]

/**
 * The clip settings context menu (hold a clip pad in Song Grid View, press SELECT). It is a context menu, not a node
 * of the menu tree, so the dump does not list it; its rows are the strings ClipSettingsMenu shows for an instrument
 * clip (an audio clip's lists the last two), and Clip Mode's values are LaunchStyleMenu's.
 */
const CLIP_SETTINGS_MENU = {
  name: "Song Clip Settings",
  children: [
    { token: "STRING_FOR_CONVERT_TO_AUDIO" },
    {
      token: "STRING_FOR_CLIP_MODE",
      options: [
        "STRING_FOR_DEFAULT_LAUNCH",
        "STRING_FOR_FILL_LAUNCH",
        "STRING_FOR_ONCE_LAUNCH",
      ],
    },
    { token: "STRING_FOR_CLIP_NAME" },
  ],
}

/** Anchor-safe spelling of a label: lower case, every run of other characters one hyphen. */
export function slug(text) {
  const s = text
    .toLowerCase()
    .replace(/[^a-z0-9]+/g, "-")
    .replace(/^-+|-+$/g, "")
  return s || "item"
}

/**
 * A label with its slot filled in. The firmware draws a slot item's "*" as its slot's number ("**" as two digits),
 * and the dump lists such an item once per slot under the same name, so the n-th of them is slot n.
 */
export function fillSlot(name, index, count) {
  if (count < 2 || !name.includes("*")) return name
  const n = String(index + 1)
  return name.replace("**", n.padStart(2, "0")).replace("*", n)
}

/**
 * What a reader of the reference sees of a dump node: its subtree without ids and availability. Two siblings with
 * the same shape are the same row to a reader (the menu lists one variant per kind of owner and shows the one that
 * applies), so the reference lists it once.
 */
function shapeOf(node) {
  return JSON.stringify(node, (field, value) =>
    field === "id" || field === "available_when" ? undefined : value,
  )
}

/** Ids of every node the tree under `root` lists as a child: the place the reference shows those nodes. */
function listedIds(root) {
  const ids = new Set()
  const walk = (node) => {
    for (const child of node.children ?? []) {
      ids.add(child.id)
      walk(child)
    }
    for (const target of node.select_chain ?? []) walk(target)
  }
  walk(root)
  return ids
}

/**
 * One dump root as a reference tree. A node's rows are the values it offers (`options`, each an `option` row), its
 * children, then the pages its select press opens
 * (`select_chain`) that no list in this tree names, each shown once, under the first item that opens it. A cycle stub
 * (`ref`) names a page already open above it, so it is one of those two and is skipped with them.
 *
 * Rows of the same shape are listed once (`shapeOf`), except slot items ("*"), which are one row per slot.
 *
 * Keys are the node's labels from the section's top level down, slugged and joined by hyphens, so the same page has
 * the same key in every section that lists it at the same place; a key already used in this tree gets "-2", "-3".
 */
export function convertTree(root, rootKey, tags, hides = []) {
  const listed = listedIds(root)
  const chained = new Set()
  const used = new Set()

  const keyFor = (parentKey, label) => {
    const base = parentKey ? `${parentKey}-${slug(label)}` : slug(label)
    let key = base
    for (let n = 2; used.has(key); n += 1) key = `${base}-${n}`
    used.add(key)
    return key
  }

  const rowsOf = (node) => {
    const rows = []
    const shapes = new Set()
    const add = (row) => {
      const shape = shapeOf(row)
      if (!row.name.includes("*") && shapes.has(shape)) return
      shapes.add(shape)
      rows.push(row)
    }
    for (const child of node.children ?? []) {
      if (!hides.includes(child.available_when)) add(child)
    }
    for (const target of node.select_chain ?? []) {
      if (listed.has(target.id) || chained.has(target.id)) continue
      chained.add(target.id)
      add(target)
    }
    return rows
  }

  const convertRows = (node, parentKey) => {
    const values = (node.options ?? []).map((value) => ({
      name: value,
      title: value,
      option: true,
    }))
    const rows = [...values, ...rowsOf(node)]
    const counts = new Map()
    for (const row of rows)
      counts.set(row.name, (counts.get(row.name) ?? 0) + 1)
    const seen = new Map()
    return rows.map((row) => {
      const index = seen.get(row.name) ?? 0
      seen.set(row.name, index + 1)
      const count = counts.get(row.name)
      const label = fillSlot(row.name, index, count)
      const title = fillSlot(row.title ?? row.name, index, count)
      return convertNode(row, label, title, keyFor(parentKey, label))
    })
  }

  const convertNode = (node, label, title, key) => {
    const out = { key, oled: label, title, code: label }
    if (node.option) out.option = true
    const tag = tagFor(rootKey, key, label, tags)
    if (tag) out.tag = tag
    out.children = convertRows(node, key)
    return out
  }

  const out = {
    key: rootKey,
    oled: root.name,
    title: root.title ?? root.name,
    code: root.name,
  }
  const tag = tags.get(rootKey)
  if (tag) out.tag = tag
  out.children = convertRows(root, "")
  return out
}

/**
 * A row's version tag: the tags file's entry for "<section>/<key>", else for its key (one entry tags the row in every
 * section that lists it), else c1.0 for the template and dotted-note rows.
 */
function tagFor(rootKey, key, label, tags) {
  return (
    tags.get(`${rootKey}/${key}`) ??
    tags.get(key) ??
    (/(?:tplts|dtted)/i.test(label) ? "c1.0" : undefined)
  )
}

/** The clip settings context menu as a dump-shaped tree, its labels read from the English strings. */
export function clipSettingsRoot(english) {
  let id = 0
  const text = (token) => {
    if (!english[token]) throw new Error(`${INPUTS.english} has no ${token}`)
    return english[token]
  }
  const build = (entry) => {
    const name = entry.name ?? text(entry.token)
    id += 1
    return {
      id,
      name,
      options: (entry.options ?? []).map(text),
      children: (entry.children ?? []).map(build),
    }
  }
  return build(CLIP_SETTINGS_MENU)
}

/** The dump's roots placed in the reference's sections; throws on a root the reference does not place. */
export function placeRoots(dump) {
  const byName = new Map()
  for (const root of dump) {
    const roots = byName.get(root.name) ?? []
    roots.push(root)
    byName.set(root.name, roots)
  }
  const placed = new Set()
  const sections = MENU_TREES.map(({ treeKey, name, occurrence, hides }) => {
    const root = byName.get(name)?.[occurrence]
    if (!root)
      throw new Error(
        `${INPUTS.menuTree} has no root "${name}" (#${occurrence + 1}) for the ${treeKey} section`,
      )
    placed.add(root)
    return { treeKey, root, hides: hides ?? [] }
  })
  const unplaced = dump.filter(
    (root) => !placed.has(root) && !UNDOCUMENTED_ROOTS.includes(root.name),
  )
  if (unplaced.length > 0)
    throw new Error(
      [
        `${INPUTS.menuTree} has roots the menu reference does not place: ${unplaced.map((r) => `"${r.name}"`).join(", ")}.`,
        "Add each to MENU_TREES (a section of reference/menu_hierarchies.mdx) or to UNDOCUMENTED_ROOTS.",
      ].join("\n"),
    )
  return sections
}

/** The tags file as key -> tag; keys starting "__" are its section headings. */
export function readTags(parsed) {
  const tags = new Map()
  for (const [key, tag] of Object.entries(parsed)) {
    if (!key.startsWith("__") && typeof tag === "string" && tag.trim())
      tags.set(key, tag.trim())
  }
  return tags
}

/** The reference's JSON text from the parsed inputs. */
export function buildReference(dump, english, tagsFile) {
  const tags = readTags(tagsFile)
  const trees = {}
  for (const { treeKey, root, hides } of placeRoots(dump))
    trees[treeKey] = convertTree(root, treeKey, tags, hides)
  trees.songClipSettingsMenu = convertTree(
    clipSettingsRoot(english),
    "songClipSettingsMenu",
    tags,
  )
  return `${JSON.stringify({ sourceFiles: INPUTS, trees }, null, 2)}\n`
}

function readInput(relative) {
  return JSON.parse(fs.readFileSync(path.join(repoRoot, relative), "utf8"))
}

export function main(args) {
  const text = buildReference(
    readInput(INPUTS.menuTree),
    readInput(INPUTS.english).strings,
    readInput(INPUTS.menuTags),
  )
  const outputPath = path.join(repoRoot, OUTPUT)
  const current = fs.existsSync(outputPath)
    ? fs.readFileSync(outputPath, "utf8")
    : null
  if (args.includes("--check")) {
    if (current === text) return 0
    console.error(
      `${OUTPUT} is not what its inputs give: run \`pnpm run generate:menu-hierarchy\` in website/ and commit the result.`,
    )
    return 1
  }
  if (current !== text) fs.writeFileSync(outputPath, text, "utf8")
  console.log(`${OUTPUT}: ${current === text ? "unchanged" : "written"}`)
  return 0
}

if (
  process.argv[1] &&
  import.meta.url === pathToFileURL(process.argv[1]).href
) {
  process.exitCode = main(process.argv.slice(2))
}
