import { z } from "zod"

export const titleCase = (str: string) =>
  str
    .toLowerCase()
    .replace(/[_-]/g, " ")
    .replace(/\b\w/g, (char) => char.toUpperCase())

const ActionSchema = z.object({
  /**
   * The icon to display for the action.
   */
  icon: z.string().optional(),

  /**
   * The text to display for the action.
   */
  text: z.string().optional(),

  /**
   * The text that the search indexer will see
   */
  searchText: z.string().optional(),

  /**
   * This makes the rendering know that this should look like a pad
   */
  isPad: z.boolean().optional().default(false),

  /**
   * The id of the action if there is more than one.
   * This is used to highlight the correct control.
   * Array, because there can be multiple controls as alternatives.
   *
   * Gold knobs are assigned UPPER or LOWER.
   * Parameter buttons are assigned 1-8.
   * Grid pads have IDs from A1 to P8.
   * Audion and mute pads are AUDITION1-8 and MUTE1-8.
   */
  id: z.string().array().optional(),
})
export type Action = z.infer<typeof ActionSchema>

const ControlDefinitionSchema = z
  .object({
    /**
     * Did you mean... (these we don't accept, but we give a hint)
     */
    didYouMean: z.string().array().optional().default([]),

    toAction: z
      .function()
      .args(
        z.string().describe("The name of the control"),
        z.string().array().describe("List of modifiers"),
        z
          .string()
          .array()
          .describe("All keywords (name and modifiers) in original order"),
      )
      .returns(ActionSchema),

    display: z.string().optional(),
  })
  .strict()

const ControlData = z.record(ControlDefinitionSchema)

const parseModifiers = (
  modifiers: string[],
  before: string[],
  after: string[] = [],
): { before: string[]; after: string[]; remaining: string[] } => {
  const modifiersBefore = []
  const modifiersAfter = []
  let remainingModifiers = modifiers

  for (const modifier of before) {
    if (modifiers.includes(modifier.toLowerCase())) {
      remainingModifiers = remainingModifiers.filter(
        (keyword) => keyword.toLowerCase() !== modifier.toLowerCase(),
      )
      modifiersBefore.push(modifier)
    }
  }

  for (const modifier of after) {
    if (modifiers.includes(modifier.toLowerCase())) {
      remainingModifiers = remainingModifiers.filter(
        (keyword) => keyword.toLowerCase() !== modifier.toLowerCase(),
      )
      modifiersAfter.push(modifier)
    }
  }

  return {
    before: modifiersBefore,
    after: modifiersAfter,
    remaining: remainingModifiers,
  }
}

const labelledButtonToAction =
  (override: Partial<z.infer<typeof ActionSchema>> = {}) =>
  (text: string, modifiers: string[]) => {
    if (modifiers.length > 0) {
      throw new Error(
        `Modifiers are not supported for ${text}. (Modifiers: ${modifiers.join(" ")})`,
      )
    }

    return {
      text,
      rounded: true,
      ...override,
    }
  }

const KNOB_MODIFIERS_BEFORE = ["Press", "Turn"]
const KNOB_MODIFIERS_AFTER = ["Left", "Right"]

const knobToAction =
  (idParser?: (modifiers: string[]) => string[] | undefined) =>
  (text: string, modifiers: string[]): typeof ActionSchema._input => {
    // Separate icon modifiers from the modifier list
    const {
      before: modifiersBefore,
      after: modifiersAfter,
      remaining: remainingModifiers,
    } = parseModifiers(modifiers, KNOB_MODIFIERS_BEFORE, KNOB_MODIFIERS_AFTER)

    const id = idParser ? idParser(remainingModifiers) : undefined

    return {
      icon: [...modifiersBefore, text.toLowerCase(), ...modifiersAfter]
        .filter(Boolean)
        .map((m) => m.toLowerCase())
        .join("-"),
      text: titleCase(remainingModifiers.join(" ")),
      searchText: [
        ...modifiersBefore,
        ...remainingModifiers,
        text.toLowerCase(),
        ...modifiersAfter,
      ]
        .filter(Boolean)
        .map((m) => titleCase(m))
        .join(""),
      id,
    }
  }

// These are the valid buttons/actions that we accept. Come up with a convention and standardize it.
// Aliases help the user find the standard way to refer to a key ("Did you mean ...?")
// The order also standardizes the order that the actions must be defined in a combo.
export const CONTROLS = ControlData.parse({
  // Buttons
  Shift: { toAction: labelledButtonToAction() },
  AffectEntire: {
    toAction: labelledButtonToAction(),
    didYouMean: ["AffectAll", "Affect", "Entire", "All", "Affect_Entire"],
    display: "Affect Entire",
  },
  Clip: { toAction: labelledButtonToAction() },
  CrossScreen: {
    toAction: labelledButtonToAction(),
    didYouMean: ["Cross", "Screen", "Cross_Screen"],
    display: "Cross Screen",
  },
  CV: { toAction: labelledButtonToAction() },
  Keyboard: {
    toAction: labelledButtonToAction({ icon: "button-keyboard" }),
    didYouMean: ["Keys"],
  },
  Kit: { toAction: labelledButtonToAction() },
  Load: {
    toAction: labelledButtonToAction(),
    didYouMean: ["New"],
  },
  MIDI: { toAction: labelledButtonToAction() },
  Record: {
    toAction: labelledButtonToAction(),
    didYouMean: ["Rec"],
  },
  Play: {
    toAction: labelledButtonToAction(),
  },
  Save: {
    toAction: labelledButtonToAction(),
    didYouMean: ["Delete"],
  },
  Scale: { toAction: labelledButtonToAction() },
  Song: { toAction: labelledButtonToAction() },
  SyncScaling: {
    toAction: labelledButtonToAction(),
    didYouMean: ["Sync", "Scaling", "Sync_Scaling"],
    display: "Sync Scaling",
  },
  Synth: { toAction: labelledButtonToAction() },
  TapTempo: {
    toAction: labelledButtonToAction(),
    didYouMean: ["Tap", "Tempo", "Tap_Tempo"],
    display: "Tap Tempo",
  },
  TripletsView: {
    toAction: labelledButtonToAction(),
    didYouMean: ["Triplets", "View", "Triplets_View"],
    display: "Triplets View",
  },

  Learn: { toAction: labelledButtonToAction() },
  Input: { toAction: labelledButtonToAction() },
  LearnInput: {
    toAction: labelledButtonToAction(),
    display: "Learn/Input",
    didYouMean: ["Learn/Input", "Learn_Input"],
  },

  Back: { toAction: labelledButtonToAction() },
  Undo: { toAction: labelledButtonToAction(), didYouMean: ["Redo"] },
  BackUndo: {
    toAction: labelledButtonToAction(),
    display: "Back/Undo",
    didYouMean: ["Back/Undo", "Back_Undo"],
  },

  // TODO: Implement parameter buttons (gold knob selectors)
  Param: { toAction: labelledButtonToAction() },

  // Knobs
  Horizontal: { toAction: knobToAction() },
  Vertical: { toAction: knobToAction() },
  Select: { toAction: knobToAction() },
  Tempo: { toAction: knobToAction() },
  Gold: {
    toAction: knobToAction((modifiers) => {
      if (modifiers.length > 1) {
        throw new Error(
          `Gold knob got invalid modifiers: [${modifiers.join(" ")}]`,
        )
      }
      return modifiers.includes("Upper")
        ? ["Upper"]
        : modifiers.includes("Lower")
          ? ["Lower"]
          : undefined
    }),
  },

  // Pads
  Pad: {
    // TODO: parse pad ID from modifiers
    toAction: (name, modifiers) => {
      return {
        text: titleCase(modifiers.length > 0 ? modifiers.join(" ") : name),
        isPad: true,
        searchText: [...modifiers.map((m) => titleCase(m)), name]
          .filter(Boolean)
          .join(""),
      }
    },
  },
} as typeof ControlData._input)
