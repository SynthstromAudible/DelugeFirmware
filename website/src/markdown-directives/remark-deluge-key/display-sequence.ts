import type { ElementContent } from "hast"
import {
  type Action,
  CONTROLS,
  resolveControl,
  sequenceKeys,
  titleCase,
} from "./controls.ts"

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
  const { name, modifiers, keywords } = resolveControl(key)
  const { toAction, display } = CONTROLS[name]
  return toAction(display || name, modifiers, keywords)
}

const parseSequence = (sequence: string): Action[][] =>
  sequenceKeys(sequence).map((chord) => chord.map(parseAction))

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
