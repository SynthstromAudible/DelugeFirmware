const DISPLAY_CODE_PATTERN = /\(([^()]+)\)/g
const MENU_HIERARCHY_PATH =
  /\/reference\/(menu_hierarchies|menu-hierarchies)\/?$/i
const MAX_DISPLAY_CODE_LENGTH = 32
const SEVEN_SEG_PREVIEW_LENGTH = 4
const NON_DISPLAY_PARENTHETICAL_PATTERN =
  /^(if |note:|only |or |e\.g\.|can |each |synth clips$|kit rows$|no fx$|default\.xml$|synth \/ kit \/ midi \/ cv$)/i

type SingleDisplayPart = {
  leadingWhitespace: string
  oled: string
  displayCode: string
  after: string
}

type SeparatorDisplayPart = {
  text: string
}

type DisplayPart = SingleDisplayPart | SeparatorDisplayPart

function getTagClassSuffix(tag: string) {
  return tag
    .toLowerCase()
    .replace(/[^a-z0-9]+/g, "-")
    .replace(/^-+|-+$/g, "")
}

function isDisplayCode(value: string) {
  const code = value.trim()

  if (!code || code.length > MAX_DISPLAY_CODE_LENGTH) {
    return false
  }

  if (NON_DISPLAY_PARENTHETICAL_PATTERN.test(code)) {
    return false
  }

  if (!/^[A-Za-z0-9#.+/_&>*\-\s]+$/.test(code)) {
    return false
  }

  return true
}

function getSingleDisplayParts(text: string): SingleDisplayPart | undefined {
  const matches = [...text.matchAll(DISPLAY_CODE_PATTERN)]
  const displayMatch = matches
    .reverse()
    .find((match) => isDisplayCode(match[1]))

  if (!displayMatch || displayMatch.index === undefined) {
    return undefined
  }

  const before = text.slice(0, displayMatch.index)
  const after = text.slice(displayMatch.index + displayMatch[0].length)
  const leadingWhitespace = before.match(/^\s*/)?.[0] ?? ""
  const oled = before.slice(leadingWhitespace.length).trimEnd()

  if (!oled.trim()) {
    return undefined
  }

  return {
    leadingWhitespace,
    oled,
    displayCode: displayMatch[1].trim(),
    after,
  }
}

function getSlashSeparatedDisplayParts(
  text: string,
): DisplayPart[] | undefined {
  const pieces = text.split(/(\s+\/\s+)/)
  const transformedPieces = pieces.map((piece) => {
    if (/^\s+\/\s+$/.test(piece)) {
      return { text: piece }
    }

    return getSingleDisplayParts(piece)
  })

  const displayCount = transformedPieces.filter(
    (piece) => piece && !("text" in piece),
  ).length
  const labelCount = pieces.filter(
    (piece) => !/^\s+\/\s+$/.test(piece) && piece.trim(),
  ).length

  if (displayCount < 2 || displayCount !== labelCount) {
    return undefined
  }

  return transformedPieces.filter(Boolean) as DisplayPart[]
}

function getDisplayParts(text: string): DisplayPart[] | undefined {
  const slashSeparatedParts = getSlashSeparatedDisplayParts(text)

  if (slashSeparatedParts) {
    return slashSeparatedParts
  }

  const displayParts = getSingleDisplayParts(text)

  return displayParts ? [displayParts] : undefined
}

function getSevenSegPreview(value: string) {
  const normalized = value.trim().replace(/\s+/g, " ")

  if (normalized.length <= SEVEN_SEG_PREVIEW_LENGTH) {
    return normalized
  }

  return normalized.slice(0, SEVEN_SEG_PREVIEW_LENGTH).toUpperCase()
}

function toggleSevenSegPreview(sevenSegment: HTMLElement) {
  const shortLabel = sevenSegment.dataset.shortLabel ?? ""
  const fullLabel = sevenSegment.dataset.fullLabel ?? shortLabel
  const expanded = sevenSegment.dataset.expanded === "true"
  const nextExpanded = !expanded

  sevenSegment.dataset.expanded = nextExpanded ? "true" : "false"
  sevenSegment.setAttribute("aria-pressed", nextExpanded ? "true" : "false")
  sevenSegment.setAttribute(
    "aria-label",
    `7SEG: ${nextExpanded ? fullLabel : shortLabel}. Activate to ${nextExpanded ? "show short" : "show full"} text.`,
  )
  sevenSegment.textContent = nextExpanded ? fullLabel : shortLabel

  const label = sevenSegment.closest(".menu-hierarchy-display-label")
  if (label instanceof HTMLElement) {
    label.dataset.sevenSegmentExpanded = nextExpanded ? "true" : "false"
  }
}

function createDisplayLabel(
  { oled, displayCode }: SingleDisplayPart,
  allowSevenSegToggle: boolean,
) {
  const label = document.createElement("span")
  label.className = "menu-hierarchy-display-label"
  label.dataset.oledLabel = oled.trim()
  label.dataset.sevenSegmentLabel = displayCode

  const sevenSegment = document.createElement("span")
  sevenSegment.className = "menu-hierarchy-7seg"

  const shortLabel = getSevenSegPreview(displayCode)
  sevenSegment.dataset.shortLabel = shortLabel
  sevenSegment.dataset.fullLabel = displayCode
  sevenSegment.dataset.expanded = "false"
  sevenSegment.textContent = shortLabel

  if (allowSevenSegToggle && shortLabel !== displayCode) {
    label.dataset.sevenSegmentExpanded = "false"
    sevenSegment.classList.add("menu-hierarchy-7seg-toggle")
    sevenSegment.tabIndex = 0
    sevenSegment.setAttribute("role", "button")
    sevenSegment.setAttribute("aria-pressed", "false")
    sevenSegment.setAttribute(
      "aria-label",
      `7SEG: ${shortLabel}. Activate to show full text.`,
    )
    sevenSegment.addEventListener("click", () => {
      toggleSevenSegPreview(sevenSegment)
    })
    sevenSegment.addEventListener("keydown", (event) => {
      if (event.key === "Enter" || event.key === " ") {
        event.preventDefault()
        toggleSevenSegPreview(sevenSegment)
      }
    })
  } else {
    sevenSegment.setAttribute("aria-label", `7SEG: ${displayCode}`)
  }

  const sevenSegmentValue = document.createElement("span")
  sevenSegmentValue.className = "menu-hierarchy-7seg-value"
  sevenSegmentValue.append(sevenSegment, document.createTextNode(" "))

  const oledText = document.createElement("span")
  oledText.className = "menu-hierarchy-oled"
  oledText.textContent = oled.trim()

  label.append(sevenSegmentValue, oledText)

  return label
}

function ensureHide7SegToggle(content: HTMLElement) {
  let switchInput = content.querySelector<HTMLInputElement>(
    "[data-menu-hierarchy-hide-7seg]",
  )

  if (!switchInput) {
    const toggleLabel = document.createElement("label")
    toggleLabel.className = "menu-hierarchy-hide-7seg-toggle"

    const text = document.createElement("span")
    text.textContent = "Hide 7SEG"

    switchInput = document.createElement("input")
    switchInput.type = "checkbox"
    switchInput.setAttribute("role", "switch")
    switchInput.setAttribute("aria-label", "Hide 7SEG display labels")
    switchInput.setAttribute("data-menu-hierarchy-hide-7seg", "")

    toggleLabel.append(text, switchInput)
    content.prepend(toggleLabel)
  }

  const updateVisibility = () => {
    content.dataset.hideSevenSegment = switchInput.checked ? "true" : "false"
  }

  if (switchInput.dataset.menuHierarchyHide7segReady !== "true") {
    switchInput.dataset.menuHierarchyHide7segReady = "true"
    switchInput.addEventListener("change", updateVisibility)
  }

  updateVisibility()
}

function enhanceNode(node: HTMLElement) {
  if (node.dataset.menuHierarchyDisplayLabel === "true") {
    return
  }

  const textNode = Array.from(node.childNodes).find(
    (childNode) =>
      childNode.nodeType === Node.TEXT_NODE &&
      Boolean(childNode.textContent?.trim()),
  )

  if (!textNode?.textContent) {
    return
  }

  const displayParts = getDisplayParts(textNode.textContent)

  if (!displayParts) {
    return
  }

  const allowSevenSegToggle = !node.closest("summary")

  const replacementNodes = displayParts.flatMap((part) => {
    if ("text" in part) {
      return [document.createTextNode(part.text)]
    }

    return [
      document.createTextNode(part.leadingWhitespace),
      createDisplayLabel(part, allowSevenSegToggle),
      document.createTextNode(part.after),
    ]
  })

  textNode.replaceWith(...replacementNodes)
  node.dataset.menuHierarchyDisplayLabel = "true"
}

function enhanceSummaryTags(content: HTMLElement) {
  content.querySelectorAll(".menu-hierarchy-tree-root").forEach((treeRoot) => {
    if (!(treeRoot instanceof HTMLElement)) {
      return
    }

    const tag = treeRoot.dataset.treeTag?.trim() ?? ""
    if (!tag) {
      return
    }

    const details = treeRoot.closest("details")
    if (!(details instanceof HTMLElement)) {
      return
    }

    const summary = details.querySelector(":scope > summary")
    if (!(summary instanceof HTMLElement)) {
      return
    }

    if (summary.querySelector(".menu-hierarchy-node-tag")) {
      return
    }

    summary.append(" ")

    const badge = document.createElement("span")
    const tagClassSuffix = getTagClassSuffix(tag)
    badge.className = `menu-hierarchy-node-tag menu-hierarchy-node-tag--${tagClassSuffix}`
    badge.setAttribute("data-tag", tag)
    badge.setAttribute("aria-label", `Menu tag ${tag}`)
    badge.textContent = tag
    summary.append(badge)
  })
}

export function enhanceMenuHierarchyLabels() {
  if (!MENU_HIERARCHY_PATH.test(window.location.pathname)) {
    return
  }

  const content = document.querySelector<HTMLElement>(".sl-markdown-content")
  if (!content) {
    return
  }

  content.classList.add("menu-hierarchy-page")
  ensureHide7SegToggle(content)
  enhanceSummaryTags(content)

  content
    .querySelectorAll(".menu-hierarchy-row-label, li, summary")
    .forEach((node) => {
      if (node instanceof HTMLElement) {
        enhanceNode(node)
      }
    })
}
