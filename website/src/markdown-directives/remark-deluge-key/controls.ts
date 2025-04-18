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
     * We accept aliases
     */
    aliases: z.string().array().optional().default([]),

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

export const KNOB_MODIFIERS = ["Press", "Turn", "Left", "Right"]

const labelledButtonToAction =
  (
    override: Partial<z.infer<typeof ActionSchema>> = {},
  ) =>
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

const knobToAction =
  (idParser?: (modifiers: string[]) => string[] | undefined) =>
  (text: string, modifiers: string[]) => {
    // Separate icon modifiers from modifier list
    const iconModifiers = []
    let remainingModifiers = modifiers
    for (const modifier of KNOB_MODIFIERS) {
      if (modifiers.includes(modifier.toLowerCase())) {
        remainingModifiers = remainingModifiers.filter(
          (keyword) => keyword.toLowerCase() !== modifier.toLowerCase(),
        )
        iconModifiers.push(modifier)
      }
    }

    const id = idParser ? idParser(remainingModifiers) : undefined

    const icon = [
      text.toLowerCase(),
      iconModifiers.length > 0 &&
        iconModifiers.map((m) => m.toLowerCase()).join("-"),
    ]
      .filter(Boolean)
      .join("-")

    return {
      icon,
      text: titleCase(remainingModifiers.join(" ")),
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
  Learn: {
    toAction: labelledButtonToAction(),
    didYouMean: ["Input"],
  },
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
  Back: {
    toAction: labelledButtonToAction(),
    didYouMean: ["Undo", "Redo", "Back_Undo", "Back_Redo"],
    display: "Back",
  },
  Param: { toAction: labelledButtonToAction() }, // TODO: ids

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
    toAction: (name, modifiers) => ({
      text: titleCase(modifiers.length > 0 ? modifiers.join(" ") : name),
      isPad: true,
      // TODO: parse IDs from modifiers
    }),
  },
} as typeof ControlData._input)
