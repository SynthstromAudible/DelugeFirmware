import { z } from "zod"

/**
 * The control map of the Guidebook's front-panel drawing (src/data/guidebook-panel.json),
 * generated with panel.svg by the panel tool in the online-manual repo
 * (tools/guidebook/panelbuild.py). Coordinates are px in the 5808x3952 book raster.
 */
const Circle = z.object({
  kind: z.enum(["button", "encoder", "pad"]),
  shape: z.literal("circle"),
  cx: z.number(),
  cy: z.number(),
  r: z.number(),
})

const Rect = z.object({
  kind: z.enum(["button", "encoder", "pad"]),
  shape: z.literal("rect"),
  x: z.number(),
  y: z.number(),
  w: z.number(),
  h: z.number(),
  rx: z.number(),
  cx: z.number(),
  cy: z.number(),
})

const Box = z.tuple([z.number(), z.number(), z.number(), z.number()])

export const PanelMapSchema = z.object({
  viewBox: Box,
  controls: z.record(z.string(), z.discriminatedUnion("shape", [Circle, Rect])),
  /** Label printed under exactly one pad -> that pad's id. */
  padLabels: z.record(z.string(), z.string()),
  /** Labels printed under several pads: never guessed. */
  ambiguousPadLabels: z.record(z.string(), z.array(z.string())),
  regions: z.record(z.string(), Box),
  unread: z.array(z.object({ text: z.string(), bbox: Box })),
  problems: z.array(z.string()),
})

export type PanelMap = z.infer<typeof PanelMapSchema>
export type PanelControl = PanelMap["controls"][string]
export type Box = z.infer<typeof Box>
