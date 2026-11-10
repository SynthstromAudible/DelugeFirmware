import { z } from "zod"
import type { PanelMap } from "./panel-map.ts"

/**
 * What the Deluge shows after each step of a figure: pad colours and lit buttons.
 * Hand-authored for now (`source: "hand"`); firmware captures will fill the same shape later
 * (`source: "capture"`).
 */
export const LED_COLOURS = [
  "white",
  "red",
  "green",
  "blue",
  "yellow",
  "magenta",
  "cyan",
  "orange",
  "off",
] as const

const LedColour = z.enum(LED_COLOURS)
const Led = z.union([
  LedColour,
  z.object({ colour: LedColour, blink: z.literal(true) }),
])

export const FrameSchema = z
  .object({
    pads: z
      .record(z.string(), z.string().regex(/^#[0-9a-fA-F]{6}$/))
      .optional(),
    leds: z.record(z.string(), Led).optional(),
    /** For authors: what this frame shows. Not rendered. */
    note: z.string().optional(),
  })
  .strict()

export const FrameFileSchema = z
  .object({
    figure: z.string(),
    source: z.enum(["hand", "capture"]),
    window: z.union([
      z.string(),
      z.tuple([z.number(), z.number(), z.number(), z.number()]),
    ]),
    steps: z.array(FrameSchema).min(1),
  })
  .strict()

export type Frame = z.infer<typeof FrameSchema>
export type FrameFile = z.infer<typeof FrameFileSchema>
export type Led = z.infer<typeof Led>

/**
 * A figure's frame file, checked against the figure, its Steps and the panel.
 * `stepCount` is the number of steps driving the figure (undefined: no Steps, so one frame).
 */
export function validateFrames(
  raw: unknown,
  {
    figure,
    stepCount,
    panel,
  }: { figure: string; stepCount: number | undefined; panel: PanelMap },
): FrameFile {
  const file = FrameFileSchema.parse(raw)
  if (file.figure !== figure)
    throw new Error(`frame file is for ${file.figure}, not ${figure}`)
  if (stepCount === undefined && file.steps.length > 1)
    throw new Error(
      `${figure}: ${file.steps.length} frames but no Steps bound to the figure — is a heading between them?`,
    )
  if (stepCount !== undefined && file.steps.length !== stepCount)
    throw new Error(
      `${figure}: ${file.steps.length} frames but ${stepCount} steps — one frame per step`,
    )
  if (typeof file.window === "string" && !panel.regions[file.window])
    throw new Error(
      `${figure}: no panel region "${file.window}" (have: ${Object.keys(panel.regions).join(", ")})`,
    )
  file.steps.forEach((frame, i) => {
    for (const id of [
      ...Object.keys(frame.pads ?? {}),
      ...Object.keys(frame.leds ?? {}),
    ]) {
      if (!panel.controls[id])
        throw new Error(
          `${figure} frame ${i + 1}: no control ${id} on the panel`,
        )
    }
  })
  return file
}
