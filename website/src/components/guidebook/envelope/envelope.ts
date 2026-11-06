/**
 * The Deluge's envelope, as the firmware runs it (src/deluge/modulation/envelope.cpp), for the
 * Guidebook's envelope explorer. Rates and curve tables come from src/data/guidebook-envelope.json,
 * generated from upstream firmware source by the online-manual repo's
 * tools/guidebook/envelope_data.py. Levels are 0..1 (the firmware's 0..2^31).
 *
 * Left out: the sustain smoothing (it only matters while sustain is being modulated) and the fast
 * release used when voices are stolen.
 */
import type { EnvelopeData } from "./envelope-data.ts"

export type { EnvelopeData }

/** Knob positions, 0..50 as on the Deluge. */
export type Settings = {
  attack: number
  decay: number
  sustain: number
  release: number
}
export type Stage = "attack" | "decay" | "sustain" | "release" | "off"
export type Point = { t: number; level: number; stage: Stage }
/** Stage lengths in seconds, and the sustain level 0..1. */
export type Timing = {
  attack: number
  decay: number
  sustain: number
  release: number
}

const Q31 = 2147483647

const knob = (n: number) => Math.max(0, Math.min(50, Math.round(n)))

export function timing(data: EnvelopeData, s: Settings): Timing {
  // A stage ends once its position, advanced by the rate every sample, reaches stageEnd.
  const seconds = (rate: number) =>
    Math.ceil(data.stageEnd / rate) / data.sampleRate
  return {
    attack: seconds(data.knobs[knob(s.attack)][0]),
    decay: seconds(data.knobs[knob(s.decay)][1]),
    sustain: data.knobs[knob(s.sustain)][3] / Q31,
    release: seconds(data.knobs[knob(s.release)][2]),
  }
}

/** A curve table (257 entries, 0..65535) at fraction 0..1 of its stage, linearly interpolated. */
function shape(table: number[], fraction: number): number {
  const x = Math.max(0, Math.min(1, fraction)) * 256
  const i = Math.min(255, Math.floor(x))
  const f = x - i
  return (table[i] * (1 - f) + table[i + 1] * f) / 65535
}

function held(data: EnvelopeData, t: Timing, u: number) {
  if (u < t.attack)
    return {
      stage: "attack" as const,
      level: 1 - shape(data.shapes.attack, u / t.attack),
    }
  u -= t.attack
  if (u < t.decay)
    return {
      stage: "decay" as const,
      level:
        t.sustain + shape(data.shapes.decay, u / t.decay) * (1 - t.sustain),
    }
  // A zero sustain has nothing to hold: the envelope is over.
  if (t.sustain === 0) return { stage: "off" as const, level: 0 }
  return { stage: "sustain" as const, level: t.sustain }
}

/** The envelope at `t` seconds after note on, the note let go at `gate` seconds. */
export function levelAt(
  data: EnvelopeData,
  s: Settings,
  t: number,
  gate: number,
): { stage: Stage; level: number } {
  const tm = timing(data, s)
  if (t < gate) return held(data, tm, t)
  // The release starts from wherever the envelope was when the note was let go.
  const from = held(data, tm, gate)
  const u = t - gate
  if (from.stage === "off" || u >= tm.release) return { stage: "off", level: 0 }
  return {
    stage: "release",
    level: shape(data.shapes.decay, u / tm.release) * from.level,
  }
}

/** A note length that shows every stage: through the decay, then held a while. */
export function defaultGate(data: EnvelopeData, s: Settings): number {
  const t = timing(data, s)
  return (
    t.attack + t.decay + Math.max((t.attack + t.decay + t.release) / 3, 0.01)
  )
}

/** When the envelope falls silent for a note let go at `gate`. */
export function endTime(data: EnvelopeData, s: Settings, gate: number): number {
  const t = timing(data, s)
  if (t.sustain === 0 && gate >= t.attack + t.decay) return t.attack + t.decay
  return gate + t.release
}

/** The envelope as points, each stage sampled finely enough to draw however short it is. */
export function curve(
  data: EnvelopeData,
  s: Settings,
  gate: number,
  perStage = 64,
): Point[] {
  const t = timing(data, s)
  const end = endTime(data, s, gate)
  const marks = [0, t.attack, t.attack + t.decay, gate, gate + t.release, end]
    .filter((m) => m <= end)
    .sort((a, b) => a - b)
  const times = new Set<number>()
  for (let k = 0; k < marks.length - 1; k++) {
    const [a, b] = [marks[k], marks[k + 1]]
    if (b <= a) continue
    for (let i = 0; i < perStage; i++) times.add(a + ((b - a) * i) / perStage)
    // the level just before the stage ends, then the next stage's start
    times.add(b - (b - a) * 1e-6)
  }
  const pts = [...times]
    .sort((a, b) => a - b)
    .map((u) => ({ t: u, ...levelAt(data, s, u, gate) }))
  pts.push({ t: end, stage: "off", level: 0 })
  return pts
}

export function formatTime(seconds: number): string {
  if (seconds < 0.001) return `${(seconds * 1000).toFixed(1)} ms`
  if (seconds < 1) return `${Math.round(seconds * 1000)} ms`
  if (seconds < 10) return `${seconds.toFixed(2)} s`
  return `${seconds.toFixed(1)} s`
}

/** Round tick times from 0 to at most `max`: steps of 1, 2 or 5 × 10^k, at most `count` ticks. */
export function niceTicks(max: number, count = 7): number[] {
  const base = 10 ** Math.floor(Math.log10(max / (count - 1)))
  const step =
    [1, 2, 5, 10, 20].map((m) => m * base).find((st) => max / st < count) ??
    base * 50
  const ticks: number[] = []
  for (let i = 0; i * step <= max + step * 1e-9; i++)
    ticks.push(Number((i * step).toPrecision(10)))
  return ticks
}

/**
 * The sustain readout's caption: the level for volume; for other destinations, where it sits
 * against the destination's own value (25 is on it), as a share of the mod depth.
 */
export function sustainCaption(
  t: Timing,
  s: Settings,
  destination: "volume" | "other",
): string {
  if (destination === "volume") return `${Math.round(t.sustain * 100)} % level`
  if (s.sustain === 25) return "= dest. value"
  const share = Math.round(Math.abs(t.sustain - 0.5) * 200)
  return `${s.sustain > 25 ? "+" : "−"}${share} % of depth`
}
