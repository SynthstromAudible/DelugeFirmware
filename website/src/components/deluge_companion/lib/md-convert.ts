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

/**
 * Parse a string like 'press(X)' into an action (PRESS) and control (X).
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

export const parseMd = (mdContent: string): Shortcut[] => {
  const results: Shortcut[] = []

  let current: Shortcut | null = null
  const tokens = marked.lexer(mdContent)

  tokens.forEach((t) => {
    if (t.type === "heading") {
      if (t.depth === 1) {
        // h1 header, meaning a new shortcut starts
        if (current) {
          results.push(current)
        }
        current = { name: t.text, steps: [], views: [], paragraphs: [] }
        return
      }
    }
    if (t.type === "code") {
      if (t.lang === "shortcut" && current) {
        // code tag containing shortcut definition
        current.steps.push(...parseSteps(t.text))
        return
      }
    }
    if (t.type === "paragraph" && t.raw.startsWith("#") && current) {
      // paragraph starting with '#', so it's probably a list of views
      current.views.push(
        ...t.raw
          .trim()
          .split(/[\s,]+/)
          .map((viewTag) => {
            const matches = viewTag.match(/^#(\w+)$/)
            if (!matches) {
              throw new Error(
                "Could not parse a line that looked like a list of view tags: " +
                  t.raw,
              )
            }
            const [, view] = matches
            const parsedView = Views[view.toUpperCase() as keyof typeof Views]
            if (parsedView == null) {
              throw new Error(
                `Unknown view tag "#${view}" in shortcut "${current?.name}". Add it to Views in src/components/deluge_companion/data/views.ts.`,
              )
            }
            return parsedView
          })
          .filter((v) => v != null),
      )
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
      current?.paragraphs.push({ spans })
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
    results.push(current)
  }

  return results
}
