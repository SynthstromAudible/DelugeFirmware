import { z } from "zod"

/** src/data/guidebook-envelope.json: see envelope.ts. */
export const EnvelopeDataSchema = z.object({
  source: z.object({ firmware: z.string(), commit: z.string().nullable() }),
  sampleRate: z.number(),
  stageEnd: z.number(),
  /** Per knob position 0..50: attack, decay and release rates per sample, and the q31 sustain. */
  knobs: z
    .array(z.tuple([z.number(), z.number(), z.number(), z.number()]))
    .length(51),
  shapes: z.object({
    /** decayTableSmall4: the attack is 1 − this. */
    attack: z.array(z.number()).length(257),
    /** decayTableSmall8: the decay toward sustain, and the release. */
    decay: z.array(z.number()).length(257),
  }),
})
export type EnvelopeData = z.infer<typeof EnvelopeDataSchema>
