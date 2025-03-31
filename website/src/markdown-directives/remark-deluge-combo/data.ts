import { z } from "zod"

const KeyDataItem = z
  .object({
    aliases: z.string().array().optional().default([]),
    /**
     * Action name maps from markdown syntax to the file name.
     * Do we want to rename the files instead?
     */
    actionName: z.string().optional(),
    type: z.enum(["button", "knob"]).optional().default("button"), // TODO: Gold
  })
  .strict()

const KeyData = z.record(KeyDataItem)

export const KNOB_MODIFIERS = ["PRESS", "TURN", "LEFT", "RIGHT"]

// These are the valid buttons/actions that we accept. Come up with a convention and standardize it.
// Aliases help the user find the standard way to refer to a key ("Did you mean ...?")
// The order also standardizes the order that the actions must be defined in a combo.
export const KEYS = KeyData.parse({
  SHIFT: {},
  LOAD: {},
  BACK: {
    actionName: "undo-back",
  },
  TEMPO: {
    type: "knob",
    actionName: "bpm",
  },
  SELECT: {
    actionName: "sel",
    type: "knob",
  },
  V: {
    type: "knob",
    aliases: ["UD"],
  },
  H: {
    type: "knob",
    aliases: ["<>"],
  },
  // TODO: Add the rest of the buttons to the syntax
})

export const ACTIONS = [
  "audition-pad",
  "button-shift",
  "bpm-press-turn",
  "bpm-press",
  "bpm-turn",
  "bpm",
  "button-affect-all",
  "button-clip",
  "button-cross-screen",
  "button-cv",
  "button-keyboard",
  "button-kit",
  "button-learn-input",
  "button-load",
  "button-midi",
  "button-param",
  "button-rec",
  "button-save",
  "button-scale",
  "button-shift",
  "button-song",
  "button-sync-scale",
  "button-synth",
  "button-tap-tempo",
  "button-triplet-view",
  "button-undo-back",
  "gld-press-turn",
  "gld-press",
  "gld-turn",
  "gld",
  "h-left",
  "h-press-turn",
  "h-press",
  "h-right",
  "h-turn",
  "h",
  "lwr-press-turn",
  "lwr-press",
  "lwr-turn",
  "lwr",
  "main-pad-grid",
  "mute-pad",
  "pad-column-press",
  "pad-play",
  "pad-press",
  "pad-row-press",
  "sel-left",
  "sel-press-hold",
  "sel-press-turn",
  "sel-press",
  "sel-right",
  "sel-turn",
  "sel",
  "upr-press-turn",
  "upr-press",
  "upr-turn",
  "upr",
  "v-left",
  "v-press-turn",
  "v-press",
  "v-right",
  "v-turn",
  "v",
] as const
export type Action = (typeof ACTIONS)[number]
