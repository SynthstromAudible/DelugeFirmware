import { execSync } from "node:child_process"
import fs from "node:fs"
import path from "node:path"
import { fileURLToPath } from "node:url"

const __filename = fileURLToPath(import.meta.url)
const __dirname = path.dirname(__filename)
const repoRoot = path.resolve(__dirname, "../..")

const input = {
  srcRoot: path.join(repoRoot, "src"),
  menusCpp: path.join(repoRoot, "src/deluge/gui/ui/menus.cpp"),
  generatedMenus: path.join(
    repoRoot,
    "src/deluge/gui/menu_item/generate/g_menus.inc",
  ),
  menuItemRoot: path.join(repoRoot, "src/deluge/gui/menu_item"),
  contextMenuRoot: path.join(repoRoot, "src/deluge/gui/context_menu"),
  english: path.join(repoRoot, "src/deluge/gui/l10n/english.json"),
  sevenSeg: path.join(repoRoot, "src/deluge/gui/l10n/seven_segment.json"),
  menuTags: path.join(repoRoot, "website/src/data/menu-hierarchy-tags.json"),
}

const REQUIRED_INPUT_KEYS = new Set([
  "srcRoot",
  "english",
  "sevenSeg",
  "menuTags",
])

const outputPath = path.join(
  repoRoot,
  "website/src/data/generated/menu-hierarchies.json",
)
const BASELINE_MENU_HIERARCHY_COMMIT =
  "6510025e4b7dc5710ba5cc9cde4743d7003b2e87"

function stripGeneratedMetadata(value) {
  if (Array.isArray(value)) {
    return value.map((item) => stripGeneratedMetadata(item))
  }

  if (value && typeof value === "object") {
    const normalized = {}
    for (const [key, child] of Object.entries(value)) {
      if (key === "generatedAt" || key === "varName") {
        continue
      }
      normalized[key] = stripGeneratedMetadata(child)
    }
    return normalized
  }

  return value
}

function stripBaselineComparisonMetadata(value) {
  const normalized = stripGeneratedMetadata(value)
  if (
    normalized &&
    typeof normalized === "object" &&
    !Array.isArray(normalized)
  ) {
    const withoutSourceFiles = { ...normalized }
    delete withoutSourceFiles.sourceFiles
    return withoutSourceFiles
  }
  return normalized
}

function menuHierarchyMatchesBaseline(currentPayload, baselinePayload) {
  const currentNormalized = stripBaselineComparisonMetadata(currentPayload)
  const baselineNormalized = stripBaselineComparisonMetadata(baselinePayload)
  return (
    JSON.stringify(currentNormalized) === JSON.stringify(baselineNormalized)
  )
}

function generatedPayloadsMatch(currentPayload, previousPayload) {
  const currentNormalized = stripGeneratedMetadata(currentPayload)
  const previousNormalized = stripGeneratedMetadata(previousPayload)
  return (
    JSON.stringify(currentNormalized) === JSON.stringify(previousNormalized)
  )
}

// These are the menus references in menu_hierarchies.mdx
// Define root menu here and reference it in menu_hierarchies.mdx so it can be rendered
// These are the root menu vars that are actually used in the firmware source code
const MENU_TREES = {
  settingsMenu: "settingsRootMenu",
  songMenu: "soundEditorRootMenuSongView",
  performFxMenu: "soundEditorRootMenuPerformanceView",
  audioClipMenu: "soundEditorRootMenuAudioClip",
  synthSoundMenu: "soundEditorRootMenu",
  kitFxMenu: "soundEditorRootMenuKitGlobalFX",
  kitSoundDrumMenu: "soundEditorRootMenuDrum",
  kitMidiDrumMenu: "soundEditorRootMenuMidiDrum",
  kitGateDrumMenu: "soundEditorRootMenuGateDrum",
  midiInstrumentMenu: "soundEditorRootMenuMIDIOrCV",
  cvInstrumentMenu: "soundEditorRootMenuMIDIOrCV",
  noteEditorMenu: "noteEditorRootMenu",
  noteRowEditorMenu: "noteRowEditorRootMenu",
}

// This renders the menu by using buildSongClipSettingsVirtualTree.
// It uses context menu option types from the firmware source corpus.
const VIRTUAL_TREES = {
  songClipSettingsMenu: "songClipSettingsMenu",
}

function extractIsRelevantBodies(menuItemCorpus, typeName) {
  const typeCandidates = unique([typeName, simpleTypeName(typeName)]).filter(
    Boolean,
  )
  const bodies = []

  for (const candidate of typeCandidates) {
    const qualifiedBodies = extractMethodBodies(
      menuItemCorpus,
      new RegExp(
        `${escapeForRegex(candidate)}::isRelevant\\s*\\([^)]*\\)`,
        "g",
      ),
    )
    bodies.push(...qualifiedBodies)
  }

  const classBody = extractClassBody(menuItemCorpus, typeName)
  if (classBody) {
    const unqualifiedBodies = extractMethodBodies(
      classBody,
      /isRelevant\s*\([^)]*\)\s*(?:override\s*)?/g,
    )
    bodies.push(...unqualifiedBodies)
  }

  return bodies
}

function bodyHasPositiveOutputTypeCheck(body, outputTypeName) {
  const member = `OutputType::${outputTypeName}`
  const escapedMember = escapeForRegex(member)
  return new RegExp(
    [
      `==\\s*${escapedMember}\\b`,
      `${escapedMember}\\s*==`,
      `case\\s+${escapedMember}\\b`,
      `one_of\\s*\\([^)]*${escapedMember}\\b`,
    ].join("|"),
  ).test(body)
}

function classifyMenuContextFromIsRelevant(menuItemCorpus, typeName) {
  if (!typeName) {
    return "shared"
  }

  const bodies = extractIsRelevantBodies(menuItemCorpus, typeName)
  if (bodies.length === 0) {
    return "shared"
  }

  const combined = bodies.join("\n")
  const hasMidi = bodyHasPositiveOutputTypeCheck(combined, "MIDI_OUT")
  const hasCv = bodyHasPositiveOutputTypeCheck(combined, "CV")

  if (hasMidi && !hasCv) {
    return "midi"
  }
  if (hasCv && !hasMidi) {
    return "cv"
  }
  return "shared"
}

function isVarRelevantForContext(varName, sourceStructure, context) {
  if (context === "shared") {
    return true
  }

  const typeName = sourceStructure.varToType.get(varName) ?? ""
  const classification = classifyMenuContextFromIsRelevant(
    sourceStructure.menuItemCorpus,
    typeName,
  )

  if (classification === "shared") {
    return true
  }

  return classification === context
}

function filterMenuTreeForContext(node, sourceStructure, context) {
  if (!node || typeof node !== "object") {
    return node
  }

  const filteredChildren = (node.children ?? [])
    .filter((child) =>
      isVarRelevantForContext(child.varName, sourceStructure, context),
    )
    .map((child) => filterMenuTreeForContext(child, sourceStructure, context))

  return {
    ...node,
    children: filteredChildren,
  }
}

function validateInputPaths(inputPaths) {
  const missing = []

  for (const [key, filePath] of Object.entries(inputPaths)) {
    const isRequired = REQUIRED_INPUT_KEYS.has(key)
    if (typeof filePath !== "string" || !filePath.trim()) {
      if (isRequired) {
        missing.push({ key, filePath: String(filePath), reason: "empty path" })
      }
      continue
    }

    if (!fs.existsSync(filePath)) {
      if (isRequired) {
        missing.push({ key, filePath, reason: "not found" })
      }
      continue
    }

    if (
      (key.endsWith("Root") || key.endsWith("root")) &&
      !fs.statSync(filePath).isDirectory()
    ) {
      if (isRequired) {
        missing.push({ key, filePath, reason: "expected directory" })
      }
      continue
    }
  }

  if (missing.length === 0) {
    return
  }

  const detailLines = missing.map(
    ({ key, filePath, reason }) =>
      `  - ${key}: ${path.relative(repoRoot, filePath)} (${reason})`,
  )

  throw new Error(
    [
      "Menu hierarchy generation failed: required input path(s) are invalid.",
      ...detailLines,
      "Update the input path mapping in website/scripts/generate-menu-hierarchies.mjs.",
    ].join("\n"),
  )
}

function discoverFirmwareRootMenuVars(sourceText) {
  const rootVars = new Set()
  const declarationPattern =
    /^\s*(?:PLACE_SDRAM_BSS\s+|PLACE_SDRAM_DATA\s+)?[A-Za-z_][A-Za-z0-9_:\s<>,*&]*?\s+([A-Za-z_][A-Za-z0-9_:]*)\s*\{\s*(?:l10n::String::)?STRING_FOR_[A-Z0-9_]+/gm

  for (const match of sourceText.matchAll(declarationPattern)) {
    const varName = stripNamespace(match[1])
    if (/(?:^|[A-Za-z])RootMenu(?:[A-Za-z0-9_]*)?$/.test(varName)) {
      rootVars.add(varName)
    }
  }

  return [...rootVars].sort()
}

function discoverReturnedRootMenuVars(sourceText) {
  return unique(
    [...sourceText.matchAll(/return\s*&\s*([A-Za-z_][A-Za-z0-9_:]*)\s*;/g)]
      .map((match) => stripNamespace(match[1]))
      .filter((varName) =>
        /(?:^|[A-Za-z])RootMenu(?:[A-Za-z0-9_]*)?$/.test(varName),
      ),
  )
}

function ensureAnchorsExist(varToToken, sourceText) {
  const missingRootVars = Object.values(MENU_TREES).filter(
    (varName) => !varToToken.has(varName),
  )

  if (missingRootVars.length > 0) {
    throw new Error(
      [
        "Menu hierarchy generation failed: one or more root menu vars were not found.",
        `Missing roots: ${missingRootVars.join(", ")}`,
        "Update MENU_TREES in website/scripts/generate-menu-hierarchies.mjs if firmware vars were renamed.",
      ].join("\n"),
    )
  }

  const discoveredRoots = discoverFirmwareRootMenuVars(sourceText)
  const trackedRoots = new Set(Object.values(MENU_TREES))
  const internalRoots = new Set(discoverReturnedRootMenuVars(sourceText))
  const untrackedRoots = discoveredRoots.filter(
    (varName) => !trackedRoots.has(varName) && !internalRoots.has(varName),
  )

  if (untrackedRoots.length > 0) {
    throw new Error(
      [
        "Menu hierarchy generation failed: new firmware root menu vars were detected but are not mapped.",
        `Untracked roots: ${untrackedRoots.join(", ")}`,
        "Add each new documented root to MENU_TREES; internal roots returned by firmware code are ignored automatically.",
      ].join("\n"),
    )
  }
}

function readJson(filePath) {
  return JSON.parse(fs.readFileSync(filePath, "utf8"))
}

function readMenuTags(filePath) {
  const parsed = readJson(filePath)
  const tagsByVarName = new Map()

  if (!parsed || typeof parsed !== "object") {
    return tagsByVarName
  }

  for (const [varName, value] of Object.entries(parsed)) {
    if (typeof varName !== "string" || !varName.trim()) {
      continue
    }

    const tags = Array.isArray(value) ? value : [value]
    const normalizedTags = tags
      .filter((tag) => typeof tag === "string")
      .map((tag) => tag.trim())
      .filter(Boolean)

    if (normalizedTags.length > 0) {
      tagsByVarName.set(varName, normalizedTags[0])
    }
  }

  return tagsByVarName
}

function applyMenuTags(node, tagsByVarName) {
  if (!node || typeof node !== "object") {
    return node
  }

  const tag = tagsByVarName.get(node.varName)
  if (tag) {
    node.tag = tag
  }

  for (const child of node.children ?? []) {
    applyMenuTags(child, tagsByVarName)
  }

  return node
}

function applyDerivedMenuTags(node) {
  if (!node || typeof node !== "object") {
    return node
  }

  if (
    !node.tag &&
    /(?:tplts|dtted)/i.test(`${node.oled ?? ""} ${node.code ?? ""}`)
  ) {
    node.tag = "c1.0"
  }

  for (const child of node.children ?? []) {
    applyDerivedMenuTags(child)
  }

  return node
}

function stripNamespace(symbol) {
  return symbol.split("::").at(-1)
}

function unique(values) {
  return [...new Set(values)]
}

function listFilesRecursive(rootDir, extensions) {
  const results = []
  const stack = [rootDir]

  while (stack.length > 0) {
    const current = stack.pop()
    const entries = fs.readdirSync(current, { withFileTypes: true })
    for (const entry of entries) {
      const fullPath = path.join(current, entry.name)
      if (entry.isDirectory()) {
        stack.push(fullPath)
      } else if (extensions.some((ext) => entry.name.endsWith(ext))) {
        results.push(fullPath)
      }
    }
  }

  return results
}

function listExistingFilesRecursive(rootDir, extensions) {
  if (!fs.existsSync(rootDir) || !fs.statSync(rootDir).isDirectory()) {
    return []
  }
  return listFilesRecursive(rootDir, extensions)
}

function readExistingFile(filePath) {
  if (!fs.existsSync(filePath) || fs.statSync(filePath).isDirectory()) {
    return null
  }
  return fs.readFileSync(filePath, "utf8")
}

function extractBackslashContinuedMacroDefinitions(sourceText) {
  const definitions = []
  const lines = sourceText.split(/\r?\n/)

  for (let i = 0; i < lines.length; i += 1) {
    if (!/^\s*#\s*define\b/.test(lines[i])) {
      continue
    }

    const bodyLines = []
    let line = lines[i]
    while (true) {
      const trimmed = line.trimEnd()
      bodyLines.push(trimmed.replace(/\\$/, ""))
      if (!trimmed.endsWith("\\") || i + 1 >= lines.length) {
        break
      }
      i += 1
      line = lines[i]
    }

    definitions.push(bodyLines.join("\n"))
  }

  return definitions
}

function collectVarToToken(sourceText) {
  const map = new Map()
  const declarationPattern =
    /^\s*(?:PLACE_SDRAM_BSS\s+|PLACE_SDRAM_DATA\s+)?[A-Za-z_][A-Za-z0-9_:\s<>,*&]*?\s+([A-Za-z_][A-Za-z0-9_:]*)\s*\{\s*(?:l10n::String::)?(STRING_FOR_[A-Z0-9_]+)/gm

  for (const match of sourceText.matchAll(declarationPattern)) {
    map.set(stripNamespace(match[1]), match[2])
  }

  return map
}

function normalizeTypeName(typeSpec) {
  const cleaned = typeSpec.replace(/\b(const|volatile|static|extern)\b/g, "")
  const token = cleaned.trim().split(/\s+/).at(-1) ?? ""
  return token.replace(/[&*]+$/g, "")
}

function collectVarToType(sourceText) {
  const map = new Map()
  const declarationPattern =
    /^\s*(?:PLACE_SDRAM_BSS\s+|PLACE_SDRAM_DATA\s+)?([A-Za-z_][A-Za-z0-9_:\s<>,*&]*?)\s+([A-Za-z_][A-Za-z0-9_:]*)\s*\{\s*(?:l10n::String::)?STRING_FOR_[A-Z0-9_]+/gm

  for (const match of sourceText.matchAll(declarationPattern)) {
    const typeName = normalizeTypeName(match[1])
    map.set(stripNamespace(match[2]), typeName)
  }

  return map
}

function extractPresetScaleNames(sourceText) {
  const scaleMacroBodies = extractBackslashContinuedMacroDefinitions(
    sourceText,
  ).filter((body) => /\bDEF_NOTES\s*\(/.test(body))
  const searchText =
    scaleMacroBodies.length > 0 ? scaleMacroBodies.join("\n\n") : sourceText
  const names = []
  for (const match of searchText.matchAll(
    /DEF\(\s*[A-Z0-9_]+\s*,\s*"([^"]+)"\s*,\s*DEF_NOTES\s*\(/g,
  )) {
    names.push(match[1])
  }
  return unique(names)
}

function extractFillOptionData(sourceText) {
  const signaturePattern =
    /const\s+char\s*\*\s+(?:[A-Za-z_][A-Za-z0-9_:]*::)?([A-Za-z_][A-Za-z0-9_]*)\s*\(\s*uint8_t\s+([A-Za-z_][A-Za-z0-9_]*)\s*\)/g
  const matches = extractMethodBodyMatches(sourceText, signaturePattern)
  const candidates = []

  for (const { body, match: signatureMatch } of matches) {
    if (!/fill/i.test(`${signatureMatch[1]} ${signatureMatch[2]}`)) {
      continue
    }

    const labels = []
    for (const match of body.matchAll(/return\s+"([^"]+)"\s*;/g)) {
      labels.push(match[1])
    }

    if (labels.length >= 2) {
      candidates.push({
        functionName: signatureMatch[1],
        labels: unique(labels),
      })
    }
  }

  return { candidates }
}

function extractConstCharStringArrays(sourceText) {
  const arrays = new Map()
  const arrayPattern =
    /(?:static\s+)?const\s+char\s*\*\s+([A-Za-z_][A-Za-z0-9_]*)\s*(?:\[\s*\])?\s*=?\s*\{/g

  for (const match of sourceText.matchAll(arrayPattern)) {
    if (match.index === undefined) {
      continue
    }

    const openBraceIndex = sourceText.indexOf("{", match.index)
    if (openBraceIndex < 0) {
      continue
    }

    const closeBraceIndex = findMatchingBrace(sourceText, openBraceIndex)
    if (closeBraceIndex < 0) {
      continue
    }

    const body = sourceText.slice(openBraceIndex + 1, closeBraceIndex)
    arrays.set(
      match[1],
      [...body.matchAll(/"([^"\\]*(?:\\.[^"\\]*)*)"/g)].map(
        (stringMatch) => stringMatch[1],
      ),
    )
  }

  return arrays
}

function extractInitializedStringCollections(sourceText) {
  const arrays = new Map()
  const collectionPattern =
    /(?:static\s+)?(?:const\s+)?(?:[A-Za-z_][A-Za-z0-9_]*::)*(?:vector|array)\s*<[^;{}=]+>\s+([A-Za-z_][A-Za-z0-9_]*)\s*=?\s*\{/g

  for (const match of sourceText.matchAll(collectionPattern)) {
    if (match.index === undefined) {
      continue
    }

    const openBraceIndex = sourceText.indexOf("{", match.index)
    if (openBraceIndex < 0) {
      continue
    }

    const closeBraceIndex = findMatchingBrace(sourceText, openBraceIndex)
    if (closeBraceIndex < 0) {
      continue
    }

    const body = sourceText.slice(openBraceIndex + 1, closeBraceIndex)
    arrays.set(
      match[1],
      [...body.matchAll(/"([^"\\]*(?:\\.[^"\\]*)*)"/g)].map(
        (stringMatch) => stringMatch[1],
      ),
    )
  }

  return arrays
}

function extractDxParamDescriptorContext(sourceText) {
  const arrays = extractConstCharStringArrays(sourceText)
  const opLong = arrays.get("desc_op_long")
  const opShort = arrays.get("desc_op_short")
  const globalLong = arrays.get("desc_global_long")
  const globalShort = arrays.get("desc_global_short")

  if (
    !opLong?.length ||
    !opShort?.length ||
    !globalLong?.length ||
    !globalShort?.length
  ) {
    return null
  }

  const globalBaseExpression =
    /desc_global_long\s*\[\s*param\s*-\s*([^\]]+)\]/.exec(sourceText)?.[1] ??
    "6 * 21"
  const globalBase = evaluateIntegerExpression(globalBaseExpression, new Map())

  if (globalBase === null) {
    return null
  }

  return {
    opLong,
    opShort,
    globalLong,
    globalShort,
    curves: arrays.get("curves") ?? [],
    shapesLong: arrays.get("shapes_long") ?? [],
    shapesShort: arrays.get("shapes_short") ?? [],
    globalBase,
    opGroupSize: opLong.length,
    opGroups: [
      [0, 7],
      [8, 12],
      [13, 15],
      [16, opLong.length - 1],
    ],
    globalGroups: [
      [0, 7],
      [8, 8],
      [9, 9],
      [10, 10],
      [11, globalLong.length - 1],
    ],
  }
}

function extractNumericDefines(sourceText) {
  const defines = new Map()
  for (const match of sourceText.matchAll(
    /^\s*#define\s+([A-Z_][A-Z0-9_]*)\s+(-?\d+)\s*$/gm,
  )) {
    defines.set(match[1], Number.parseInt(match[2], 10))
  }
  return defines
}

function evaluateIntegerExpression(expression, valueMap) {
  if (!expression) {
    return null
  }

  let expanded = expression
  for (const [name, value] of valueMap) {
    expanded = expanded.replace(
      new RegExp(`\\b${escapeForRegex(name)}\\b`, "g"),
      String(value),
    )
  }

  if (!/^[0-9+\-*/%\s()]+$/.test(expanded)) {
    return null
  }

  try {
    const value = Function(`"use strict"; return (${expanded});`)()
    return Number.isInteger(value) ? value : null
  } catch {
    return null
  }
}

function evaluateIntegerComparison(leftValue, operator, rightValue) {
  switch (operator) {
    case "==":
      return leftValue === rightValue
    case "!=":
      return leftValue !== rightValue
    case ">=":
      return leftValue >= rightValue
    case "<=":
      return leftValue <= rightValue
    case ">":
      return leftValue > rightValue
    case "<":
      return leftValue < rightValue
    default:
      return false
  }
}

function extractConstexprIntegers(sourceText) {
  const values = new Map()
  const pending = []

  for (const match of sourceText.matchAll(
    /constexpr\s+int32_t\s+([A-Za-z_][A-Za-z0-9_]*)\s*=\s*([^;]+);/g,
  )) {
    pending.push({ name: match[1], expression: match[2] })
  }

  let progressed = true
  while (progressed && pending.length > 0) {
    progressed = false
    for (let i = pending.length - 1; i >= 0; i -= 1) {
      const candidate = pending[i]
      const value = evaluateIntegerExpression(candidate.expression, values)
      if (value === null) {
        continue
      }
      values.set(candidate.name, value)
      pending.splice(i, 1)
      progressed = true
    }
  }

  return values
}

function extractIterancePresets(sourceText) {
  const tableMatch =
    /iterancePresets\s*=\s*\{([\s\S]*?)\}\s*;/m.exec(sourceText) ??
    [
      ...sourceText.matchAll(
        /(?:^|\n)[^\n;{}=]*\bIterance\b[^\n;{}=]*\b[A-Za-z_][A-Za-z0-9_]*\s*=\s*\{([\s\S]*?)\}\s*;/g,
      ),
    ].find((match) => /\bIterance\s*\{/.test(match[1]))
  if (!tableMatch) {
    return []
  }

  const presets = []
  for (const match of tableMatch[1].matchAll(
    /Iterance\s*\{\s*(\d+)\s*,\s*(0b[01]+|\d+)\s*\}/g,
  )) {
    presets.push({
      divisor: Number.parseInt(match[1], 10),
      stepMask: Number.parseInt(match[2].replace(/^0b/, ""), 2),
    })
  }
  return presets
}

function extractIteranceDisplayLiterals(sourceText) {
  const displayBody = extractMethodBodies(
    sourceText,
    /void\s+(?:[A-Za-z_][A-Za-z0-9_:]*::)?displayIterance\s*\([^)]*\bIterance\b[^)]*\)/g,
  ).find((candidate) => /\bformat_display_value\s*\(/.test(candidate))
  const formatterBody = extractMethodBodies(
    sourceText,
    /void\s+(?:[A-Za-z_][A-Za-z0-9_:]*::)?format_preset_display_value\s*\([^)]*\bpreset_index\b[^)]*\)/g,
  )[0]

  if (!displayBody || !formatterBody) {
    return null
  }

  const firstShortLabel = extractIteranceFirstShortLabel(sourceText)
  const branchLabels = extractIteranceFormatterBranchLabels(
    formatterBody,
    firstShortLabel,
  )
  const displayOptions = extractIteranceDisplayOptions(displayBody)

  return {
    branchLabels,
    oledFormat: displayOptions?.oledFormat ?? null,
    codeFormat: displayOptions?.codeFormat ?? null,
  }
}

function extractIteranceFirstShortLabel(sourceText) {
  const body = extractMethodBodies(
    sourceText,
    /char\s+const\s*\*\s+get_first_label\s*\([^)]*\)/g,
  )[0]
  const displayTernaryMatch =
    /display->haveOLED\(\)\s*\?\s*"([^"]+)"\s*:\s*"([^"]+)"/.exec(body ?? "")

  return displayTernaryMatch ? displayTernaryMatch[2] : "1 ST"
}

function extractIteranceFormatterBranchLabels(formatterBody, firstShortLabel) {
  return [
    ...formatterBody.matchAll(
      /(?:if|else\s+if)\s*\(([^)]+)\)\s*\{([\s\S]*?)\}/g,
    ),
  ]
    .map((match) => {
      const labelMatch =
        /copy_with_prefix\s*\([^,]+,\s*[^,]+,\s*[^,]+,\s*(?:"([^"]+)"|get_first_label\s*\([^)]*\))\s*\)/.exec(
          match[2],
        )
      if (!labelMatch) {
        return null
      }

      return {
        condition: match[1].trim(),
        shortLabel: labelMatch[1] ?? firstShortLabel,
      }
    })
    .filter(Boolean)
}

function extractIteranceDisplayOptions(displayBody) {
  const stepFormatMatch =
    /\.step_format\s*=\s*display->haveOLED\(\)\s*\?\s*"([^"]+)"\s*:\s*"([^"]+)"/.exec(
      displayBody,
    )
  const prefixMatch =
    /\.prefix\s*=\s*display->haveOLED\(\)\s*\?\s*"([^"]*)"\s*:\s*"([^"]*)"/.exec(
      displayBody,
    )

  if (!stepFormatMatch) {
    return null
  }

  return {
    oledFormat: `${prefixMatch?.[1] ?? ""}${stepFormatMatch[1]}`,
    codeFormat: `${prefixMatch?.[2] ?? ""}${stepFormatMatch[2]}`,
  }
}

function resolveIterancePresetIndexFromCondition(condition, valueMap) {
  const equalityMatch = /preset_index\s*==\s*([^&|]+)/.exec(condition)
  if (!equalityMatch) {
    return null
  }

  return evaluateIntegerExpression(equalityMatch[1].trim(), valueMap)
}

function buildIteranceConditionValues(iteranceConstants, numPresets) {
  const defaultPreset =
    iteranceConstants.get("kDefaultIterancePreset") ??
    iteranceConstants.get("DEFAULT_PRESET_INDEX") ??
    0
  const customPreset =
    iteranceConstants.get("kCustomIterancePreset") ??
    iteranceConstants.get("CUSTOM_PRESET_INDEX") ??
    numPresets + 1
  const conditionValues = new Map(iteranceConstants)
  const inferredPresetIndexes = {
    kDefaultIterancePreset: defaultPreset,
    DEFAULT_PRESET_INDEX: defaultPreset,
    kCustomIterancePreset: customPreset,
    CUSTOM_PRESET_INDEX: customPreset,
  }
  for (const [name, value] of Object.entries(inferredPresetIndexes)) {
    if (!conditionValues.has(name)) {
      conditionValues.set(name, value)
    }
  }

  return { conditionValues, customPreset, defaultPreset }
}

function formatIteranceLabel(formatString, step, divisor) {
  if (!formatString) {
    return null
  }

  let replaced = 0
  const result = formatString.replace(/%d/g, () => {
    const value = replaced === 0 ? step : divisor
    replaced += 1
    return String(value)
  })

  return replaced >= 2 ? result : null
}

function highestSetBitIndex(mask, maxIndex) {
  for (let i = maxIndex; i >= 0; i -= 1) {
    if ((mask & (1 << i)) !== 0) {
      return i
    }
  }
  return -1
}

function buildIteranceOptionLabels(
  iterancePresets,
  iteranceLiterals,
  iteranceConstants,
) {
  const numPresets = iterancePresets.length
  const { conditionValues, customPreset, defaultPreset } =
    buildIteranceConditionValues(iteranceConstants, numPresets)
  const presetLabelMap = new Map()

  if (
    iterancePresets.length === 0 ||
    !iteranceLiterals ||
    !iteranceLiterals.branchLabels ||
    iteranceLiterals.branchLabels.length === 0 ||
    !iteranceLiterals.oledFormat ||
    !iteranceLiterals.codeFormat
  ) {
    return { labels: [], conditionValues, customPreset }
  }

  for (const { condition, shortLabel } of iteranceLiterals.branchLabels) {
    const presetIndex = resolveIterancePresetIndexFromCondition(
      condition,
      conditionValues,
    )
    if (presetIndex === null) {
      continue
    }
    presetLabelMap.set(presetIndex, {
      oled: shortLabel,
      code: shortLabel,
    })
  }

  const labels = []
  for (
    let presetIndex = defaultPreset;
    presetIndex <= customPreset;
    presetIndex += 1
  ) {
    const specialLabel = presetLabelMap.get(presetIndex)
    if (specialLabel) {
      labels.push(specialLabel)
      continue
    }

    const preset = iterancePresets[presetIndex - 1]
    if (!preset || preset.divisor <= 0) {
      continue
    }

    const activeStep = highestSetBitIndex(preset.stepMask, preset.divisor)
    if (activeStep < 0) {
      continue
    }

    const step = activeStep + 1
    const oledLabel = formatIteranceLabel(
      iteranceLiterals.oledFormat,
      step,
      preset.divisor,
    )
    const codeLabel = formatIteranceLabel(
      iteranceLiterals.codeFormat,
      step,
      preset.divisor,
    )
    if (!oledLabel || !codeLabel) {
      continue
    }

    labels.push({
      oled: oledLabel,
      code: codeLabel,
    })
  }

  return { labels, conditionValues, customPreset }
}

function extractDefaultMagnitude(sourceText) {
  const directMatch =
    /defaultMagnitude\s*=\s*(-?\d+)\s*;[\s\S]*?defaultSwingInterval\s*=\s*8\s*-\s*defaultMagnitude/m.exec(
      sourceText,
    )
  return directMatch ? Number.parseInt(directMatch[1], 10) : null
}

function extractNoteMagnitudeBase(sourceText) {
  const match =
    /getNoteMagnitudeFfromNoteLength\s*\([^)]*\)\s*\{[\s\S]*?noteMagnitude\s*=\s*(-?\d+)\s*-\s*tickMagnitude\s*;/m.exec(
      sourceText,
    )
  return match ? Number.parseInt(match[1], 10) : null
}

function extractSyncTypeSuffixes(sourceText) {
  const suffixes = new Map()
  const syncLabelBodies = extractMethodBodies(
    sourceText,
    /\bvoid\s+syncValueToString\s*\([^)]*\)/g,
  )
  const searchText =
    syncLabelBodies.find((body) => /\btypeStr\b/.test(body)) ??
    syncLabelBodies[0] ??
    sourceText

  for (const match of searchText.matchAll(
    /case\s+([A-Z_][A-Z0-9_]*)\s*:\s*([\s\S]*?)break\s*;/g,
  )) {
    const typeName = match[1]
    const caseBody = match[2]
    const suffixMatch = /typeStr\s*=\s*"([^"]+)"\s*;/.exec(caseBody)
    if (suffixMatch) {
      suffixes.set(typeName, suffixMatch[1])
    }
  }
  return suffixes
}

function extractEnumEntries(sourceText, enumName) {
  const enumMatch = new RegExp(
    `enum\\s+(?:class\\s+)?${escapeForRegex(enumName)}\\s*(?::\\s*[A-Za-z_][A-Za-z0-9_:]*)?\\s*\\{([\\s\\S]*?)\\}`,
    "m",
  ).exec(sourceText)
  if (!enumMatch) {
    return []
  }

  const entries = []
  for (const match of enumMatch[1].matchAll(
    /\b([A-Z_][A-Z0-9_]*)\s*=\s*(-?\d+)/g,
  )) {
    entries.push({
      name: match[1],
      value: Number.parseInt(match[2], 10),
    })
  }

  return entries.sort((a, b) => a.value - b.value)
}

function extractEnumEntriesByNameOrEntryPrefix(
  sourceText,
  enumName,
  entryPrefix,
) {
  const namedEntries = extractEnumEntries(sourceText, enumName)
  if (namedEntries.length > 0) {
    return namedEntries
  }

  for (const enumMatch of sourceText.matchAll(
    /enum\s+(?:class\s+)?[A-Za-z_][A-Za-z0-9_]*\s*(?::\s*[A-Za-z_][A-Za-z0-9_:]*)?\s*\{([\s\S]*?)\}/g,
  )) {
    const entries = []
    for (const match of enumMatch[1].matchAll(
      /\b([A-Z_][A-Z0-9_]*)\s*=\s*(-?\d+)/g,
    )) {
      entries.push({
        name: match[1],
        value: Number.parseInt(match[2], 10),
      })
    }

    if (entries.some((entry) => entry.name.startsWith(entryPrefix))) {
      return entries.sort((a, b) => a.value - b.value)
    }
  }

  return []
}

function findConstantInExpression(sourceText, expressionPattern) {
  const match = expressionPattern.exec(sourceText)
  return match ? match[1] : null
}

function getSyncTypeEntryForOption(option, syncTypeEntries) {
  let resolved = null
  for (const entry of syncTypeEntries) {
    if (option >= entry.value) {
      resolved = entry
    } else {
      break
    }
  }
  return resolved
}

function getSyncTypeGroupIndex(syncTypeEntries, typeName) {
  return syncTypeEntries.findIndex((entry) => entry.name === typeName)
}

function syncLevelForOption(option, syncTypeEntries, syncTypeName) {
  const groupIndex = getSyncTypeGroupIndex(syncTypeEntries, syncTypeName)
  if (groupIndex < 0) {
    return option
  }

  const groupStart = syncTypeEntries[groupIndex]?.value ?? 0
  const offset = groupIndex > 0 ? 1 : 0
  return option - groupStart + offset
}

function buildSyncOptionContext(sourceText) {
  const syncTypeEntries = extractEnumEntriesByNameOrEntryPrefix(
    sourceText,
    "SyncType",
    "SYNC_TYPE_",
  )
  const syncLevelEntries = extractEnumEntriesByNameOrEntryPrefix(
    sourceText,
    "SyncLevel",
    "SYNC_LEVEL_",
  )
  if (syncTypeEntries.length === 0 || syncLevelEntries.length === 0) {
    return null
  }

  const syncLevelValues = new Map(
    syncLevelEntries.map((entry) => [entry.name, entry.value]),
  )
  const shiftAnchorName = findConstantInExpression(
    sourceText,
    /shift\s*=\s*([A-Z_][A-Z0-9_]*)\s*-\s*level/,
  )
  const barLevelName = findConstantInExpression(
    sourceText,
    /magnitudeLevelBars\s*=\s*([A-Z_][A-Z0-9_]*)\s*-\s*tickMagnitude/,
  )
  if (!shiftAnchorName || !barLevelName) {
    return null
  }

  const shiftAnchor = syncLevelValues.get(shiftAnchorName)
  const barLevelBase = syncLevelValues.get(barLevelName)
  const defaultMagnitude = extractDefaultMagnitude(sourceText)
  const noteMagnitudeBase = extractNoteMagnitudeBase(sourceText)
  if (
    shiftAnchor === undefined ||
    barLevelBase === undefined ||
    defaultMagnitude === null ||
    noteMagnitudeBase === null
  ) {
    return null
  }

  return {
    syncTypeEntries,
    syncTypeSuffixes: extractSyncTypeSuffixes(sourceText),
    shiftAnchor,
    barLevelBase,
    defaultMagnitude,
    noteMagnitudeBase,
  }
}

function getNoteMagnitudeFromSyncLevel(
  syncLevel,
  syncLevel256th,
  defaultMagnitude,
  noteMagnitudeBase,
) {
  const noteLength = 3 << (syncLevel256th - syncLevel)

  let noteMagnitude = noteMagnitudeBase - defaultMagnitude
  let level = 3
  while (level < noteLength) {
    noteMagnitude += 1
    level <<= 1
  }

  return noteMagnitude
}

function buildOledSyncLabel(noteMagnitude, suffix, appendSuffixForBars) {
  if (noteMagnitude < 0) {
    const division = 1 << -noteMagnitude
    const ordinal = division % 10 === 2 ? "nd" : "th"
    return `${division}${ordinal}${suffix ?? ""}`
  }

  const bars = 1 << noteMagnitude
  return appendSuffixForBars && suffix ? `${bars}-bar${suffix}` : `${bars}-bar`
}

function buildSevenSegSyncLabel(noteMagnitude, suffix) {
  const upperSuffix = (suffix ?? "").toUpperCase()

  if (noteMagnitude < 0) {
    const division = 1 << -noteMagnitude
    let base = ""
    if (division <= 9999) {
      base = `${division}`
      if (division === 2 || division === 32) {
        base += "ND"
      } else if (division <= 99) {
        base += "TH"
      } else if (division <= 999) {
        base += "T"
      }
    } else {
      base = "TINY"
    }

    return `${base}${upperSuffix}`
  }

  const bars = 1 << noteMagnitude
  let base = ""
  if (bars <= 9999) {
    base = `${bars}`
    if (base.length === 1) {
      base += "BAR"
    } else if (base.length <= 3) {
      base += "B"
    }
  } else {
    base = "BIG"
  }

  return `${base}${upperSuffix}`
}

function buildSyncLabelForOption(option, syncContext) {
  const syncTypeEntry = getSyncTypeEntryForOption(
    option,
    syncContext.syncTypeEntries,
  )
  if (!syncTypeEntry) {
    return null
  }

  const syncType = syncTypeEntry.name
  const syncTypeGroupIndex = getSyncTypeGroupIndex(
    syncContext.syncTypeEntries,
    syncType,
  )
  const syncLevel = syncLevelForOption(
    option,
    syncContext.syncTypeEntries,
    syncType,
  )
  const suffix = syncContext.syncTypeSuffixes.get(syncType) ?? ""

  const noteMagnitude = getNoteMagnitudeFromSyncLevel(
    syncLevel,
    syncContext.shiftAnchor,
    syncContext.defaultMagnitude,
    syncContext.noteMagnitudeBase,
  )

  const magnitudeLevelBars =
    syncContext.barLevelBase - syncContext.defaultMagnitude
  const appendSuffixForBars =
    syncTypeGroupIndex > 0 && syncLevel <= magnitudeLevelBars

  return {
    oled: buildOledSyncLabel(noteMagnitude, suffix, appendSuffixForBars),
    code: buildSevenSegSyncLabel(noteMagnitude, suffix),
  }
}

function extractInitializerBody(sourceText, varName) {
  const markerPattern = new RegExp(`\\b${varName}\\s*\\{`, "m")
  const markerMatch = markerPattern.exec(sourceText)
  if (!markerMatch || markerMatch.index === undefined) {
    return undefined
  }

  const openBraceIndex = sourceText.indexOf("{", markerMatch.index)
  if (openBraceIndex < 0) {
    return undefined
  }

  let depth = 0
  for (let i = openBraceIndex; i < sourceText.length; i++) {
    const c = sourceText[i]
    if (c === "{") {
      depth += 1
    } else if (c === "}") {
      depth -= 1
      if (depth === 0) {
        return sourceText.slice(openBraceIndex + 1, i)
      }
    }
  }

  return undefined
}

function extractArrayChildren(sourceText) {
  const map = new Map()
  const pattern =
    /std::array\s*<\s*(?:const\s+)?MenuItem\s*\*\s*,\s*[^>]+>\s+([A-Za-z_][A-Za-z0-9_]*)\s*(?:=\s*)?\{([\s\S]*?)\};/g

  for (const match of sourceText.matchAll(pattern)) {
    const arrayName = match[1]
    const refs = []
    for (const refMatch of match[2].matchAll(
      /&\s*([A-Za-z_][A-Za-z0-9_:]*)/g,
    )) {
      refs.push(stripNamespace(refMatch[1]))
    }
    map.set(arrayName, unique(refs))
  }

  return map
}

function extractChildren(sourceText, arrayChildren, varName) {
  const body = extractInitializerBody(sourceText, varName)
  if (!body) {
    return []
  }

  const refs = []
  for (const match of body.matchAll(/&\s*([A-Za-z_][A-Za-z0-9_:]*)/g)) {
    refs.push(stripNamespace(match[1]))
  }

  for (const [arrayName, members] of arrayChildren) {
    const arrayPattern = new RegExp(`\\b${arrayName}\\b`)
    if (arrayPattern.test(body)) {
      refs.push(...members)
    }
  }

  return unique(refs)
}

function extractRuntimeFeatureTypeFromClassCtor(className, sourceText) {
  const pattern = new RegExp(
    `${escapeForRegex(className)}\\s*\\([^)]*\\)\\s*:\\s*[^{;]*RuntimeFeatureSettingType::([A-Za-z0-9_]+)`,
  )
  const match = pattern.exec(sourceText)
  return match ? match[1] : null
}

function extractRuntimeFeatureMenuVarToSettingType(menuSource, menuItemCorpus) {
  const mapping = new Map()

  for (const match of menuSource.matchAll(
    /\b[A-Za-z_][A-Za-z0-9_:<>]*\s+([A-Za-z_][A-Za-z0-9_]*)\(RuntimeFeatureSettingType::([A-Za-z0-9_]+)\)\s*;/g,
  )) {
    mapping.set(match[1], match[2])
  }

  for (const match of menuSource.matchAll(
    /\b([A-Za-z_][A-Za-z0-9_:<>]*)\s+([A-Za-z_][A-Za-z0-9_]*)\s*\{\s*\}\s*;/g,
  )) {
    const className = stripNamespace(match[1])
    const varName = match[2]
    if (mapping.has(varName)) {
      continue
    }

    const settingType = extractRuntimeFeatureTypeFromClassCtor(
      className,
      menuItemCorpus,
    )
    if (settingType) {
      mapping.set(varName, settingType)
    }
  }

  return mapping
}

function extractRuntimeFeatureSettingTypeToToken(sourceText) {
  const mapping = new Map()

  for (const match of sourceText.matchAll(
    /Setup[A-Za-z0-9_]*Setting\(\s*settings\[RuntimeFeatureSettingType::([A-Za-z0-9_]+)\]\s*,\s*(STRING_FOR_[A-Z0-9_]+)/g,
  )) {
    mapping.set(match[1], match[2])
  }

  return mapping
}

function buildRuntimeFeatureMenuVarToToken(sourceStructure) {
  const mapping = new Map()

  for (const [
    varName,
    settingType,
  ] of sourceStructure.runtimeFeatureMenuVarToSettingType) {
    const token =
      sourceStructure.runtimeFeatureSettingTypeToToken.get(settingType)
    if (token) {
      mapping.set(varName, token)
    }
  }

  return mapping
}

function build() {
  validateInputPaths(input)

  const sourceFiles = listFilesRecursive(input.srcRoot, [
    ".h",
    ".hpp",
    ".cpp",
    ".cxx",
  ])
  const sourceCorpus = sourceFiles
    .map((filePath) => fs.readFileSync(filePath, "utf8"))
    .join("\n\n")
  const menusCpp = readExistingFile(input.menusCpp) ?? sourceCorpus
  const generatedMenus = readExistingFile(input.generatedMenus) ?? sourceCorpus
  const menuItemFiles = listExistingFilesRecursive(input.menuItemRoot, [
    ".h",
    ".hpp",
    ".cpp",
  ])
  const contextMenuFiles = listExistingFilesRecursive(input.contextMenuRoot, [
    ".h",
    ".hpp",
    ".cpp",
  ])
  const menuSourceFiles = unique([...menuItemFiles, ...contextMenuFiles])
  const menuItemCorpusFiles =
    menuSourceFiles.length > 0 ? menuSourceFiles : sourceFiles
  const menuItemCorpus = menuItemCorpusFiles
    .map((filePath) => fs.readFileSync(filePath, "utf8"))
    .join("\n\n")
  const english = readJson(input.english).strings
  const sevenSeg = readJson(input.sevenSeg).strings
  const menuTagsByVarName = readMenuTags(input.menuTags)

  const combined = `${generatedMenus}\n${menusCpp}\n${sourceCorpus}`
  const varToToken = new Map([
    ...collectVarToToken(generatedMenus),
    ...collectVarToToken(menusCpp),
    ...collectVarToToken(menuItemCorpus),
    ...collectVarToToken(sourceCorpus),
  ])
  const varToType = new Map([
    ...collectVarToType(generatedMenus),
    ...collectVarToType(menusCpp),
    ...collectVarToType(menuItemCorpus),
    ...collectVarToType(sourceCorpus),
  ])
  const arrayChildren = extractArrayChildren(`${combined}\n${menuItemCorpus}`)
  const syncOptionContext = buildSyncOptionContext(sourceCorpus)
  const iteranceConstants = extractConstexprIntegers(sourceCorpus)
  const iterancePresets = extractIterancePresets(sourceCorpus)
  const iteranceLiterals = extractIteranceDisplayLiterals(sourceCorpus)
  const iteranceOptionData = buildIteranceOptionLabels(
    iterancePresets,
    iteranceLiterals,
    iteranceConstants,
  )
  const numericDefines = extractNumericDefines(sourceCorpus)
  const syncOffLabel = labelFromToken(english, sevenSeg, "STRING_FOR_OFF")
  const fillOptionData = extractFillOptionData(sourceCorpus)
  const dxParamDescriptorContext = extractDxParamDescriptorContext(sourceCorpus)
  const runtimeFeatureMenuVarToSettingType =
    extractRuntimeFeatureMenuVarToSettingType(sourceCorpus, menuItemCorpus)
  const runtimeFeatureSettingTypeToToken =
    extractRuntimeFeatureSettingTypeToToken(sourceCorpus)
  const sourceStructure = {
    combined,
    arrayChildren,
    varToToken,
    varToType,
    english,
    sevenSeg,
    menuItemCorpus,
    sourceCorpus,
    numericDefines,
    typeInheritanceCache: new Map(),
    presetScaleNames: extractPresetScaleNames(sourceCorpus),
    fillOptionCandidates: fillOptionData.candidates,
    dxParamDescriptorContext,
    iteranceOptionLabels: iteranceOptionData.labels,
    iteranceConditionValues: iteranceOptionData.conditionValues,
    iteranceCustomPreset: iteranceOptionData.customPreset,
    syncOptionContext,
    syncOffLabel,
    runtimeFeatureMenuVarToSettingType,
    runtimeFeatureSettingTypeToToken,
  }
  sourceStructure.runtimeFeatureMenuVarToToken =
    buildRuntimeFeatureMenuVarToToken(sourceStructure)

  ensureAnchorsExist(varToToken, sourceCorpus)

  const trees = {}
  for (const [treeKey, rootVar] of Object.entries(MENU_TREES)) {
    const node = buildNode(
      combined,
      arrayChildren,
      varToToken,
      english,
      sevenSeg,
      sourceStructure,
      rootVar,
      [],
      0,
    )
    if (node) {
      trees[treeKey] = applyDerivedMenuTags(
        applyMenuTags(node, menuTagsByVarName),
      )
    }
  }

  if (trees.midiInstrumentMenu) {
    trees.midiInstrumentMenu = filterMenuTreeForContext(
      trees.midiInstrumentMenu,
      sourceStructure,
      "midi",
    )
    trees.midiInstrumentMenu = {
      ...trees.midiInstrumentMenu,
      oled: "MIDI Instrument",
      code: "MIDI",
    }
  }

  if (trees.cvInstrumentMenu) {
    trees.cvInstrumentMenu = filterMenuTreeForContext(
      trees.cvInstrumentMenu,
      sourceStructure,
      "cv",
    )
    trees.cvInstrumentMenu = {
      ...trees.cvInstrumentMenu,
      oled: "CV Instrument",
      code: "CV",
    }
  }

  trees.songClipSettingsMenu = applyMenuTags(
    buildSongClipSettingsVirtualTree(sourceCorpus, english, sevenSeg),
    menuTagsByVarName,
  )

  const payload = {
    sourceFiles: {
      srcRoot: path.relative(repoRoot, input.srcRoot),
      menusCpp: path.relative(repoRoot, input.menusCpp),
      generatedMenus: path.relative(repoRoot, input.generatedMenus),
      menuItemRoot: path.relative(repoRoot, input.menuItemRoot),
      contextMenuRoot: path.relative(repoRoot, input.contextMenuRoot),
      english: path.relative(repoRoot, input.english),
      sevenSeg: path.relative(repoRoot, input.sevenSeg),
      menuTags: path.relative(repoRoot, input.menuTags),
    },
    trees,
    mapping: {
      ...MENU_TREES,
      ...VIRTUAL_TREES,
    },
  }

  const baselinePayload = (() => {
    try {
      const gitShow = execSync(
        `git --no-pager show ${BASELINE_MENU_HIERARCHY_COMMIT}:${path.relative(repoRoot, outputPath)}`,
        {
          cwd: repoRoot,
          encoding: "utf8",
        },
      )
      return JSON.parse(gitShow)
    } catch {
      return null
    }
  })()

  const previousText = fs.existsSync(outputPath)
    ? fs.readFileSync(outputPath, "utf8")
    : null

  if (
    baselinePayload &&
    menuHierarchyMatchesBaseline(payload, baselinePayload)
  ) {
    if (previousText) {
      try {
        const previousPayload = JSON.parse(previousText)
        if (!generatedPayloadsMatch(payload, previousPayload)) {
          const finalPayload = {
            ...payload,
            generatedAt: new Date().toISOString(),
          }

          fs.mkdirSync(path.dirname(outputPath), { recursive: true })
          fs.writeFileSync(
            outputPath,
            `${JSON.stringify(finalPayload, null, 2)}\n`,
            "utf8",
          )
          console.log(
            `Regenerated stale menu hierarchy to match baseline ${BASELINE_MENU_HIERARCHY_COMMIT} in ${path.relative(repoRoot, outputPath)}.`,
          )
          return
        }
      } catch {
        // fall through and rewrite invalid or stale files below
      }
    }

    console.log(
      `No menu hierarchy changes detected against baseline ${BASELINE_MENU_HIERARCHY_COMMIT}; leaving ${path.relative(repoRoot, outputPath)} unchanged.`,
    )
    return
  }

  if (previousText) {
    try {
      const previousPayload = JSON.parse(previousText)
      if (generatedPayloadsMatch(payload, previousPayload)) {
        console.log(
          `No menu hierarchy changes detected; leaving ${path.relative(repoRoot, outputPath)} unchanged.`,
        )
        return
      }
    } catch {
      // fall through and rewrite invalid or stale files
    }
  }

  const finalPayload = {
    ...payload,
    generatedAt: new Date().toISOString(),
  }

  fs.mkdirSync(path.dirname(outputPath), { recursive: true })
  fs.writeFileSync(
    outputPath,
    `${JSON.stringify(finalPayload, null, 2)}\n`,
    "utf8",
  )
  console.log(
    `Generated ${Object.keys(trees).length} menu trees from firmware source into ${path.relative(repoRoot, outputPath)}.`,
  )
}

function labelFromToken(english, sevenSeg, token) {
  const oled = english[token]
  if (!oled) {
    return null
  }

  return {
    token,
    oled,
    code: sevenSeg[token] ?? oled,
  }
}

function applyVarNameLabelOverrides(varName, label) {
  const replaceWildcardWithNumber = (numberString) => ({
    ...label,
    oled: label.oled.replace("*", numberString),
    code: label.code.replace("*", numberString),
  })

  const envelopeMatch = /^env(\d+)Menu$/.exec(varName)
  if (envelopeMatch) {
    return replaceWildcardWithNumber(envelopeMatch[1])
  }

  const sourceMatch = /^source(\d+)(?:Menu|VolumeMenu)$/.exec(varName)
  if (sourceMatch && label.oled.includes("*")) {
    return replaceWildcardWithNumber(
      String(Number.parseInt(sourceMatch[1], 10) + 1),
    )
  }

  const sampleMatch = /^sample(\d+)RecorderMenu$/.exec(varName)
  if (sampleMatch && label.oled.includes("*")) {
    return replaceWildcardWithNumber(sampleMatch[1])
  }

  const modulatorMatch = /^modulator(\d+)Volume$/.exec(varName)
  if (modulatorMatch && label.oled.includes("*")) {
    return replaceWildcardWithNumber(
      String(Number.parseInt(modulatorMatch[1], 10) + 1),
    )
  }

  const midiTrackMatch = /^midiFollowChannelTrack(\d+)Menu$/.exec(varName)
  if (midiTrackMatch) {
    const index = Number.parseInt(midiTrackMatch[1], 10)
    if (Number.isInteger(index) && index >= 1 && index <= 16) {
      const suffix = String(index).padStart(2, "0")
      return {
        ...label,
        oled: `Channel Track${suffix}`,
        code: `TR${suffix}`,
      }
    }
  }

  return label
}

function makeVirtualNode(
  english,
  sevenSeg,
  token,
  varName,
  children = [],
  codeToken = token,
) {
  const label = labelFromToken(english, sevenSeg, token)
  if (!label) {
    return null
  }

  const code = sevenSeg[codeToken] ?? label.code

  return {
    varName,
    token: label.token,
    oled: label.oled,
    code,
    children,
  }
}

function resolveTokenForVar(varName, varToToken, sourceStructure) {
  return (
    varToToken.get(varName) ??
    sourceStructure.runtimeFeatureMenuVarToToken?.get(varName) ??
    null
  )
}

function cloneChildren(children) {
  return JSON.parse(JSON.stringify(children))
}

function escapeForRegex(value) {
  return value.replace(/[.*+?^${}()|[\]\\]/g, "\\$&")
}

function findMatchingBrace(source, openIndex) {
  let depth = 0
  for (let i = openIndex; i < source.length; i++) {
    const c = source[i]
    if (c === "{") {
      depth += 1
    } else if (c === "}") {
      depth -= 1
      if (depth === 0) {
        return i
      }
    }
  }

  return -1
}

function extractMethodBodyMatches(source, signatureRegex) {
  const bodies = []

  for (const match of source.matchAll(signatureRegex)) {
    if (match.index === undefined) {
      continue
    }

    const bodySearchStart = match.index + match[0].length
    const openBraceIndex = source.indexOf("{", bodySearchStart)
    if (openBraceIndex < 0) {
      continue
    }

    const semicolonIndex = source.indexOf(";", bodySearchStart)
    if (semicolonIndex >= 0 && semicolonIndex < openBraceIndex) {
      continue
    }

    const closeBraceIndex = findMatchingBrace(source, openBraceIndex)
    if (closeBraceIndex < 0) {
      continue
    }

    bodies.push({
      body: source.slice(openBraceIndex + 1, closeBraceIndex),
      match,
    })
  }

  return bodies
}

function extractMethodBodies(source, signatureRegex) {
  return extractMethodBodyMatches(source, signatureRegex).map(
    (result) => result.body,
  )
}

function normalizeShortOptBranches(body) {
  const ternaryToFullOption =
    /(?:shortOpt|optType\s*==\s*OptType::SHORT)\s*\?\s*([\s\S]*?)\s*(?<!:):(?!:)\s*([\s\S]*?)(?=[,)\n])/g

  return body.replace(ternaryToFullOption, "$2")
}

function extractSelectionTokensFromBody(body) {
  const tokens = []
  for (const tokenMatch of normalizeShortOptBranches(body).matchAll(
    /\bSTRING_FOR_[A-Z0-9_]+\b/g,
  )) {
    tokens.push(tokenMatch[0])
  }
  return tokens
}

function extractScopedSymbols(sourceText) {
  return unique(
    [
      ...(sourceText ?? "").matchAll(
        /\b(?:[A-Za-z_][A-Za-z0-9_]*::)+[A-Za-z_][A-Za-z0-9_]*\b/g,
      ),
    ].map((match) => match[0]),
  )
}

function scopedSymbolParts(symbol) {
  return symbol.split("::").filter(Boolean)
}

function scopedSymbolEnumName(symbol) {
  const parts = scopedSymbolParts(symbol)
  return parts.length > 1 ? parts.at(-2) : null
}

function scopedSymbolMemberName(symbol) {
  return scopedSymbolParts(symbol).at(-1) ?? ""
}

function scopedSymbolsReferenceSameEnum(symbolA, symbolB) {
  const enumA = scopedSymbolEnumName(symbolA)
  const enumB = scopedSymbolEnumName(symbolB)
  return Boolean(enumA && enumB && enumA === enumB)
}

function scopedSymbolsReferenceSameEnumMember(symbolA, symbolB) {
  return (
    scopedSymbolsReferenceSameEnum(symbolA, symbolB) &&
    scopedSymbolMemberName(symbolA) === scopedSymbolMemberName(symbolB)
  )
}

function extractInitializerDiscriminantSymbols(initializerBody) {
  return extractScopedSymbols(initializerBody).filter(
    (symbol) => !/^l10n::/.test(symbol) && !/^std::/.test(symbol),
  )
}

function extractOptionReturnBody(body) {
  const returnMatch = /return\s*\{([\s\S]*?)\};/.exec(body)
  return returnMatch ? normalizeShortOptBranches(returnMatch[1]) : null
}

function extractAlternativeIfBranchBody(body, ifCloseIndex) {
  const afterIfBody = body.slice(ifCloseIndex + 1)
  const elseMatch = /^\s*else\b/.exec(afterIfBody)
  if (!elseMatch) {
    return afterIfBody
  }

  const elseBodyStart = ifCloseIndex + 1 + elseMatch[0].length
  const openBraceIndex = body.indexOf("{", elseBodyStart)
  const semicolonIndex = body.indexOf(";", elseBodyStart)

  if (
    openBraceIndex >= 0 &&
    (semicolonIndex < 0 || openBraceIndex < semicolonIndex)
  ) {
    const closeBraceIndex = findMatchingBrace(body, openBraceIndex)
    if (closeBraceIndex >= 0) {
      return body.slice(openBraceIndex + 1, closeBraceIndex)
    }
  }

  return semicolonIndex >= 0
    ? body.slice(elseBodyStart, semicolonIndex + 1)
    : body.slice(elseBodyStart)
}

function extractSelectionReturnBodyForInitializerEnum(
  menuItemCorpus,
  typeName,
  initializerBody,
) {
  const initializerSymbols =
    extractInitializerDiscriminantSymbols(initializerBody)
  if (initializerSymbols.length === 0) {
    return null
  }

  for (const body of extractOptionBodiesForType(menuItemCorpus, typeName)) {
    for (const match of body.matchAll(/if\s*\(([\s\S]*?)\)\s*\{/g)) {
      if (match.index === undefined) {
        continue
      }

      const condition = match[1]
      const conditionSymbols = extractScopedSymbols(condition)
      const checksInitializerEnum = conditionSymbols.some((conditionSymbol) =>
        initializerSymbols.some((initializerSymbol) =>
          scopedSymbolsReferenceSameEnum(conditionSymbol, initializerSymbol),
        ),
      )
      if (!checksInitializerEnum) {
        continue
      }

      const matchesInitializerValue = conditionSymbols.some((conditionSymbol) =>
        initializerSymbols.some((initializerSymbol) =>
          scopedSymbolsReferenceSameEnumMember(
            conditionSymbol,
            initializerSymbol,
          ),
        ),
      )
      const negatesCondition = /!=|!\s*\(/.test(condition)
      const useIfBranch = negatesCondition
        ? !matchesInitializerValue
        : matchesInitializerValue

      const ifOpenIndex = body.indexOf("{", match.index)
      if (ifOpenIndex < 0) {
        continue
      }

      const ifCloseIndex = findMatchingBrace(body, ifOpenIndex)
      if (ifCloseIndex < 0) {
        continue
      }

      const selectedBranchBody = useIfBranch
        ? body.slice(ifOpenIndex + 1, ifCloseIndex)
        : extractAlternativeIfBranchBody(body, ifCloseIndex)
      const optionBody = extractOptionReturnBody(selectedBranchBody)
      if (optionBody) {
        return optionBody
      }
    }
  }

  return null
}

function extractOptionBodiesForType(sourceText, typeName) {
  const bodies = []
  const typeCandidates = unique([typeName, simpleTypeName(typeName)]).filter(
    Boolean,
  )

  for (const candidate of typeCandidates) {
    const qualifiedBodies = extractMethodBodies(
      sourceText,
      new RegExp(
        `${escapeForRegex(candidate)}::getOptions\\s*\\([^)]*\\)`,
        "g",
      ),
    )
    bodies.push(...qualifiedBodies)
  }

  const classBody = extractClassBody(sourceText, typeName)
  if (classBody) {
    const unqualifiedBodies = extractMethodBodies(
      classBody,
      /getOptions\s*\([^)]*\)\s*(?:override\s*)?/g,
    )
    bodies.push(...unqualifiedBodies)
  }

  return bodies
}

function extractMenuOptionTokensForType(sourceText, typeName) {
  const tokens = []
  for (const body of extractOptionBodiesForType(sourceText, typeName)) {
    tokens.push(...extractSelectionTokensFromBody(body))
  }

  return unique(tokens)
}

function typeOptionsReferenceFunction(sourceText, typeName, functionName) {
  const callPattern = new RegExp(`\\b${escapeForRegex(functionName)}\\s*\\(`)
  return extractOptionBodiesForType(sourceText, typeName).some((body) =>
    callPattern.test(body),
  )
}

function typeBodyReferences(sourceText, typeName, pattern) {
  const classBody = extractClassBody(sourceText, typeName)
  if (classBody && pattern.test(classBody)) {
    return true
  }

  const typeCandidates = unique([typeName, simpleTypeName(typeName)]).filter(
    Boolean,
  )
  for (const candidate of typeCandidates) {
    const methodBodies = extractMethodBodies(
      sourceText,
      new RegExp(
        `${escapeForRegex(candidate)}::[A-Za-z_][A-Za-z0-9_]*\\s*\\([^)]*\\)`,
        "g",
      ),
    )
    if (methodBodies.some((body) => pattern.test(body))) {
      return true
    }
  }

  return false
}

function extractSelectionLiteralsFromBody(body) {
  const literals = []
  for (const literalMatch of normalizeShortOptBranches(body).matchAll(
    /"([^"\\n]+)"/g,
  )) {
    literals.push(literalMatch[1])
  }
  return literals
}

function extractReturnedSelectionTokensForFunction(
  menuItemCorpus,
  functionName,
) {
  const tokens = []
  const bodies = extractMethodBodies(
    menuItemCorpus,
    new RegExp(`${escapeForRegex(functionName)}\\s*\\([^)]*\\)`, "g"),
  )

  for (const body of bodies) {
    tokens.push(...extractSelectionTokensFromBody(body))
  }

  return unique(tokens)
}

function extractClassBody(source, typeName) {
  const typeParts = typeName.split("::")
  const className = typeParts.at(-1) ?? typeName
  const namespaceHint = typeParts.length > 1 ? typeParts.at(-2) : null

  const candidates = []
  const classPattern = new RegExp(
    `class\\s+${escapeForRegex(className)}\\b`,
    "g",
  )

  for (const match of source.matchAll(classPattern)) {
    if (match.index === undefined) {
      continue
    }

    const openBraceIndex = source.indexOf("{", match.index)
    if (openBraceIndex < 0) {
      continue
    }

    const closeBraceIndex = findMatchingBrace(source, openBraceIndex)
    if (closeBraceIndex < 0) {
      continue
    }

    const prefix = source.slice(Math.max(0, match.index - 1200), match.index)
    const namespaceMatch = namespaceHint
      ? new RegExp(
          `namespace\\s+[^\\n{;]*\\b${escapeForRegex(namespaceHint)}\\b`,
        ).test(prefix)
      : false

    candidates.push({
      body: source.slice(openBraceIndex + 1, closeBraceIndex),
      namespaceMatch,
    })
  }

  const best = candidates.find((candidate) => candidate.namespaceMatch)
  return (best ?? candidates[0])?.body ?? null
}

function simpleTypeName(typeName) {
  return typeName.split("::").at(-1) ?? typeName
}

function extractDirectBaseTypes(source, typeName) {
  const typeParts = typeName.split("::")
  const className = typeParts.at(-1) ?? typeName
  const namespaceHint = typeParts.length > 1 ? typeParts.at(-2) : null

  const candidates = []
  const classPattern = new RegExp(
    `class\\s+${escapeForRegex(className)}\\b`,
    "g",
  )

  for (const match of source.matchAll(classPattern)) {
    if (match.index === undefined) {
      continue
    }

    const openBraceIndex = source.indexOf("{", match.index)
    if (openBraceIndex < 0) {
      continue
    }

    const declaration = source.slice(match.index, openBraceIndex)
    const namespacePrefix = source.slice(
      Math.max(0, match.index - 1200),
      match.index,
    )
    const namespaceMatch = namespaceHint
      ? new RegExp(
          `namespace\\s+[^\\n{;]*\\b${escapeForRegex(namespaceHint)}\\b`,
        ).test(namespacePrefix)
      : false

    const baseTypes = []
    for (const baseMatch of declaration.matchAll(
      /\bpublic\s+([A-Za-z_][A-Za-z0-9_:]*)/g,
    )) {
      baseTypes.push(baseMatch[1])
    }

    candidates.push({
      baseTypes,
      namespaceMatch,
    })
  }

  const best = candidates.find((candidate) => candidate.namespaceMatch)
  return (best ?? candidates[0])?.baseTypes ?? []
}

function typeExtends(
  typeName,
  targetTypeName,
  sourceStructure,
  seen = new Set(),
) {
  if (!typeName) {
    return false
  }

  const cacheKey = `${typeName}->${targetTypeName}`
  const cached = sourceStructure.typeInheritanceCache.get(cacheKey)
  if (cached !== undefined) {
    return cached
  }

  const currentSimple = simpleTypeName(typeName)
  if (currentSimple === targetTypeName) {
    sourceStructure.typeInheritanceCache.set(cacheKey, true)
    return true
  }

  if (seen.has(typeName)) {
    sourceStructure.typeInheritanceCache.set(cacheKey, false)
    return false
  }
  seen.add(typeName)

  const baseTypes = extractDirectBaseTypes(
    sourceStructure.menuItemCorpus,
    typeName,
  )
  for (const baseType of baseTypes) {
    if (simpleTypeName(baseType) === targetTypeName) {
      sourceStructure.typeInheritanceCache.set(cacheKey, true)
      return true
    }
    if (typeExtends(baseType, targetTypeName, sourceStructure, seen)) {
      sourceStructure.typeInheritanceCache.set(cacheKey, true)
      return true
    }
  }

  sourceStructure.typeInheritanceCache.set(cacheKey, false)
  return false
}

function extractSelectionOptionsTokensForType(menuItemCorpus, typeName) {
  const tokens = []

  const qualifiedBodies = extractMethodBodies(
    menuItemCorpus,
    new RegExp(`${escapeForRegex(typeName)}::getOptions\\s*\\([^)]*\\)`, "g"),
  )
  for (const body of qualifiedBodies) {
    tokens.push(...extractSelectionTokensFromBody(body))
  }

  const classBody = extractClassBody(menuItemCorpus, typeName)
  if (classBody) {
    const unqualifiedBodies = extractMethodBodies(
      classBody,
      /getOptions\s*\([^)]*\)\s*(?:override\s*)?/g,
    )
    for (const body of unqualifiedBodies) {
      tokens.push(...extractSelectionTokensFromBody(body))
    }
  }

  return unique(tokens)
}

function extractSelectionOptionLiteralsForType(menuItemCorpus, typeName) {
  const literals = []

  const qualifiedBodies = extractMethodBodies(
    menuItemCorpus,
    new RegExp(`${escapeForRegex(typeName)}::getOptions\\s*\\([^)]*\\)`, "g"),
  )
  for (const body of qualifiedBodies) {
    literals.push(...extractSelectionLiteralsFromBody(body))
  }

  const classBody = extractClassBody(menuItemCorpus, typeName)
  if (classBody) {
    const unqualifiedBodies = extractMethodBodies(
      classBody,
      /getOptions\s*\([^)]*\)\s*(?:override\s*)?/g,
    )
    for (const body of unqualifiedBodies) {
      literals.push(...extractSelectionLiteralsFromBody(body))
    }
  }

  return unique(literals).filter(Boolean)
}

function extractDisplayConditionalSingleOptionPairForType(
  menuItemCorpus,
  typeName,
) {
  const bodies = []

  const typeCandidates = unique([typeName, simpleTypeName(typeName)]).filter(
    Boolean,
  )

  for (const candidate of typeCandidates) {
    const qualifiedBodies = extractMethodBodies(
      menuItemCorpus,
      new RegExp(
        `${escapeForRegex(candidate)}::getOptions\\s*\\([^)]*\\)`,
        "g",
      ),
    )
    bodies.push(...qualifiedBodies)
  }

  const classBody = extractClassBody(menuItemCorpus, typeName)
  if (classBody) {
    const unqualifiedBodies = extractMethodBodies(
      classBody,
      /getOptions\s*\([^)]*\)\s*(?:override\s*)?/g,
    )
    bodies.push(...unqualifiedBodies)
  }

  for (const body of bodies) {
    if (!/display->haveOLED\s*\(\s*\)/.test(body)) {
      continue
    }

    // Heuristic: display-conditional selectors with one option on each branch
    // should have exactly two option tokens total and at least two returns of
    // the `{..., 1}` form.
    const tokens = extractSelectionTokensFromBody(body)
    if (tokens.length !== 2) {
      continue
    }

    const singleOptionReturns =
      body.match(/return\s*\{[^}]*,\s*1\s*\}\s*;/g)?.length ?? 0
    if (singleOptionReturns < 2) {
      continue
    }

    return {
      oledToken: tokens[0],
      sevenSegToken: tokens[1],
    }
  }

  return null
}

function buildImplicitOptionChildren(
  varName,
  english,
  sevenSeg,
  sourceStructure,
) {
  const selectorChildren = buildSelectorOptionChildren(
    varName,
    english,
    sevenSeg,
    sourceStructure,
    0,
  )
  if (selectorChildren.length > 0) {
    return selectorChildren
  }

  const typeName = sourceStructure.varToType.get(varName) ?? ""
  if (!typeExtends(typeName, "Toggle", sourceStructure)) {
    return []
  }

  return [
    makeVirtualNode(
      english,
      sevenSeg,
      "STRING_FOR_DISABLED",
      `virtualToggle_${varName}_0`,
    ),
    makeVirtualNode(
      english,
      sevenSeg,
      "STRING_FOR_ENABLED",
      `virtualToggle_${varName}_1`,
    ),
  ].filter(Boolean)
}

function extractSelectButtonTargetForType(menuItemCorpus, typeName) {
  const typeCandidates = unique([typeName, simpleTypeName(typeName)]).filter(
    Boolean,
  )

  for (const candidate of typeCandidates) {
    const pattern = new RegExp(
      `${escapeForRegex(candidate)}::selectButtonPress\\s*\\([^)]*\\)\\s*\\{[\\s\\S]*?return\\s*&\\s*([A-Za-z_][A-Za-z0-9_:]*)\\s*;`,
      "g",
    )
    const match = pattern.exec(menuItemCorpus)
    if (match) {
      return stripNamespace(match[1])
    }

    const qualifiedBodies = extractMethodBodies(
      menuItemCorpus,
      new RegExp(
        `${escapeForRegex(candidate)}::selectButtonPress\\s*\\([^)]*\\)`,
        "g",
      ),
    )
    for (const body of qualifiedBodies) {
      const openUiMatch =
        /openUI\s*\(\s*&\s*([A-Za-z_][A-Za-z0-9_:]*)\s*\)/.exec(body)
      if (openUiMatch) {
        return stripNamespace(openUiMatch[1])
      }
    }
  }

  const classBody = extractClassBody(menuItemCorpus, typeName)
  if (classBody) {
    const unqualifiedBodies = extractMethodBodies(
      classBody,
      /selectButtonPress\s*\([^)]*\)\s*(?:override\s*)?/g,
    )
    for (const body of unqualifiedBodies) {
      const match = /return\s*&\s*([A-Za-z_][A-Za-z0-9_:]*)\s*;/.exec(body)
      if (match) {
        return stripNamespace(match[1])
      }

      const openUiMatch =
        /openUI\s*\(\s*&\s*([A-Za-z_][A-Za-z0-9_:]*)\s*\)/.exec(body)
      if (openUiMatch) {
        return stripNamespace(openUiMatch[1])
      }
    }
  }

  return null
}

function extractSelectButtonBodyMatchesForType(menuItemCorpus, typeName) {
  const bodies = []
  const typeCandidates = unique([typeName, simpleTypeName(typeName)]).filter(
    Boolean,
  )

  for (const candidate of typeCandidates) {
    bodies.push(
      ...extractMethodBodyMatches(
        menuItemCorpus,
        new RegExp(
          `${escapeForRegex(candidate)}::selectButtonPress\\s*\\([^)]*\\)`,
          "g",
        ),
      ),
    )
  }

  const classBody = extractClassBody(menuItemCorpus, typeName)
  if (classBody) {
    bodies.push(
      ...extractMethodBodyMatches(
        classBody,
        /selectButtonPress\s*\([^)]*\)\s*(?:override\s*)?/g,
      ),
    )
  }

  return bodies
}

function extractSelectButtonBodiesForType(menuItemCorpus, typeName) {
  return extractSelectButtonBodyMatchesForType(menuItemCorpus, typeName).map(
    (result) => result.body,
  )
}

function extractReturnStatements(body) {
  return [
    ...body.matchAll(
      /return\s+(?:&\s*([A-Za-z_][A-Za-z0-9_:]*)|(nullptr))\s*;/g,
    ),
  ].map((match) => ({
    index: match.index ?? 0,
    target: match[1] ? stripNamespace(match[1]) : null,
  }))
}

function evaluateSelectorOptionExpression(expression, sourceStructure) {
  const valueMap = sourceStructure?.numericDefines ?? new Map()
  return evaluateIntegerExpression(expression.trim(), valueMap)
}

function extractValueConditionBranches(body) {
  const branches = []
  const conditionPattern =
    /if\s*\(\s*this->getValue\s*\(\s*\)\s*(==|!=)\s*([A-Za-z_][A-Za-z0-9_:]*|\d+)\s*\)\s*\{/g

  for (const match of body.matchAll(conditionPattern)) {
    if (match.index === undefined) {
      continue
    }

    const openBraceIndex = body.indexOf("{", match.index)
    if (openBraceIndex < 0) {
      continue
    }

    const closeBraceIndex = findMatchingBrace(body, openBraceIndex)
    if (closeBraceIndex < 0) {
      continue
    }

    const branchBody = body.slice(openBraceIndex + 1, closeBraceIndex)
    const returnStatement = extractReturnStatements(branchBody)[0]
    if (!returnStatement) {
      continue
    }

    branches.push({
      operator: match[1],
      expression: match[2],
      target: returnStatement.target,
    })
  }

  return branches
}

function hasUnknownNullGuard(body, sourceStructure) {
  const firstTargetReturn = extractReturnStatements(body).find(
    (statement) => statement.target,
  )
  if (!firstTargetReturn) {
    return false
  }

  for (const statement of extractReturnStatements(body)) {
    if (statement.index >= firstTargetReturn.index) {
      continue
    }
    if (statement.target !== null) {
      continue
    }

    const prefix = body.slice(0, statement.index)
    const guardMatch =
      /if\s*\(([\s\S]*?)\)\s*\{?\s*$/.exec(prefix) ??
      /if\s*\(([\s\S]*?)\)[\s\S]*$/.exec(prefix)
    const condition = guardMatch?.[1] ?? ""
    if (!/this->getValue\s*\(\s*\)/.test(condition)) {
      return true
    }

    const valueMatch =
      /this->getValue\s*\(\s*\)\s*(==|!=)\s*([A-Za-z_][A-Za-z0-9_:]*|\d+)/.exec(
        condition,
      )
    if (
      !valueMatch ||
      evaluateSelectorOptionExpression(valueMatch[2], sourceStructure) === null
    ) {
      return true
    }
  }

  return false
}

function normalizeOptionIdentity(value) {
  return stripNamespace(value)
    .replace(/^STRING_FOR_/, "")
    .replace(/[^A-Za-z0-9]/g, "")
    .toUpperCase()
}

function optionIdentityAtIndex(optionIndex, optionTokens, optionLiterals) {
  if (optionIndex < optionTokens.length) {
    return normalizeOptionIdentity(optionTokens[optionIndex])
  }

  const literal = optionLiterals[optionIndex - optionTokens.length]
  return literal ? normalizeOptionIdentity(literal) : ""
}

function resolveSymbolGuardedTarget(
  body,
  defaultTarget,
  optionIndex,
  optionTokens,
  optionLiterals,
) {
  const optionIdentity = optionIdentityAtIndex(
    optionIndex,
    optionTokens,
    optionLiterals,
  )
  if (!optionIdentity) {
    return undefined
  }

  for (const match of body.matchAll(
    /if\s*\(\s*[^{}\n;]+?\s*(==|!=)\s*([A-Za-z_][A-Za-z0-9_:]*)\s*\)\s*\{[\s\S]*?return\s+nullptr\s*;/g,
  )) {
    const operator = match[1]
    const symbolIdentity = normalizeOptionIdentity(match[2])
    if (!symbolIdentity) {
      continue
    }

    const optionMatches =
      optionIdentity === symbolIdentity ||
      optionIdentity.endsWith(symbolIdentity) ||
      symbolIdentity.endsWith(optionIdentity)
    if (operator === "!=") {
      return optionMatches ? defaultTarget : null
    }
    return optionMatches ? null : defaultTarget
  }

  return undefined
}

function extractSelectButtonTargetForOptionIndex(
  menuItemCorpus,
  typeName,
  optionIndex,
  optionCount,
  sourceStructure,
  optionTokens = [],
  optionLiterals = [],
) {
  for (const body of extractSelectButtonBodiesForType(
    menuItemCorpus,
    typeName,
  )) {
    const returnStatements = extractReturnStatements(body)
    const defaultTarget = returnStatements.findLast(
      (statement) => statement.target,
    )?.target
    if (!defaultTarget) {
      continue
    }

    const branches = extractValueConditionBranches(body)
    for (const branch of branches) {
      const branchValue = evaluateSelectorOptionExpression(
        branch.expression,
        sourceStructure,
      )

      if (branchValue === null) {
        continue
      }

      const conditionMatches =
        branch.operator === "=="
          ? optionIndex === branchValue
          : optionIndex !== branchValue
      if (conditionMatches) {
        return branch.target
      }
    }

    const unresolvedEqualityBranches = branches.filter(
      (branch) =>
        branch.operator === "==" &&
        evaluateSelectorOptionExpression(branch.expression, sourceStructure) ===
          null,
    )
    if (
      unresolvedEqualityBranches.length === 1 &&
      optionIndex === optionCount - 1
    ) {
      return unresolvedEqualityBranches[0].target
    }

    const symbolGuardedTarget = resolveSymbolGuardedTarget(
      body,
      defaultTarget,
      optionIndex,
      optionTokens,
      optionLiterals,
    )
    if (symbolGuardedTarget !== undefined) {
      return symbolGuardedTarget
    }

    if (hasUnknownNullGuard(body, sourceStructure)) {
      continue
    }

    return defaultTarget
  }

  return null
}

function resolveVarTypeFromCorpus(menuItemCorpus, varName) {
  const pattern = new RegExp(
    `(?:^|\\n)\\s*(?:extern\\s+)?([A-Za-z_][A-Za-z0-9_:\\s<>*&]*?)\\s+${escapeForRegex(varName)}\\s*\\{`,
    "m",
  )
  const match = pattern.exec(menuItemCorpus)
  if (!match) {
    const externPattern = new RegExp(
      `(?:^|\\n)\\s*extern\\s+([A-Za-z_][A-Za-z0-9_:\\s<>*&]*?)\\s+${escapeForRegex(varName)}\\s*;`,
      "m",
    )
    const externMatch = externPattern.exec(menuItemCorpus)
    if (!externMatch) {
      return null
    }
    return normalizeTypeName(externMatch[1])
  }
  return normalizeTypeName(match[1])
}

function extractSizeReturnForType(menuItemCorpus, typeName) {
  const typeCandidates = unique([typeName, simpleTypeName(typeName)]).filter(
    Boolean,
  )

  for (const candidate of typeCandidates) {
    const qualifiedMatch = new RegExp(
      `${escapeForRegex(candidate)}::size\\s*\\([^)]*\\)\\s*(?:const\\s*)?(?:override\\s*)?\\{[\\s\\S]*?return\\s+([A-Za-z_][A-Za-z0-9_]*|\\d+)\\s*;`,
      "m",
    ).exec(menuItemCorpus)
    if (qualifiedMatch) {
      return qualifiedMatch[1]
    }
  }

  const classBody = extractClassBody(menuItemCorpus, typeName)
  if (!classBody) {
    return null
  }

  const unqualifiedMatch =
    /size\s*\([^)]*\)\s*(?:const\s*)?(?:override\s*)?\{[\s\S]*?return\s+([A-Za-z_][A-Za-z0-9_]*|\d+)\s*;/.exec(
      classBody,
    )
  return unqualifiedMatch ? unqualifiedMatch[1] : null
}

function resolveTypeSize(typeName, sourceStructure, seen = new Set()) {
  if (!typeName || seen.has(typeName)) {
    return null
  }
  seen.add(typeName)

  const sizeExpr = extractSizeReturnForType(
    sourceStructure.menuItemCorpus,
    typeName,
  )
  if (sizeExpr) {
    if (/^\d+$/.test(sizeExpr)) {
      return Number.parseInt(sizeExpr, 10)
    }
    const definedValue = sourceStructure.numericDefines.get(sizeExpr)
    if (definedValue !== undefined) {
      return definedValue
    }
  }

  const baseTypes = extractDirectBaseTypes(
    sourceStructure.menuItemCorpus,
    typeName,
  )
  for (const baseType of baseTypes) {
    const resolved = resolveTypeSize(baseType, sourceStructure, seen)
    if (resolved !== null) {
      return resolved
    }
  }

  return null
}

function buildSyncLevelChildren(varName, sourceStructure) {
  if (!sourceStructure.syncOptionContext) {
    return []
  }

  const typeName = sourceStructure.varToType.get(varName) ?? ""
  const size = resolveTypeSize(typeName, sourceStructure)
  const maxCount = size ?? 0

  const children = []
  for (let option = 0; option < Math.max(0, maxCount); option += 1) {
    const label =
      option === 0
        ? sourceStructure.syncOffLabel
          ? {
              oled: sourceStructure.syncOffLabel.oled,
              code: sourceStructure.syncOffLabel.code,
              token: sourceStructure.syncOffLabel.token,
            }
          : buildSyncLabelForOption(option, sourceStructure.syncOptionContext)
        : buildSyncLabelForOption(option, sourceStructure.syncOptionContext)

    if (!label) {
      continue
    }

    children.push({
      varName: `virtualSync_${varName}_${option}`,
      token: label.token ?? `LITERAL_${varName}_${option}`,
      oled: label.oled,
      code: label.code,
      children: [],
    })
  }

  return children
}

function buildIteranceChildren(varName, customVarName, sourceStructure) {
  const customPreset = sourceStructure.iteranceCustomPreset
  if (customPreset === undefined) {
    return []
  }

  const customRoot = buildNode(
    sourceStructure.combined,
    sourceStructure.arrayChildren,
    sourceStructure.varToToken,
    sourceStructure.english,
    sourceStructure.sevenSeg,
    sourceStructure,
    customVarName,
    [varName],
    1,
  )

  return sourceStructure.iteranceOptionLabels.map((label, index) => {
    const presetIndex = index
    const isCustom = presetIndex === customPreset
    return {
      varName: `virtualIterance_${varName}_${presetIndex}`,
      token: `LITERAL_${varName}_${presetIndex}`,
      oled: label.oled,
      code: label.code,
      children: isCustom ? (customRoot?.children ?? []) : [],
    }
  })
}

function extractTargetValueUpdateMethod(
  menuItemCorpus,
  selectorType,
  targetVar,
) {
  const targetPattern = escapeForRegex(targetVar)
  const updateCallPattern = new RegExp(
    `\\b${targetPattern}\\s*\\.\\s*([A-Za-z_][A-Za-z0-9_]*)\\s*\\(\\s*this->getValue\\s*\\(\\s*\\)\\s*\\)`,
  )

  for (const body of extractSelectButtonBodiesForType(
    menuItemCorpus,
    selectorType,
  )) {
    const match = updateCallPattern.exec(body)
    if (match) {
      return match[1]
    }
  }

  return null
}

function extractTokenVectorInitializers(classBody) {
  const vectors = new Map()
  for (const match of classBody.matchAll(
    /(?:std|deluge)::vector\s*<\s*l10n::String\s*>\s+([A-Za-z_][A-Za-z0-9_]*)\s*=\s*\{([\s\S]*?)\}\s*;/g,
  )) {
    vectors.set(match[1], extractSelectionTokensFromBody(match[2]))
  }
  return vectors
}

function extractCaseBodies(methodBody) {
  const cases = []
  const caseMatches = [...methodBody.matchAll(/case\s+([^:]+):/g)]

  for (let index = 0; index < caseMatches.length; index += 1) {
    const match = caseMatches[index]
    const nextMatch = caseMatches[index + 1]
    const defaultIndex = methodBody.indexOf("default:", match.index)
    const nextCaseIndex = nextMatch?.index ?? methodBody.length
    const endIndex =
      defaultIndex >= 0 && defaultIndex > match.index
        ? Math.min(defaultIndex, nextCaseIndex)
        : nextCaseIndex

    cases.push({
      expression: match[1].trim(),
      body: methodBody.slice((match.index ?? 0) + match[0].length, endIndex),
    })
  }

  return cases
}

function extractVectorPushTokens(body, vectorName) {
  const tokens = []
  const pushPattern = new RegExp(
    `${escapeForRegex(vectorName)}\\s*\\.\\s*push_back\\s*\\(\\s*(?:l10n::String::)?(STRING_FOR_[A-Z0-9_]+)\\s*\\)`,
    "g",
  )

  for (const match of body.matchAll(pushPattern)) {
    tokens.push(match[1])
  }

  return tokens
}

function extractMethodBodiesForType(menuItemCorpus, typeName, methodName) {
  const bodies = []
  const typeCandidates = unique([typeName, simpleTypeName(typeName)]).filter(
    Boolean,
  )

  for (const candidate of typeCandidates) {
    bodies.push(
      ...extractMethodBodies(
        menuItemCorpus,
        new RegExp(
          `${escapeForRegex(candidate)}::${escapeForRegex(methodName)}\\s*\\([^)]*\\)`,
          "g",
        ),
      ),
    )
  }

  const classBody = extractClassBody(menuItemCorpus, typeName)
  if (classBody) {
    bodies.push(
      ...extractMethodBodies(
        classBody,
        new RegExp(
          `${escapeForRegex(methodName)}\\s*\\([^)]*\\)\\s*(?:override\\s*)?`,
          "g",
        ),
      ),
    )
  }

  return bodies
}

function buildUpdatedTargetOptionChildren(
  targetVar,
  selectorType,
  optionIndex,
  english,
  sevenSeg,
  sourceStructure,
) {
  const updateMethod = extractTargetValueUpdateMethod(
    sourceStructure.menuItemCorpus,
    selectorType,
    targetVar,
  )
  if (!updateMethod) {
    return null
  }

  const targetType =
    sourceStructure.varToType.get(targetVar) ??
    resolveVarTypeFromCorpus(sourceStructure.menuItemCorpus, targetVar)
  if (!targetType) {
    return null
  }

  const classBody = extractClassBody(sourceStructure.menuItemCorpus, targetType)
  if (!classBody) {
    return null
  }

  const tokenVectors = extractTokenVectorInitializers(classBody)
  if (tokenVectors.size === 0) {
    return null
  }

  const updateBodies = extractMethodBodiesForType(
    sourceStructure.menuItemCorpus,
    targetType,
    updateMethod,
  )
  if (updateBodies.length === 0) {
    return null
  }

  for (const [vectorName, baseTokens] of tokenVectors) {
    const pushedTokens = []
    for (const updateBody of updateBodies) {
      for (const caseBody of extractCaseBodies(updateBody)) {
        const caseValue = evaluateIntegerExpression(
          caseBody.expression,
          sourceStructure.numericDefines,
        )
        if (caseValue !== optionIndex) {
          continue
        }

        pushedTokens.push(...extractVectorPushTokens(caseBody.body, vectorName))
      }
    }

    const tokens = unique([...baseTokens, ...pushedTokens])
    if (tokens.length === 0) {
      continue
    }

    return tokens
      .map((token, childIndex) =>
        makeVirtualNode(
          english,
          sevenSeg,
          token,
          `virtualUpdatedOption_${targetVar}_${optionIndex}_${childIndex}`,
          [],
        ),
      )
      .filter(Boolean)
  }

  return null
}

function expressionEqualsResolvedValue(expression, valueMap, expectedValue) {
  return (
    evaluateIntegerExpression(expression.trim(), valueMap) === expectedValue
  )
}

function bodyChecksMenuValueAgainstPreset(body, expectedValue, valueMap) {
  const valueVars = new Set()
  for (const match of body.matchAll(
    /\b[A-Za-z_][A-Za-z0-9_:<>\s*&]*\s+([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(?:this->)?getValue(?:<[^>]+>)?\s*\(\s*\)\s*;/g,
  )) {
    valueVars.add(match[1])
  }

  for (const match of body.matchAll(
    /(?:this->)?getValue(?:<[^>]+>)?\s*\(\s*\)\s*==\s*([^\n{;&|)]+)/g,
  )) {
    if (expressionEqualsResolvedValue(match[1], valueMap, expectedValue)) {
      return true
    }
  }

  for (const valueVar of valueVars) {
    const valueVarPattern = new RegExp(
      `\\b${escapeForRegex(valueVar)}\\b\\s*==\\s*([^\\n{;&|)]+)`,
      "g",
    )
    for (const match of body.matchAll(valueVarPattern)) {
      if (expressionEqualsResolvedValue(match[1], valueMap, expectedValue)) {
        return true
      }
    }
  }

  return false
}

function extractCustomIteranceTargetForType(
  menuItemCorpus,
  typeName,
  customPreset,
  conditionValues,
) {
  for (const body of extractSelectButtonBodiesForType(
    menuItemCorpus,
    typeName,
  )) {
    if (
      !bodyChecksMenuValueAgainstPreset(body, customPreset, conditionValues)
    ) {
      continue
    }

    const targetRef = extractRawReturnTargetRefs(body)[0]
    if (targetRef) {
      return stripNamespace(targetRef)
    }
  }

  return null
}

function humanizeIdentifier(identifier) {
  return simpleTypeName(identifier)
    .replace(/([a-z0-9])([A-Z])/g, "$1 $2")
    .replace(/_/g, " ")
    .trim()
}

function singularizeLabel(label) {
  if (/ies$/i.test(label)) {
    return label.replace(/ies$/i, "y")
  }
  if (/s$/i.test(label) && !/ss$/i.test(label)) {
    return label.replace(/s$/i, "")
  }
  return label
}

function pascalCaseLabel(label) {
  return (
    label
      .match(/[A-Za-z0-9]+/g)
      ?.map((word) => `${word[0].toUpperCase()}${word.slice(1)}`)
      .join("") ?? ""
  )
}

function virtualCodeForLabel(label) {
  const subject = /\bPatch Cable\b/i.test(label)
    ? "Patch"
    : (label.match(/[A-Za-z0-9]+/)?.[0] ?? label)
  const upper = subject.toUpperCase()

  if (/^REG/.test(upper)) {
    return "REG"
  }

  if (/^STR/.test(upper)) {
    return "STR"
  }

  if (upper === "PATCH") {
    return "PATCH"
  }

  if (upper.length <= 3) {
    return upper
  }

  const consonantCode = `${upper[0]}${upper.slice(1).replace(/[AEIOU]/g, "")}`
  return consonantCode.length >= 3
    ? consonantCode.slice(0, 4)
    : upper.slice(0, 4)
}

function extractRawReturnTargetRefs(body) {
  return [...body.matchAll(/return\s*&\s*([A-Za-z_][A-Za-z0-9_:]*)\s*;/g)].map(
    (match) => match[1],
  )
}

function resolveVarTypeFromCorpusWithNamespace(menuItemCorpus, varRef) {
  const varName = stripNamespace(varRef)
  const namespaceHint = varRef.includes("::")
    ? varRef.split("::").slice(0, -1).at(-1)
    : null
  const declarations = []
  const pattern = new RegExp(
    `(?:^|\\n)\\s*(?:extern\\s+)?([A-Za-z_][A-Za-z0-9_:\\s<>*&]*?)\\s+${escapeForRegex(varName)}\\s*(?:\\{|;)`,
    "gm",
  )

  for (const match of menuItemCorpus.matchAll(pattern)) {
    if (match.index === undefined) {
      continue
    }

    const prefix = menuItemCorpus.slice(
      Math.max(0, match.index - 1200),
      match.index,
    )
    const namespaceMatch = namespaceHint
      ? new RegExp(
          `namespace\\s+[^\\n{;]*\\b${escapeForRegex(namespaceHint)}\\b`,
        ).test(prefix)
      : false

    declarations.push({
      typeName: normalizeTypeName(match[1]),
      namespaceMatch,
    })
  }

  const best = declarations.find((declaration) => declaration.namespaceMatch)
  const typeName = (best ?? declarations[0])?.typeName ?? null
  if (typeName && namespaceHint && !typeName.includes("::")) {
    return `${namespaceHint}::${typeName}`
  }
  return typeName
}

function splitInitializerArgs(args) {
  const parts = []
  let depth = 0
  let start = 0

  for (let index = 0; index < args.length; index += 1) {
    const c = args[index]
    if (c === "(" || c === "{" || c === "<") {
      depth += 1
    } else if (c === ")" || c === "}" || c === ">") {
      depth -= 1
    } else if (c === "," && depth === 0) {
      parts.push(args.slice(start, index).trim())
      start = index + 1
    }
  }

  parts.push(args.slice(start).trim())
  return parts
}

function extractConstructorSubmenuArrayName(menuItemCorpus, typeName) {
  const typeCandidates = unique([typeName, simpleTypeName(typeName)]).filter(
    Boolean,
  )
  const constructorName = simpleTypeName(typeName)

  for (const candidate of typeCandidates) {
    const pattern = new RegExp(
      `${escapeForRegex(candidate)}::${escapeForRegex(constructorName)}\\s*\\([^)]*\\)\\s*:\\s*[^\\{;]*\\bSubmenu\\s*\\(([^)]*)\\)`,
      "g",
    )

    for (const match of menuItemCorpus.matchAll(pattern)) {
      const args = splitInitializerArgs(match[1])
      const arrayName = args.at(-1)
      if (/^[A-Za-z_][A-Za-z0-9_]*$/.test(arrayName ?? "")) {
        return arrayName
      }
    }
  }

  return null
}

function buildConstructorSubmenuChildren(
  varName,
  typeName,
  english,
  sevenSeg,
  sourceStructure,
) {
  const arrayName = extractConstructorSubmenuArrayName(
    sourceStructure.menuItemCorpus,
    typeName,
  )
  if (!arrayName) {
    return null
  }

  const childVars = sourceStructure.arrayChildren.get(arrayName) ?? []
  if (childVars.length === 0) {
    return null
  }

  return childVars
    .map((childVar) => {
      const runtimeToken =
        sourceStructure.runtimeFeatureMenuVarToToken?.get(childVar)
      if (runtimeToken) {
        return makeVirtualNode(english, sevenSeg, runtimeToken, childVar, [])
      }

      return buildNode(
        sourceStructure.combined,
        sourceStructure.arrayChildren,
        sourceStructure.varToToken,
        english,
        sevenSeg,
        sourceStructure,
        childVar,
        [varName],
        1,
      )
    })
    .filter(Boolean)
}

function buildRepresentativeTargetChildren(
  varName,
  typeName,
  english,
  sevenSeg,
  sourceStructure,
) {
  if (
    extractChildren(
      sourceStructure.combined,
      sourceStructure.arrayChildren,
      varName,
    ).length > 0
  ) {
    return null
  }

  if (
    extractSelectionOptionsTokensForType(
      sourceStructure.menuItemCorpus,
      typeName,
    ).length > 0 ||
    extractSelectionOptionLiteralsForType(
      sourceStructure.menuItemCorpus,
      typeName,
    ).length > 0
  ) {
    return null
  }

  const targetVar = extractSelectButtonTargetForType(
    sourceStructure.menuItemCorpus,
    typeName,
  )
  if (!targetVar) {
    return null
  }

  const templateChildVars = extractChildren(
    sourceStructure.combined,
    sourceStructure.arrayChildren,
    targetVar,
  )
  if (templateChildVars.length === 0) {
    return null
  }

  const label = labelFromToken(
    english,
    sevenSeg,
    resolveTokenForVar(varName, sourceStructure.varToToken, sourceStructure),
  )
  if (!label) {
    return null
  }

  const templateChildren = templateChildVars
    .map((childVar) => {
      const selectorOptions = buildSelectorOptionChildren(
        childVar,
        english,
        sevenSeg,
        sourceStructure,
        0,
      )
      if (selectorOptions.length > 0) {
        const childLabel = labelFromToken(
          english,
          sevenSeg,
          resolveTokenForVar(
            childVar,
            sourceStructure.varToToken,
            sourceStructure,
          ),
        )
        if (!childLabel) {
          return null
        }
        return {
          varName: childVar,
          token: childLabel.token,
          oled: childLabel.oled,
          code: childLabel.code,
          children: selectorOptions,
        }
      }

      return buildNode(
        sourceStructure.combined,
        sourceStructure.arrayChildren,
        sourceStructure.varToToken,
        english,
        sevenSeg,
        sourceStructure,
        childVar,
        [varName, targetVar],
        1,
      )
    })
    .filter(Boolean)

  if (templateChildren.length === 0) {
    return null
  }

  return [
    {
      varName: `virtual${pascalCaseLabel(singularizeLabel(label.oled))}Representative`,
      token: label.token,
      oled: singularizeLabel(label.oled),
      code: label.code,
      children: cloneChildren(templateChildren),
    },
  ]
}

function extractQualifiedSelectButtonBodyMatchesForType(
  menuItemCorpus,
  typeName,
) {
  const bodies = []
  const typeCandidates = unique([typeName, simpleTypeName(typeName)]).filter(
    Boolean,
  )

  for (const candidate of typeCandidates) {
    bodies.push(
      ...extractMethodBodyMatches(
        menuItemCorpus,
        new RegExp(
          `${escapeForRegex(candidate)}::selectButtonPress\\s*\\([^)]*\\)`,
          "g",
        ),
      ),
    )
  }

  return bodies
}

function extractReferencedCurrentValueItemTable(
  sourceText,
  selectButtonBodyMatch,
) {
  const { body, match: methodMatch } = selectButtonBodyMatch
  if (methodMatch.index === undefined) {
    return null
  }

  const itemAccessMatch =
    /\b([A-Za-z_][A-Za-z0-9_]*)\s*\[\s*currentValue\s*\]\s*\.\s*index\b/.exec(
      body,
    )
  if (!itemAccessMatch) {
    return null
  }

  const tableName = itemAccessMatch[1]
  const tablePattern = new RegExp(
    `\\b${escapeForRegex(tableName)}\\s*\\[\\]\\s*=\\s*\\{`,
    "g",
  )
  let tableMatch = null
  for (const match of sourceText
    .slice(0, methodMatch.index)
    .matchAll(tablePattern)) {
    tableMatch = match
  }
  if (!tableMatch || tableMatch.index === undefined) {
    return null
  }

  const openBraceIndex = sourceText.indexOf("{", tableMatch.index)
  if (openBraceIndex < 0) {
    return null
  }

  const closeBraceIndex = findMatchingBrace(sourceText, openBraceIndex)
  if (closeBraceIndex < 0) {
    return null
  }

  return {
    name: tableName,
    body: sourceText.slice(openBraceIndex + 1, closeBraceIndex),
  }
}

function extractCurrentValueItemTableEntries(tableBody, sourceStructure) {
  const entries = []

  for (const match of tableBody.matchAll(
    /\{\s*"([^"]+)"\s*,\s*"([^"]+)"\s*,\s*([^{}]+?)\s*\}/g,
  )) {
    const indexValue = evaluateSelectorOptionExpression(
      match[3],
      sourceStructure,
    )
    if (indexValue === null) {
      continue
    }

    entries.push({
      name: match[1],
      shortName: match[2],
      indexValue,
    })
  }

  return entries
}

function cloneStateByVar(stateByVar) {
  return new Map(
    [...stateByVar].map(([varName, state]) => [varName, new Map(state)]),
  )
}

function stateForVar(stateByVar, varName) {
  return stateByVar.get(varName) ?? new Map()
}

function buildExpressionValueMap(
  sourceStructure,
  stateByVar,
  currentVarName,
  itemIndexVariable,
  itemIndexValue,
) {
  const values = new Map(sourceStructure.numericDefines)
  values.set(itemIndexVariable, itemIndexValue)

  for (const [fieldName, value] of stateForVar(stateByVar, currentVarName)) {
    values.set(fieldName, value)
  }

  return values
}

function applyCurrentValueItemAssignments(body, stateByVar, expressionValues) {
  const nextState = cloneStateByVar(stateByVar)

  for (const match of body.matchAll(
    /\b([A-Za-z_][A-Za-z0-9_:]*)\s*\.\s*([A-Za-z_][A-Za-z0-9_]*)\s*=\s*([^;]+);/g,
  )) {
    const value = evaluateIntegerExpression(match[3], expressionValues)
    if (value === null) {
      continue
    }

    const targetVar = stripNamespace(match[1])
    const targetState = new Map(nextState.get(targetVar) ?? [])
    targetState.set(match[2], value)
    nextState.set(targetVar, targetState)
  }

  return nextState
}

function resolveCurrentValueItemTargetInfo(
  currentVarName,
  selectButtonBody,
  tableName,
  itemIndexValue,
  sourceStructure,
  stateByVar,
) {
  const itemIndexVariableMatch = new RegExp(
    `\\b(?:const\\s+)?(?:auto|int|int32_t)\\s+([A-Za-z_][A-Za-z0-9_]*)\\s*=\\s*${escapeForRegex(tableName)}\\s*\\[\\s*currentValue\\s*\\]\\s*\\.\\s*index\\s*;`,
  ).exec(selectButtonBody)
  const itemIndexVariable = itemIndexVariableMatch?.[1]

  if (!itemIndexVariable) {
    const targetReturns = extractReturnStatements(selectButtonBody).filter(
      (statement) => statement.target,
    )
    if (targetReturns.length !== 1) {
      return null
    }
    return {
      target: targetReturns[0].target,
      stateByVar: applyCurrentValueItemAssignments(
        selectButtonBody,
        stateByVar,
        new Map(sourceStructure.numericDefines),
      ),
    }
  }

  const expressionValues = buildExpressionValueMap(
    sourceStructure,
    stateByVar,
    currentVarName,
    itemIndexVariable,
    itemIndexValue,
  )
  const conditionPattern = new RegExp(
    `(?:if|else\\s+if)\\s*\\(\\s*${escapeForRegex(itemIndexVariable)}\\s*(==|!=|>=|<=|>|<)\\s*([^)]*?)\\s*\\)\\s*\\{`,
    "g",
  )
  let sawBranch = false
  let lastBranchCloseIndex = -1
  let selectedBody = null

  for (const match of selectButtonBody.matchAll(conditionPattern)) {
    if (match.index === undefined) {
      continue
    }

    const openBraceIndex = selectButtonBody.indexOf("{", match.index)
    if (openBraceIndex < 0) {
      continue
    }

    const closeBraceIndex = findMatchingBrace(selectButtonBody, openBraceIndex)
    if (closeBraceIndex < 0) {
      continue
    }

    sawBranch = true
    lastBranchCloseIndex = closeBraceIndex
    const comparisonValue = evaluateIntegerExpression(
      match[2],
      expressionValues,
    )
    if (comparisonValue === null) {
      continue
    }

    if (evaluateIntegerComparison(itemIndexValue, match[1], comparisonValue)) {
      selectedBody = selectButtonBody.slice(openBraceIndex + 1, closeBraceIndex)
      break
    }
  }

  if (!sawBranch) {
    selectedBody = selectButtonBody
  } else if (!selectedBody) {
    selectedBody = selectButtonBody.slice(lastBranchCloseIndex + 1)
  }

  const target = extractReturnStatements(selectedBody).find(
    (statement) => statement.target,
  )?.target
  if (!target) {
    return null
  }

  return {
    target,
    stateByVar: applyCurrentValueItemAssignments(
      selectedBody,
      stateByVar,
      expressionValues,
    ),
  }
}

function dxParamDescriptorScope(paramValue, context) {
  if (paramValue < 0) {
    return {
      scope: "literal",
      group: [0],
      offset: 0,
      longLabels: ["random detune"],
      shortLabels: ["detune"],
    }
  }

  if (paramValue < context.globalBase) {
    const offset =
      ((paramValue % context.opGroupSize) + context.opGroupSize) %
      context.opGroupSize
    const group = context.opGroups.find(
      ([start, end]) => offset >= start && offset <= end,
    )
    return group
      ? {
          scope: "op",
          group: rangeInclusive(group[0], group[1]),
          offset,
          longLabels: context.opLong,
          shortLabels: context.opShort,
        }
      : null
  }

  const offset = paramValue - context.globalBase
  const group = context.globalGroups.find(
    ([start, end]) => offset >= start && offset <= end,
  )
  return group
    ? {
        scope: "global",
        group: rangeInclusive(group[0], group[1]),
        offset,
        longLabels: context.globalLong,
        shortLabels: context.globalShort,
      }
    : null
}

function rangeInclusive(start, end) {
  const values = []
  for (let value = start; value <= end; value += 1) {
    values.push(value)
  }
  return values
}

function comparableParamLabel(label) {
  return label
    .toLowerCase()
    .replace(/\bscaling\b/g, "scale")
    .replace(/\bsense\b/g, "sens")
    .replace(/^dx7\s+/g, "")
    .replace(/[^a-z0-9]/g, "")
}

function matchingDescriptorIndexForEntry(entryName, descriptorScope) {
  const entryIdentity = comparableParamLabel(entryName)
  const matches = descriptorScope.group.filter((descriptorIndex) => {
    const descriptorIdentity = comparableParamLabel(
      descriptorScope.longLabels[descriptorIndex] ?? "",
    )
    return (
      descriptorIdentity === entryIdentity ||
      descriptorIdentity.endsWith(entryIdentity) ||
      entryIdentity.endsWith(descriptorIdentity)
    )
  })

  return matches.length === 1 ? matches[0] : null
}

function descriptorGroupKey(descriptorScope) {
  return `${descriptorScope.scope}:${descriptorScope.group[0]}:${descriptorScope.group.at(-1)}`
}

function selectDescriptorIndicesForItem(entry, descriptorScope, siblingInfos) {
  if (descriptorScope.group.length === 1) {
    return descriptorScope.group
  }

  const selfMatch = matchingDescriptorIndexForEntry(entry.name, descriptorScope)
  if (selfMatch !== null) {
    return [selfMatch]
  }

  const groupKey = descriptorGroupKey(descriptorScope)
  const siblingMatches = new Set()
  for (const siblingInfo of siblingInfos) {
    if (!siblingInfo.descriptorScope) {
      continue
    }
    if (descriptorGroupKey(siblingInfo.descriptorScope) !== groupKey) {
      continue
    }

    const siblingMatch = matchingDescriptorIndexForEntry(
      siblingInfo.entry.name,
      siblingInfo.descriptorScope,
    )
    if (siblingMatch !== null) {
      siblingMatches.add(siblingMatch)
    }
  }

  const remainingIndices = descriptorScope.group.filter(
    (descriptorIndex) => !siblingMatches.has(descriptorIndex),
  )
  return remainingIndices.length > 0 ? remainingIndices : descriptorScope.group
}

function buildDxParamValueOptionChildren(
  descriptorScope,
  descriptorIndex,
  context,
  varName,
) {
  const makeLiteralNodes = (longLabels, shortLabels = longLabels) =>
    longLabels.map((label, index) => ({
      varName: `${varName}_value_${index}`,
      token: `LITERAL_${varName}_value_${index}`,
      oled: label,
      code: shortLabels[index] ?? label,
      children: [],
    }))

  if (descriptorScope.scope === "op" && [11, 12].includes(descriptorIndex)) {
    return makeLiteralNodes(
      context.curves.filter((label) => !/^\?+$/.test(label)),
    )
  }

  if (descriptorScope.scope === "op" && descriptorIndex === 17) {
    return makeLiteralNodes(["ratio", "fixed"], ["rati", "fixd"])
  }

  if (
    descriptorScope.scope === "global" &&
    descriptorIndex === 16 &&
    context.shapesLong.length > 0
  ) {
    return makeLiteralNodes(context.shapesLong, context.shapesShort)
  }

  return []
}

function buildDxParamDescriptorChildrenForState(
  targetType,
  targetState,
  entry,
  siblingInfos,
  sourceStructure,
  virtualPrefix,
) {
  const context = sourceStructure.dxParamDescriptorContext
  const paramValue = targetState.get("param")
  if (
    !context ||
    simpleTypeName(targetType) !== "DxParam" ||
    paramValue === undefined
  ) {
    return null
  }

  const descriptorScope = dxParamDescriptorScope(paramValue, context)
  if (!descriptorScope) {
    return null
  }

  const descriptorIndices = selectDescriptorIndicesForItem(
    entry,
    descriptorScope,
    siblingInfos,
  )

  return descriptorIndices.map((descriptorIndex) => {
    const varName = `virtualDxParam_${virtualPrefix}_${descriptorScope.scope}_${descriptorIndex}`
    return {
      varName,
      token: `LITERAL_${varName}`,
      oled: descriptorScope.longLabels[descriptorIndex],
      code:
        descriptorScope.shortLabels[descriptorIndex] ??
        descriptorScope.longLabels[descriptorIndex],
      children: buildDxParamValueOptionChildren(
        descriptorScope,
        descriptorIndex,
        context,
        varName,
      ),
    }
  })
}

function descriptorScopeForTargetInfo(targetInfo, sourceStructure) {
  if (!targetInfo?.target || !sourceStructure.dxParamDescriptorContext) {
    return null
  }

  const targetType =
    sourceStructure.varToType.get(targetInfo.target) ??
    resolveVarTypeFromCorpus(sourceStructure.menuItemCorpus, targetInfo.target)
  if (simpleTypeName(targetType ?? "") !== "DxParam") {
    return null
  }

  const paramValue = stateForVar(targetInfo.stateByVar, targetInfo.target).get(
    "param",
  )
  return paramValue === undefined
    ? null
    : dxParamDescriptorScope(
        paramValue,
        sourceStructure.dxParamDescriptorContext,
      )
}

function buildCurrentValueItemTableChildren(
  varName,
  typeName,
  english,
  sevenSeg,
  sourceStructure,
  seenVars = new Set(),
  virtualPrefix = varName,
  stateByVar = new Map(),
) {
  if (!typeName || seenVars.has(varName)) {
    return null
  }

  const selectButtonBodyMatch = extractQualifiedSelectButtonBodyMatchesForType(
    sourceStructure.menuItemCorpus,
    typeName,
  )
    .map((bodyMatch) => {
      const table = extractReferencedCurrentValueItemTable(
        sourceStructure.menuItemCorpus,
        bodyMatch,
      )
      return table ? { ...bodyMatch, table } : null
    })
    .filter(Boolean)[0]
  if (!selectButtonBodyMatch) {
    return null
  }

  const entries = extractCurrentValueItemTableEntries(
    selectButtonBodyMatch.table.body,
    sourceStructure,
  )
  if (entries.length === 0) {
    return null
  }

  const nextSeenVars = new Set([...seenVars, varName])
  const rowInfos = entries.map((entry, index) => {
    const targetInfo = resolveCurrentValueItemTargetInfo(
      varName,
      selectButtonBodyMatch.body,
      selectButtonBodyMatch.table.name,
      entry.indexValue,
      sourceStructure,
      stateByVar,
    )
    const targetVar = targetInfo?.target ?? null
    const targetType = targetVar
      ? (sourceStructure.varToType.get(targetVar) ??
        resolveVarTypeFromCorpus(sourceStructure.menuItemCorpus, targetVar))
      : null
    return {
      entry,
      index,
      targetInfo,
      targetVar,
      targetType,
      descriptorScope: descriptorScopeForTargetInfo(
        targetInfo,
        sourceStructure,
      ),
    }
  })

  return rowInfos.map((rowInfo) => {
    const { entry, index, targetInfo, targetVar, targetType } = rowInfo
    const virtualVarName = `virtualItemTable_${virtualPrefix}_${index}`
    const targetState =
      targetInfo && targetVar
        ? stateForVar(targetInfo.stateByVar, targetVar)
        : new Map()
    const descriptorChildren =
      targetType && targetInfo
        ? buildDxParamDescriptorChildrenForState(
            targetType,
            targetState,
            entry,
            rowInfos,
            sourceStructure,
            `${virtualPrefix}_${index}`,
          )
        : null
    const recursiveChildren =
      !descriptorChildren &&
      targetVar &&
      targetType &&
      !nextSeenVars.has(targetVar)
        ? (buildCurrentValueItemTableChildren(
            targetVar,
            targetType,
            english,
            sevenSeg,
            sourceStructure,
            nextSeenVars,
            `${virtualPrefix}_${index}_${targetVar}`,
            targetInfo?.stateByVar ?? stateByVar,
          ) ?? [])
        : []
    const targetChildren = descriptorChildren ?? recursiveChildren

    return {
      varName: virtualVarName,
      token: `LITERAL_${virtualVarName}`,
      oled: entry.name,
      code: entry.shortName || virtualCodeForLabel(entry.name),
      children: targetChildren,
    }
  })
}

function extractCurrentValueDisplayOptionLabels(menuItemCorpus, typeName) {
  if (!typeName) {
    return null
  }

  const oledOptionSets = []
  for (const body of extractMethodBodiesForType(
    menuItemCorpus,
    typeName,
    "drawPixelsForOled",
  )) {
    if (!/\bcurrentValue\b/.test(body)) {
      continue
    }

    for (const [collectionName, labels] of extractInitializedStringCollections(
      body,
    )) {
      if (labels.length === 0) {
        continue
      }

      const drawItemsPattern = new RegExp(
        `\\bdrawItemsForOled\\s*\\([^;{}]*\\b${escapeForRegex(collectionName)}\\b[^;{}]*\\bcurrentValue\\b`,
      )
      if (drawItemsPattern.test(body)) {
        oledOptionSets.push(labels)
      }
    }
  }

  const codeOptionSets = []
  for (const body of extractMethodBodiesForType(
    menuItemCorpus,
    typeName,
    "drawValue",
  )) {
    for (const [arrayName, labels] of extractConstCharStringArrays(body)) {
      if (labels.length === 0) {
        continue
      }

      const currentValueIndexPattern = new RegExp(
        `\\b${escapeForRegex(arrayName)}\\s*\\[\\s*currentValue\\s*\\]`,
      )
      const scrollingTextPattern = new RegExp(
        `\\bsetScrollingText\\s*\\([^;{}]*\\b${escapeForRegex(arrayName)}\\b`,
      )
      if (
        currentValueIndexPattern.test(body) &&
        scrollingTextPattern.test(body)
      ) {
        codeOptionSets.push(labels)
      }
    }
  }

  const oledLabels = oledOptionSets[0] ?? codeOptionSets[0] ?? []
  const codeLabels =
    codeOptionSets.find((labels) => labels.length === oledLabels.length) ??
    oledLabels

  if (oledLabels.length < 2) {
    return null
  }

  return { oledLabels, codeLabels }
}

function buildCurrentValueDisplayOptionChildren(
  varName,
  typeName,
  sourceStructure,
) {
  const labels = extractCurrentValueDisplayOptionLabels(
    sourceStructure.menuItemCorpus,
    typeName,
  )
  if (!labels) {
    return null
  }

  return labels.oledLabels.map((oled, index) => ({
    varName: `virtualCurrentValueDisplay_${varName}_${index}`,
    token: `LITERAL_${varName}_${index}`,
    oled,
    code: labels.codeLabels[index] ?? oled,
    children: [],
  }))
}

function buildPatchCableSelectionChildren(varName, typeName, sourceStructure) {
  const targetInfos = []

  for (const body of extractSelectButtonBodiesForType(
    sourceStructure.menuItemCorpus,
    typeName,
  )) {
    for (const targetRef of extractRawReturnTargetRefs(body)) {
      const targetType = resolveVarTypeFromCorpusWithNamespace(
        sourceStructure.menuItemCorpus,
        targetRef,
      )
      if (!targetType) {
        continue
      }
      if (!typeExtends(targetType, "PatchCableStrength", sourceStructure)) {
        continue
      }
      if (targetInfos.some((targetInfo) => targetInfo.ref === targetRef)) {
        continue
      }
      targetInfos.push({ ref: targetRef, typeName: targetType })
    }
  }

  if (targetInfos.length === 0) {
    return null
  }

  const subject = singularizeLabel(humanizeIdentifier(typeName))
  const subjectId = pascalCaseLabel(subject)
  const strengthLabel = humanizeIdentifier("Strength")
  const strengthId = pascalCaseLabel(strengthLabel)

  return [
    {
      varName: `virtual${subjectId}Selection`,
      token: `LITERAL_${varName}_selected`,
      oled: `Selected ${subject}`,
      code: virtualCodeForLabel(subject),
      children: targetInfos.map((targetInfo) => {
        const targetLabel = humanizeIdentifier(targetInfo.typeName)
        const targetSlug = targetLabel.toLowerCase().replace(/\s+/g, "_")
        const targetId = pascalCaseLabel(targetLabel)
        return {
          varName: `virtual${subjectId}${targetId}`,
          token: `LITERAL_${varName}_${targetSlug}`,
          oled: `${targetLabel} destination`,
          code: virtualCodeForLabel(targetLabel),
          children: [
            {
              varName: `virtual${subjectId}${targetId}${strengthId}`,
              token: `LITERAL_${varName}_${targetSlug}_strength`,
              oled: strengthLabel,
              code: virtualCodeForLabel(strengthLabel),
              children: [],
            },
          ],
        }
      }),
    },
  ]
}

function buildSelectorOptionChildren(
  selectorVar,
  english,
  sevenSeg,
  sourceStructure,
  depth,
) {
  if (depth > 4) {
    return []
  }

  const selectorType = sourceStructure.varToType.get(selectorVar)
  const resolvedSelectorType =
    selectorType ??
    resolveVarTypeFromCorpus(sourceStructure.menuItemCorpus, selectorVar)
  if (!resolvedSelectorType) {
    return []
  }

  const selectorInitializerBody = extractInitializerBody(
    sourceStructure.combined,
    selectorVar,
  )
  const initializerSpecificOptionBody =
    extractSelectionReturnBodyForInitializerEnum(
      sourceStructure.menuItemCorpus,
      resolvedSelectorType,
      selectorInitializerBody,
    )
  if (initializerSpecificOptionBody) {
    const optionTokens = extractSelectionTokensFromBody(
      initializerSpecificOptionBody,
    )
    if (optionTokens.length === 0) {
      return []
    }

    const nextSelector = extractSelectButtonTargetForType(
      sourceStructure.menuItemCorpus,
      resolvedSelectorType,
    )
    const nextChildren = nextSelector
      ? buildSelectorOptionChildren(
          nextSelector,
          english,
          sevenSeg,
          sourceStructure,
          depth + 1,
        )
      : []

    return optionTokens
      .map((token, index) =>
        makeVirtualNode(
          english,
          sevenSeg,
          token,
          `virtualSelector_${selectorVar}_${index}`,
          cloneChildren(nextChildren),
        ),
      )
      .filter(Boolean)
  }

  if (
    typeOptionsReferenceFunction(
      sourceStructure.menuItemCorpus,
      resolvedSelectorType,
      "getModNames",
    )
  ) {
    const modFxNames = extractReturnedSelectionTokensForFunction(
      sourceStructure.sourceCorpus,
      "getModNames",
    )
    if (modFxNames.length > 0) {
      const nextChildren = []
      return modFxNames
        .map((token, index) =>
          makeVirtualNode(
            english,
            sevenSeg,
            token,
            `virtualSelector_${selectorVar}_${index}`,
            cloneChildren(nextChildren),
          ),
        )
        .filter(Boolean)
    }
  }

  const optionTokens = extractSelectionOptionsTokensForType(
    sourceStructure.menuItemCorpus,
    resolvedSelectorType,
  )
  const optionLiterals = extractSelectionOptionLiteralsForType(
    sourceStructure.menuItemCorpus,
    resolvedSelectorType,
  )
  const optionCount = optionTokens.length + optionLiterals.length
  const buildChildSetForOption = (index) => {
    const target = extractSelectButtonTargetForOptionIndex(
      sourceStructure.menuItemCorpus,
      resolvedSelectorType,
      index,
      optionCount,
      sourceStructure,
      optionTokens,
      optionLiterals,
    )

    if (!target) {
      return []
    }

    const updatedTargetChildren = buildUpdatedTargetOptionChildren(
      target,
      resolvedSelectorType,
      index,
      english,
      sevenSeg,
      sourceStructure,
    )
    if (updatedTargetChildren) {
      return updatedTargetChildren
    }

    const targetNode = buildNode(
      sourceStructure.combined,
      sourceStructure.arrayChildren,
      sourceStructure.varToToken,
      english,
      sevenSeg,
      sourceStructure,
      target,
      [selectorVar],
      depth + 1,
    )

    if (targetNode && (targetNode.children ?? []).length > 0) {
      return targetNode.children
    }

    return buildSelectorOptionChildren(
      target,
      english,
      sevenSeg,
      sourceStructure,
      depth + 1,
    )
  }

  if (optionTokens.length === 0 && optionLiterals.length === 0) {
    const nextSelector = extractSelectButtonTargetForType(
      sourceStructure.menuItemCorpus,
      resolvedSelectorType,
    )
    if (!nextSelector) {
      return []
    }

    return buildSelectorOptionChildren(
      nextSelector,
      english,
      sevenSeg,
      sourceStructure,
      depth + 1,
    )
  }

  const displayConditionalPair =
    extractDisplayConditionalSingleOptionPairForType(
      sourceStructure.menuItemCorpus,
      resolvedSelectorType,
    )
  if (displayConditionalPair && optionLiterals.length === 0) {
    const confirmNode = makeVirtualNode(
      english,
      sevenSeg,
      displayConditionalPair.oledToken,
      `virtualSelector_${selectorVar}_confirm`,
      cloneChildren(buildChildSetForOption(0)),
      displayConditionalPair.sevenSegToken,
    )

    return confirmNode ? [confirmNode] : []
  }

  const tokenChildren = optionTokens
    .map((token, index) =>
      makeVirtualNode(
        english,
        sevenSeg,
        token,
        `virtualSelector_${selectorVar}_${index}`,
        cloneChildren(buildChildSetForOption(index)),
      ),
    )
    .filter(Boolean)

  const literalChildren = optionLiterals.map((value, index) => ({
    varName: `virtualSelectorLiteral_${selectorVar}_${index}`,
    token: `LITERAL_${selectorVar}_${index}`,
    oled: value,
    code: value,
    children: cloneChildren(
      buildChildSetForOption(optionTokens.length + index),
    ),
  }))

  return [...tokenChildren, ...literalChildren]
}

function buildDynamicSelectionChildren(
  varName,
  english,
  sevenSeg,
  sourceStructure,
) {
  const typeName = sourceStructure.varToType.get(varName) ?? ""

  if (typeExtends(typeName, "SyncLevel", sourceStructure)) {
    return buildSyncLevelChildren(varName, sourceStructure)
  }

  const customIteranceTarget = extractCustomIteranceTargetForType(
    sourceStructure.menuItemCorpus,
    typeName,
    sourceStructure.iteranceCustomPreset,
    sourceStructure.iteranceConditionValues,
  )
  if (customIteranceTarget) {
    return buildIteranceChildren(varName, customIteranceTarget, sourceStructure)
  }

  const fillOptionCandidate = sourceStructure.fillOptionCandidates.find(
    ({ functionName }) =>
      typeBodyReferences(
        sourceStructure.menuItemCorpus,
        typeName,
        new RegExp(`\\b${escapeForRegex(functionName)}\\s*\\(`),
      ),
  )
  if (fillOptionCandidate) {
    return fillOptionCandidate.labels.map((label, index) => ({
      varName: `virtualFill_${varName}_${index}`,
      token: `LITERAL_${varName}_${index}`,
      oled: label,
      code: label,
      children: [],
    }))
  }

  const itemTableChildren = buildCurrentValueItemTableChildren(
    varName,
    typeName,
    english,
    sevenSeg,
    sourceStructure,
  )
  if (itemTableChildren) {
    return itemTableChildren
  }

  const currentValueDisplayChildren = buildCurrentValueDisplayOptionChildren(
    varName,
    typeName,
    sourceStructure,
  )
  if (currentValueDisplayChildren) {
    return currentValueDisplayChildren
  }

  if (typeExtends(typeName, "ActiveScaleMenu", sourceStructure)) {
    return sourceStructure.presetScaleNames.map((scaleName, index) => ({
      varName: `virtualActiveScale_${varName}_${index}`,
      token: `LITERAL_${varName}_${index}`,
      oled: scaleName,
      code: scaleName,
      children: [],
    }))
  }

  const constructorSubmenuChildren = buildConstructorSubmenuChildren(
    varName,
    typeName,
    english,
    sevenSeg,
    sourceStructure,
  )
  if (constructorSubmenuChildren) {
    return constructorSubmenuChildren
  }

  const patchCableChildren = buildPatchCableSelectionChildren(
    varName,
    typeName,
    sourceStructure,
  )
  if (patchCableChildren) {
    return patchCableChildren
  }

  const representativeTargetChildren = buildRepresentativeTargetChildren(
    varName,
    typeName,
    english,
    sevenSeg,
    sourceStructure,
  )
  if (representativeTargetChildren) {
    return representativeTargetChildren
  }

  return null
}

function buildSongClipSettingsVirtualTree(sourceText, english, sevenSeg) {
  const clipSettingsTokens = extractMenuOptionTokensForType(
    sourceText,
    "ClipSettingsMenu",
  )
  const launchStyleTokens = extractMenuOptionTokensForType(
    sourceText,
    "LaunchStyleMenu",
  )

  const clipModeToken = clipSettingsTokens.at(-2) ?? null
  const clipNameToken = clipSettingsTokens.at(-1) ?? null
  const convertToken =
    clipSettingsTokens.length > 2 && clipSettingsTokens[0] !== clipModeToken
      ? clipSettingsTokens[0]
      : null

  const convert = convertToken
    ? labelFromToken(english, sevenSeg, convertToken)
    : null
  const clipMode = clipModeToken
    ? labelFromToken(english, sevenSeg, clipModeToken)
    : null
  const clipName = clipNameToken
    ? labelFromToken(english, sevenSeg, clipNameToken)
    : null
  const launchStyleLabels = launchStyleTokens
    .map((token) => labelFromToken(english, sevenSeg, token))
    .filter(Boolean)

  const menu = {
    varName: "songClipSettingsMenu",
    token: "VIRTUAL_SONG_CLIP_SETTINGS",
    oled: "Song Clip Settings",
    code: "CLIP",
    children: [],
  }

  if (convert) {
    menu.children.push({
      varName: "virtualConvertToAudio",
      token: convert.token,
      oled: convert.oled,
      code: convert.code,
      children: [],
    })
  }

  if (clipMode) {
    const clipModeChildren = launchStyleLabels.map((item, index) => ({
      varName: `virtualLaunchMode${index}`,
      token: item.token,
      oled: item.oled,
      code: item.code,
      children: [],
    }))

    menu.children.push({
      varName: "virtualClipMode",
      token: clipMode.token,
      oled: clipMode.oled,
      code: clipMode.code,
      children: clipModeChildren,
    })
  }

  if (clipName) {
    menu.children.push({
      varName: "virtualClipName",
      token: clipName.token,
      oled: clipName.oled,
      code: clipName.code,
      children: [],
    })
  }

  return menu
}

function buildNode(
  sourceText,
  arrayChildren,
  varToToken,
  english,
  sevenSeg,
  sourceStructure,
  varName,
  ancestors,
  depth,
) {
  if (depth > 15) {
    return null
  }

  if (ancestors.includes(varName)) {
    return null
  }

  const token = resolveTokenForVar(varName, varToToken, sourceStructure)
  if (!token) {
    return null
  }

  const label = labelFromToken(english, sevenSeg, token)
  if (!label) {
    return null
  }
  const varAwareLabel = applyVarNameLabelOverrides(varName, label)

  const dynamicChildren = buildDynamicSelectionChildren(
    varName,
    english,
    sevenSeg,
    sourceStructure,
  )
  if (dynamicChildren && dynamicChildren.length > 0) {
    return {
      varName,
      token,
      oled: varAwareLabel.oled,
      code: varAwareLabel.code,
      children: dynamicChildren,
    }
  }

  const childVars = extractChildren(sourceText, arrayChildren, varName)
  const childrenFromStructure = childVars
    .map((childVar) =>
      buildNode(
        sourceText,
        arrayChildren,
        varToToken,
        english,
        sevenSeg,
        sourceStructure,
        childVar,
        [...ancestors, varName],
        depth + 1,
      ),
    )
    .filter(Boolean)

  const selectionChildren = buildImplicitOptionChildren(
    varName,
    english,
    sevenSeg,
    sourceStructure,
  )

  const children =
    childrenFromStructure.length > 0 ? childrenFromStructure : selectionChildren

  const nodeStructuralKey = (node) => {
    const childKeys = (node.children ?? []).map((child) =>
      nodeStructuralKey(child),
    )
    return `${node.oled}|${node.code}|[${childKeys.join(";")}]`
  }

  const dedupeStructurallyEquivalentSiblings = (items) => {
    const seen = new Set()
    return items.filter((child) => {
      // Wildcard labels (e.g. Envelope *, Osc*) intentionally represent
      // distinct menu instances and should not be collapsed.
      if (child.oled?.includes("*") || child.code?.includes("*")) {
        return true
      }

      const key = nodeStructuralKey(child)
      if (seen.has(key)) {
        return false
      }
      seen.add(key)
      return true
    })
  }

  const isKitRowOnlyNode = (node) => {
    const childTypeName = sourceStructure.varToType.get(node.varName) ?? ""
    const relevantBodies = extractIsRelevantBodies(
      sourceStructure.menuItemCorpus,
      childTypeName,
    )
    const combinedBody = relevantBodies.join("\n")
    return /(^|[^!])\bsoundEditor\.editingKitRow\s*\(\s*\)/.test(combinedBody)
  }

  const dedupeKitSpecificDisplaySiblings = (items) => {
    const groups = new Map()
    for (const child of items) {
      const key = `${child.oled}|${child.code}`
      groups.set(key, [...(groups.get(key) ?? []), child])
    }

    const kitOnlyDuplicates = new Set()
    for (const group of groups.values()) {
      if (group.length < 2) {
        continue
      }

      const kitOnlyNodes = group.filter(isKitRowOnlyNode)
      if (kitOnlyNodes.length === 0 || kitOnlyNodes.length === group.length) {
        continue
      }

      for (const node of kitOnlyNodes) {
        kitOnlyDuplicates.add(node)
      }
    }

    return items.filter((child) => !kitOnlyDuplicates.has(child))
  }

  const dedupedChildren = dedupeStructurallyEquivalentSiblings(children)
  const finalChildren = dedupeKitSpecificDisplaySiblings(dedupedChildren)

  return {
    varName,
    token,
    oled: varAwareLabel.oled,
    code: varAwareLabel.code,
    children: finalChildren,
  }
}

try {
  build()
} catch (error) {
  console.error("[generate-menu-hierarchies] Generation failed.")
  if (error instanceof Error) {
    console.error(error.message)
  } else {
    console.error(error)
  }
  process.exitCode = 1
}
