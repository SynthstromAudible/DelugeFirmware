/**
 * The oscillator shapes of the Guidebook's 4.9 figure, as the Deluge plays them at C3.
 *
 * Digital: the firmware's plain shapes. The saw is the crude saw (src/deluge/dsp/oscillators/
 * basic_waves.cpp, renderCrudeSawWave): the oscillator's phase as a signed number, so it rises
 * through each cycle and drops, the reverse of the book's drawing.
 *
 * Analog: the firmware's recorded single cycles, the tables of analogSawTables and
 * analogSquareTables it plays at C3 (src/data/guidebook-osc.json, from online-manual tools/guidebook/osc_data.py),
 * scaled to fill the plot. Values are −1..1; the phase is in cycles.
 */
import { z } from "zod"

export const OscDataSchema = z.object({
  source: z.object({ firmware: z.string(), commit: z.string().nullable() }),
  /** The note whose tables these are (getTableNumber picks a table by pitch). */
  note: z.string(),
  analogSquare: z.object({
    table: z.string(),
    values: z.array(z.number()).length(256),
  }),
  analogSaw: z.object({
    table: z.string(),
    values: z.array(z.number()).length(256),
  }),
})
export type OscData = z.infer<typeof OscDataSchema>

export type OscShapeId =
  "triangle" | "sine" | "saw" | "square" | "analogSaw" | "analogSquare"

/** The book's two groups, in its order, with its names. */
export const SHAPES: Record<
  "digital" | "analog",
  { id: OscShapeId; name: string }[]
> = {
  digital: [
    { id: "triangle", name: "Triangle" },
    { id: "sine", name: "Sine" },
    { id: "saw", name: "Sawtooth" },
    { id: "square", name: "Square" },
  ],
  analog: [
    { id: "analogSaw", name: "Sawtooth" },
    { id: "analogSquare", name: "ASquare" },
  ],
}

const peaks = new WeakMap<number[], number>()
function table(values: number[], phase: number): number {
  let peak = peaks.get(values)
  if (peak === undefined) {
    peak = Math.max(...values.map(Math.abs))
    peaks.set(values, peak)
  }
  const at = phase * values.length
  const i = Math.floor(at)
  const a = values[i % values.length]
  const b = values[(i + 1) % values.length]
  return (a + (b - a) * (at - i)) / peak
}

export function oscWave(
  shape: OscShapeId,
  phase: number,
  data: OscData,
): number {
  const p = phase - Math.floor(phase)
  switch (shape) {
    case "sine":
      return Math.sin(2 * Math.PI * p)
    case "triangle":
      return p < 0.5 ? 4 * p - 1 : 3 - 4 * p
    case "square":
      return p < 0.5 ? 1 : -1
    case "saw":
      return p < 0.5 ? 2 * p : 2 * p - 2
    case "analogSaw":
      return table(data.analogSaw.values, p)
    case "analogSquare":
      return table(data.analogSquare.values, p)
  }
}

/** An oscillator's output, `perCycle` values a cycle. */
export class OscStream {
  private i = 0
  private readonly perCycle: number
  readonly shape: OscShapeId
  private readonly data: OscData

  constructor(
    shape: OscShapeId,
    data: OscData,
    opts: { perCycle?: number } = {},
  ) {
    this.shape = shape
    this.data = data
    this.perCycle = opts.perCycle ?? 64
  }

  next(): number {
    return oscWave(this.shape, this.i++ / this.perCycle, this.data)
  }
}
