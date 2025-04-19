import type { ElementContent } from "hast"
import { type Action, CONTROLS, titleCase } from "./controls.ts"

const icons = Object.fromEntries(
  await Promise.all(
    Object.entries(
      import.meta.glob<ImageMetadata>("../../assets/icons/*.svg", {
        eager: true,
        import: "default",
        query: "?inline",
      }),
    ).map(async ([path, image]) => [
      path.split("/").at(-1)?.split(".")[0],
      image,
    ]),
  ),
)

const parseAction = (key: string): Action => {
  const keywords = key.split(/\s+/).map((keyword) => keyword.toLowerCase())

  const control = Object.entries(CONTROLS).find(([control, { aliases }]) => {
    return (
      keywords.includes(control.toLowerCase()) ||
      aliases.some((alias) => keywords.includes(alias.toLowerCase()))
    )
  })

  if (!control) {
    const didYouMean = Object.entries(CONTROLS).find(([_, { didYouMean }]) => {
      return didYouMean.some((alternative) =>
        keywords.includes(alternative.toLowerCase()),
      )
    })

    if (didYouMean) {
      throw new Error(
        `Unknown action: ${keywords.join(" ")}. Did you mean: ${didYouMean[0]}?`,
      )
    } else {
      throw new Error(`Unknown action: ${keywords.join(" ")}`)
    }
  }

  const [controlName, { toAction, display }] = control

  const modifiers = keywords.filter(
    (keyword) => keyword.toLowerCase() !== controlName.toLowerCase(),
  )

  return toAction(display || controlName, modifiers, keywords)
}

const parseSequence = (sequence: string): Action[][] =>
  sequence
    .split(/\s?>\s?/)
    .map((chord) => chord.split(/\s?\+\s?/).map(parseAction))

const displayAction = ({ icon, text, isPad }: Action): ElementContent[] => {
  if (isPad) {
    return [
      {
        type: "element",
        tagName: "span",
        properties: { class: "button-pad" },
        children: [createTextElement(text || "Pad")],
      },
    ]
  }

  return [
    text && createTextElement(text),
    icon && createImageElement(icon),
  ].filter(Boolean) as ElementContent[]
}

const createImageElement = (icon: string): ElementContent => {
  if (!icons[icon]) {
    throw new Error(`Unknown icon: ${icon}`)
  }

  return {
    type: "element",
    tagName: "img",
    properties: {
      src: icons[icon] as unknown as string,
      alt: titleCase(icon),
      title: titleCase(icon),
    },
    children: [],
  }
}

const createTextElement = (value: string): ElementContent => ({
  type: "text",
  value,
})

export const displaySequence = (sequence: string): ElementContent[] => {
  const chords = parseSequence(sequence)
  const result: ElementContent[] = []

  chords.forEach((chord, chordIndex) => {
    // Create chord element with interleaved "+" between keys/modifiers
    const chordChildren: ElementContent[] = []
    chord.forEach((action, actionIndex) => {
      chordChildren.push(...displayAction(action))
      if (actionIndex < chord.length - 1) {
        chordChildren.push(createTextElement(" + "))
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
