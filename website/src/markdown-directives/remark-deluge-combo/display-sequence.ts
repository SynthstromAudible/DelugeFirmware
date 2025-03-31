import type { ElementContent } from "hast"
import { type Action, ACTIONS, KEYS, KNOB_MODIFIERS } from "./data.ts"

const icons = Object.fromEntries(
  await Promise.all(
    Object.entries(
      import.meta.glob<{ default: ImageMetadata }>("../../assets/icons/*.svg"),
    ).map(
      async ([path, promise]) =>
        [path.split("/").at(-1)?.split(".")[0], (await promise()).default] as [
          string,
          ImageMetadata,
        ],
    ),
  ),
)

const parseAction = (key: string): Action => {
  let keywords = key.split(/\s/)
  const modifiers = []

  // Separate modifiers from keyword list
  for (const modifier of KNOB_MODIFIERS) {
    if (keywords.includes(modifier)) {
      keywords = keywords.filter((keyword) => keyword !== modifier)
      modifiers.push(modifier)
    }
  }

  // What remains should be the button or knob name
  if (keywords.length === 0) {
    throw new Error(`Can't parse modifiers or action in: ${key}`)
  }
  if (keywords.length !== 1 || !Object.keys(KEYS).includes(keywords[0])) {
    // TODO: Check known aliases to show "Did you mean..." message
    throw new Error(`Unknown action: ${keywords.join(" ")}`)
  }

  const { type, actionName } = KEYS[keywords[0]]

  if (type !== "knob" && modifiers.length > 0) {
    throw new Error(`Can't use modifiers (${modifiers}) with ${keywords[0]}`)
  }

  // TODO: Process gold knobs

  const action = [
    type === "button" && "button",
    actionName ? actionName : keywords[0].toLowerCase(),
    modifiers.length > 0 && modifiers.map((m) => m.toLowerCase()).join("-"),
  ]
    .filter(Boolean)
    .join("-")

  if (!ACTIONS.includes(action as Action)) {
    throw new Error(`Can't find icon for: ${key} (Looking for ${action})`)
  }

  return action as Action
}

const parseSequence = (sequence: string): Action[][] =>
  sequence
    .split(/\s?>\s?/)
    .map((chord) => chord.split(/\s?\+\s?/).map(parseAction))

const createImageElement = (action: Action): ElementContent => ({
  type: "element",
  tagName: "img",
  properties: {
    src: icons[action] as unknown as string,
  },
  children: [],
})

const createTextElement = (value: string): ElementContent => ({
  type: "text",
  value,
})

export const displaySequence = (sequence: string): ElementContent[] => {
  const chords = parseSequence(sequence)
  const result: ElementContent[] = []

  chords.forEach((chord, chordIndex) => {
    // Create chord element with interleaved "+" between images
    const chordChildren: ElementContent[] = []
    chord.forEach((action, actionIndex) => {
      chordChildren.push(createImageElement(action))
      if (actionIndex < chord.length - 1) {
        chordChildren.push(createTextElement("+"))
      }
    })

    result.push({
      type: "element",
      tagName: "span",
      properties: { class: "button-chord" },
      children: chordChildren,
    })

    // Add ">" between chords
    if (chordIndex < chords.length - 1) {
      result.push(createTextElement(">"))
    }
  })

  return result
}
