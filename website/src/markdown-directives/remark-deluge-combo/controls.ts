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
  })
  .strict()

const ControlData = z.record(ControlDefinitionSchema)

export const KNOB_MODIFIERS = ["PRESS", "TURN", "LEFT", "RIGHT"]

const labelledButtonToAction =
  (
    /**
     * This is used to process controls that could be written as multiple words.
     */
    acceptedIgnoredModifiers: string[] = [],
    override: Partial<z.infer<typeof ActionSchema>> = {},
  ) =>
  (name: string, modifiers: string[]) => {
    // Remove accepted modifiers from the modifier list
    let remainingModifiers = modifiers
    for (const modifier of acceptedIgnoredModifiers) {
      if (modifiers.includes(modifier)) {
        remainingModifiers = remainingModifiers.filter(
          (keyword) => keyword !== modifier,
        )
      }
    }

    if (remainingModifiers.length > 0) {
      throw new Error(
        `Modifiers are not supported for ${name}. (Modifiers: ${remainingModifiers.join(" ")})`,
      )
    }

    return {
      text: titleCase(name),
      rounded: true,
      ...override,
    }
  }

const knobToAction =
  (idParser?: (modifiers: string[]) => string[] | undefined) =>
  (name: string, modifiers: string[]) => {
    // Separate icon modifiers from modifier list
    const iconModifiers = []
    let remainingModifiers = modifiers
    for (const modifier of KNOB_MODIFIERS) {
      if (modifiers.includes(modifier)) {
        remainingModifiers = remainingModifiers.filter(
          (keyword) => keyword !== modifier,
        )
        iconModifiers.push(modifier)
      }
    }

    const id = idParser ? idParser(remainingModifiers) : undefined

    const icon = [
      name.toLowerCase(),
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
  SHIFT: { toAction: labelledButtonToAction() },
  AFFECT_ENTIRE: {
    aliases: ["AFFECT"],
    toAction: labelledButtonToAction(["AFFECT", "ENTIRE"]),
    didYouMean: ["AFFECT_ALL", "ALL"],
  },
  CLIP: { toAction: labelledButtonToAction() },
  CROSS_SCREEN: {
    aliases: ["CROSS"],
    toAction: labelledButtonToAction(["CROSS", "SCREEN"]),
  },
  CV: { toAction: labelledButtonToAction() },
  KEYBOARD: {
    aliases: ["KEYS"],
    toAction: labelledButtonToAction([], { icon: "button-keyboard" }),
  },
  KIT: { toAction: labelledButtonToAction() },
  LEARN: {
    aliases: ["INPUT"],
    toAction: labelledButtonToAction(),
  },
  LOAD: {
    // aliases: ["NEW"],
    toAction: labelledButtonToAction(),
  },
  MIDI: { toAction: labelledButtonToAction() },
  RECORD: {
    aliases: ["REC"],
    toAction: labelledButtonToAction(),
  },
  PLAY: {
    toAction: labelledButtonToAction(),
  },
  SAVE: {
    // aliases: ["DELETE"],
    toAction: labelledButtonToAction(),
  },
  SCALE: { toAction: labelledButtonToAction() },
  SONG: { toAction: labelledButtonToAction() },
  SYNC_SCALING: {
    aliases: ["SYNC"],
    toAction: labelledButtonToAction(["SYNC", "SCALING"]),
  },
  SYNTH: { toAction: labelledButtonToAction() },
  TAP_TEMPO: {
    aliases: ["TAP"],
    toAction: labelledButtonToAction(["TAP", "TEMPO"]),
  },
  TRIPLETS_VIEW: {
    aliases: ["TRIPLETS"],
    toAction: labelledButtonToAction(["TRIPLETS", "VIEW"]),
  },
  BACK: {
    aliases: ["UNDO" /* , "REDO" */],
    toAction: labelledButtonToAction(),
  },
  PARAM: { toAction: labelledButtonToAction() }, // TODO: ids

  // Knobs
  HORIZONTAL: { toAction: knobToAction() },
  VERTICAL: { toAction: knobToAction() },
  SELECT: { toAction: knobToAction() },
  TEMPO: { toAction: knobToAction() },
  GOLD: {
    toAction: knobToAction((modifiers) => {
      if (modifiers.length > 1) {
        throw new Error(
          `Gold knob takes only one Upper or Lower modifier. Got: [${modifiers.join(" ")}]`,
        )
      }
      return modifiers.includes("UPPER")
        ? ["UPPER"]
        : modifiers.includes("LOWER")
          ? ["LOWER"]
          : undefined
    }),
  },

  // Pads
  PAD: {
    toAction: (name, modifiers) => ({
      text: titleCase(modifiers.length > 0 ? modifiers.join(" ") : name),
      isPad: true,
      // TODO: parse IDs from modifiers
    }),
  },
} as typeof ControlData._input)
