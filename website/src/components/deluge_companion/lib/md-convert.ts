import { marked } from "marked"
import type {
  Shortcut,
  Span,
  Step,
  StepOrSubstep,
  SubstepContainer,
} from "../types/shortcut.js"
import { Action } from "../data/actions.js"
import { Control } from "../data/targets.js"
import { Views } from "../data/views.js"
import { Firmwares } from "../data/firmware.js"
import { describeShortcutSteps } from "../data/shortcut_descriptions.js"

/**
 * Parse a string like 'press(X)' into an action (PRESS) and control (X).
 * Returns a normalized Step record.
 */
const parseActionAndControl = (str: string): Step => {
  const matches = str.match(/^(\w+)\(([^)]+)\)$/)
  if (!matches) {
    throw new Error(
      `Shortcut code not in format "action(Control)" in action "${str}"`,
    )
  }
  const [, actionStr, controlStr] = matches
  const action = Action[actionStr.toUpperCase() as keyof typeof Action]
  const control = Control[controlStr.toUpperCase() as keyof typeof Control]
  if (action === undefined) {
    throw new Error(
      `Shortcut action "${actionStr}" not recognized in shortcut "${str}"`,
    )
  }
  if (control === undefined) {
    throw new Error(
      `Shortcut control "${controlStr}" not recognized in shortcut "${str}"`,
    )
  }
  return {
    action,
    control,
  }
}

// Returns a SubstepContainer parsed from a '+'-joined step string.
const parseSubstepContainer = (s: string): SubstepContainer => {
  return {
    substeps: s
      .split("+")
      .map((s) => s.trim())
      .map(parseActionAndControl),
  }
}

/**
 * Hacky way to parse a string containing multiple (nested) steps like
 * 'press(X) + press(Y), hold(Z)' into a (nested) list of JSON steps.
 * Returns a normalized list of StepOrSubstep values.
 */
const parseSteps = (str: string): StepOrSubstep[] => {
  return str
    .split(",")
    .map((s) => s.trim())
    .map((s) => {
      if (s.includes("+")) {
        return parseSubstepContainer(s)
      } else {
        return parseActionAndControl(s)
      }
    })
}

// Returns text normalized for whitespace-insensitive comparisons.
const normalizeText = (text: string) => text.trim().replace(/\s+/g, " ")

// Returns plain paragraph text, or null when the paragraph includes step spans.
const getPlainParagraphText = (spans: Span[]) => {
  if (spans.some((span) => span.steps)) {
    return null
  }

  return spans.map((span) => span.text ?? "").join("")
}

// Returns true when paragraph text duplicates generated description text.
const isGeneratedDescriptionParagraph = (
  spans: Span[],
  description: string | undefined,
) => {
  const paragraphText = getPlainParagraphText(spans)

  if (!paragraphText || !description) {
    return false
  }

  return normalizeText(paragraphText) === normalizeText(description)
}

type ParseMdOptions = {
  subCapabilityId: string
}

// Returns a slug-safe metadata id fragment.
const toMetadataSlug = (value: string) =>
  value
    .trim()
    .toLowerCase()
    .replace(/[^a-z0-9]+/g, "-")
    .replace(/^-+|-+$/g, "")

  // Returns parsed metadata directive info, or null when line is not a directive.
const parseFileMetadataDirective = (rawLine: string) => {
  const line = rawLine.trim()
  const matches = line.match(/^([A-Za-z][A-Za-z\s-]*)\s*:\s*(.+)$/)
  if (!matches) {
    return null
  }

  const [, keyRaw, valueRaw] = matches
  const key = keyRaw.toLowerCase().trim().replace(/\s+/g, "-")
  const value = valueRaw.trim()

  if (value.length === 0) {
    throw new Error(`Empty value in file metadata directive "${rawLine}"`)
  }

  if (key === "capability") {
    return { type: "capability" as const, value }
  }

  if (key === "sub-capability" || key === "subcapability") {
    return { type: "subCapabilityTitle" as const, value }
  }

  if (key === "sub-sub-capability" || key === "subsubcapability") {
    return { type: "subSubCapabilityTitle" as const, value }
  }

  if (key === "capability-order") {
    if (!/^\d+$/.test(value)) {
      throw new Error(
        `Invalid numeric value in file metadata directive "${rawLine}"`,
      )
    }

    return { type: "capabilityOrder" as const, value: Number(value) }
  }

  if (key === "sub-capability-order" || key === "subcapability-order") {
    if (!/^\d+$/.test(value)) {
      throw new Error(
        `Invalid numeric value in file metadata directive "${rawLine}"`,
      )
    }

    return { type: "subCapabilityOrder" as const, value: Number(value) }
  }

  if (
    key === "sub-sub-capability-order" ||
    key === "subsubcapability-order"
  ) {
    if (!/^\d+$/.test(value)) {
      throw new Error(
        `Invalid numeric value in file metadata directive "${rawLine}"`,
      )
    }

    return { type: "subSubCapabilityOrder" as const, value: Number(value) }
  }

  return null
}

// Returns normalized shortcut objects parsed from markdown content.
export const parseMd = (mdContent: string, options: ParseMdOptions): Shortcut[] => {
  const results: Shortcut[] = []

  let current: Shortcut | null = null
  let fileCapabilityParentId: string | undefined
  let fileCapabilityOrder: number | undefined
  let fileSubCapabilityTitle: string | undefined
  let fileSubCapabilityOrder: number | undefined
  let fileSubSubCapabilityTitle: string | undefined
  let fileSubSubCapabilityOrder: number | undefined
  const fileSubCapabilityId = options.subCapabilityId

  // Returns a single shortcut with deduplicated views/firmware and default firmware fallback.
  const finalizeShortcut = (shortcut: Shortcut): Shortcut => {
    const normalizedViews = [...new Set(shortcut.views)]
    const normalizedFirmware = [...new Set(shortcut.firmware)]

    return {
      ...shortcut,
      views: normalizedViews,
      firmware:
        normalizedFirmware.length > 0
          ? normalizedFirmware
          : [Firmwares.OFFICIAL, Firmwares.COMMUNITY],
    }
  }

  const tokens = marked.lexer(mdContent)

  tokens.forEach((t) => {
    if (t.type === "heading") {
      if (t.depth === 1) {
        // h1 header, meaning a new shortcut starts
        if (current) {
          results.push(finalizeShortcut(current))
        }
        current = {
          name: t.text,
          capability: fileSubCapabilityTitle ? fileSubCapabilityId : undefined,
          capabilityParentId: fileCapabilityParentId,
          capabilityOrder: fileCapabilityOrder,
          subCapabilityTitle: fileSubCapabilityTitle,
          subCapabilityOrder:
            fileSubCapabilityTitle != null ? fileSubCapabilityOrder : undefined,
          subSubCapabilityId:
            fileSubCapabilityTitle && fileSubSubCapabilityTitle
              ? `${fileSubCapabilityId}::${toMetadataSlug(fileSubSubCapabilityTitle)}`
              : undefined,
          subSubCapabilityTitle:
            fileSubCapabilityTitle ? fileSubSubCapabilityTitle : undefined,
          subSubCapabilityOrder:
            fileSubCapabilityTitle && fileSubSubCapabilityTitle
              ? fileSubSubCapabilityOrder
              : undefined,
          steps: [],
          views: [],
          firmware: [],
          paragraphs: [],
        }
        return
      }
    }
    if (t.type === "paragraph" && !current) {
      const directives = t.raw
        .trim()
        .split("\n")
        .map((line) => parseFileMetadataDirective(line))
        .filter((directive) => directive != null)

      if (directives.length > 0) {
        for (const directive of directives) {
          if (directive.type === "capability") {
            fileCapabilityParentId = directive.value
            continue
          }

          if (directive.type === "capabilityOrder") {
            fileCapabilityOrder = directive.value
            continue
          }

          if (directive.type === "subCapabilityOrder") {
            fileSubCapabilityOrder = directive.value
            continue
          }

          if (directive.type === "subSubCapabilityTitle") {
            fileSubSubCapabilityTitle = directive.value
            continue
          }

          if (directive.type === "subSubCapabilityOrder") {
            fileSubSubCapabilityOrder = directive.value
            continue
          }

          fileSubCapabilityTitle = directive.value
        }
      }

      return
    }
    if (t.type === "code") {
      if (t.lang === "shortcut" && current) {
        // code tag containing shortcut definition
        current.steps.push(...parseSteps(t.text))
        current.description = describeShortcutSteps(current.steps)
        return
      }

      if (t.lang === "community" && current) {
        const text = t.text.trim()
        if (text.length > 0) {
          current.paragraphs.push({
            type: "community",
            spans: [{ text }],
          })
        }
        return
      }
    }
    if (t.type === "paragraph" && t.raw.startsWith("#") && current) {
      // paragraph starting with '#', so it's probably a list of tags
      const currentShortcut = current
      t.raw
        .trim()
        .split(/[\s,]+/)
        .forEach((tagToken) => {
          const matches = tagToken.match(/^#([A-Za-z0-9_-]+)$/)
          if (!matches) {
            throw new Error(
              "Could not parse a line that looked like a list of tags: " +
                t.raw,
            )
          }

          const [, rawTag] = matches
          const upperTag = rawTag.toUpperCase()

          const parsedView = Views[upperTag as keyof typeof Views]
          if (parsedView != null) {
            currentShortcut.views.push(parsedView)
            return
          }

          const parsedFirmware =
            Firmwares[upperTag as keyof typeof Firmwares]
          if (parsedFirmware != null) {
            currentShortcut.firmware.push(parsedFirmware)
            return
          }

          throw new Error(
            `Unknown tag "#${rawTag}" in shortcut "${currentShortcut.name}". Add it to Views in src/components/deluge_companion/data/views.ts or Firmwares in src/components/deluge_companion/data/firmware.ts.`,
          )
        })
      return
    }

    if (t.type === "paragraph" && t.tokens) {
      const spans: Span[] = t.tokens
        .map((t) => {
          if (t.type === "text") {
            return {
              text: t.raw,
            }
          }
          if (t.type === "codespan") {
            return { steps: parseSteps(t.text) }
          }
        })
        .filter((s) => !!s)

      if (
        current &&
        !isGeneratedDescriptionParagraph(spans, current.description)
      ) {
        current.paragraphs.push({ spans })
      }
      return
    }

    if (t.type === "space") {
      // ignore whitespace
      return
    }
    // everything else is unexpected and therefore ignored right now
    console.warn("Ignoring token ", t)
  })

  if (current) {
    results.push(finalizeShortcut(current))
  }

  return results
}
