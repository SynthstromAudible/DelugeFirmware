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

  const control = Object.entries(CONTROLS).find(([control]) => {
    return keywords.includes(control.toLowerCase())
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

const parseSequence = (sequence: string): Action[][] => {
  const invalidCharacters = [...sequence.matchAll(/[^a-zA-Z\d>+/\s]/g)]

  if (invalidCharacters.length)
    throw new Error(
      `Invalid character(s) found: ${JSON.stringify(invalidCharacters.flat())}`,
    )

  return sequence
    .split(/\s?>\s?/)
    .map((chord) => chord.split(/\s?\+\s?/).map(parseAction))
}

const displayAction = ({
  icon,
  labelBefore,
  label,
  isPad,
}: Action): ElementContent[] => {
  if (isPad) {
    return [
      {
        type: "element",
        tagName: "span",
        properties: { class: "action pad" },
        children: [createTextElement(label || "Pad")],
      },
    ]
  }

  return [
    {
      type: "element",
      tagName: "span",
      properties: { class: "action" },
      children: [
        labelBefore && createTextElement(`${labelBefore} `),
        icon
          ? createImageElement(icon, label)
          : label && createTextElement(label),
      ].filter(Boolean) as ElementContent[],
    },
  ]
}

const createImageElement = (icon: string, label?: string): ElementContent => {
  if (!icons[icon]) {
    throw new Error(`Unknown icon: ${icon}`)
  }

  return {
    type: "element",
    tagName: "span",
    properties: { class: "icon-with-label" },
    children: [
      {
        type: "element",
        tagName: "img",
        properties: {
          src: icons[icon] as unknown as string,
          alt: titleCase(icon),
          title: titleCase(icon),
        },
        children: [],
      },
      label && createTextElement(label),
    ].filter(Boolean) as ElementContent[],
  }
}

const createTextElement = (value: string): ElementContent => ({
  type: "text",
  value,
})

export const displaySequence = (
  sequence: string,
): { elements: ElementContent[]; searchText: string } => {
  const chords = parseSequence(sequence)
  const elements: ElementContent[] = []
  let searchText = ""

  chords.forEach((chord, chordIndex) => {
    // Create a chord element with "+" inserted between actions
    const chordChildren: ElementContent[] = []
    chord.forEach((action, actionIndex) => {
      chordChildren.push(...displayAction(action))
      searchText += action.searchText
      if (actionIndex < chord.length - 1) {
        chordChildren.push(createTextElement("\xA0+ "))
        searchText += "\xA0+ "
      }
    })

    elements.push({
      type: "element",
      tagName: "span",
      properties: { class: "button-chord" },
      children: chordChildren,
    })

    // Add ">" between chords
    if (chordIndex < chords.length - 1) {
      elements.push(createTextElement("\xA0> "))
      searchText += "\xA0> "
    }
  })

  return {
    elements: elements,
    searchText: searchText,
  }
}
