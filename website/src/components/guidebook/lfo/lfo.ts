/**
 * The Deluge's LFO shapes, as the firmware draws them (upstream src/deluge/modulation/lfo.h and
 * src/deluge/util/waves.h), for the Guidebook's LFO figure. Values are −1..1 (the firmware's
 * full int32 range); the phase is in cycles.
 *
 * The random shapes keep the firmware's rules: Sample & Hold takes a new value each cycle; Random
 * Walk starts within ±5 % and each cycle steps by up to ±5 %, less a sixteenth of where it is;
 * Warbler, at twice the rate, heads for a new random target each of its cycles, accelerating
 * toward it once per audio block (128 samples), so how far it gets depends on the rate.
 */

export type ShapeId =
  | "sine"
  | "triangle"
  | "square"
  | "saw"
  | "sampleHold"
  | "randomWalk"
  | "warbler"

/**
 * In the order of the Deluge's LFO TYPE menu. `about` is the book's own description, where it
 * gave one.
 */
export const SHAPES: { id: ShapeId; name: string; about?: string }[] = [
  { id: "sine", name: "Sine" },
  { id: "triangle", name: "Triangle" },
  { id: "square", name: "Square" },
  { id: "saw", name: "Saw" },
  {
    id: "sampleHold",
    name: "Sample & Hold",
    about: "Random unique value on each trigger.",
  },
  {
    id: "randomWalk",
    name: "Random Walk",
    about:
      "Start on zero and takes small incremental steps walking up / down from the previous value when triggered.",
  },
  { id: "warbler", name: "Warbler" },
]

/** The fixed shapes at phase 0..1. */
export function wave(shape: ShapeId, phase: number): number {
  const p = phase - Math.floor(phase)
  switch (shape) {
    case "sine":
      return Math.sin(2 * Math.PI * p)
    case "triangle":
      // getTriangle: from the bottom, to the top at half a cycle
      return p < 0.5 ? 4 * p - 1 : 3 - 4 * p
    case "square":
      return p < 0.5 ? 1 : -1
    case "saw":
      // the phase as a signed number: up from 0, wrapping to the bottom at half a cycle
      return p < 0.5 ? 2 * p : 2 * p - 2
    default:
      return 0
  }
}

/** The LFO rate knob 0..50 (unsynced) in Hz: getExp(121739, preset) phase increment per sample. */
export function rateHz(knob: number): number {
  const word =
    knob >= 50
      ? 2147483647
      : knob <= 0
        ? -2147483648
        : knob * 85899345 - 2 ** 31
  return (121739 * 2 ** (word / 4 / 2 ** 26 + 2) * 44100) / 2 ** 32
}

const SAMPLE_RATE = 44100
const BLOCK = 128 // SSI_TX_BUFFER_NUM_SAMPLES

/** A repeatable random source, 0..1 (mulberry32). */
export function seeded(seed: number): () => number {
  let a = seed >>> 0
  return () => {
    a = (a + 0x6d2b79f5) >>> 0
    let t = a
    t = Math.imul(t ^ (t >>> 15), t | 1)
    t ^= t + Math.imul(t ^ (t >>> 7), t | 61)
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296
  }
}

const clamp = (v: number) => Math.max(-1, Math.min(1, v))

/**
 * An LFO's output, `perCycle` values per cycle, from note on (or playback start). `rate` (the knob,
 * 0..50) matters only to the Warbler, whose glide is worked out per audio block.
 */
export class LfoStream {
  private i = 0
  private hold = 0
  private speed = 0
  /** The Warbler's current target. */
  target = 0
  private blockPhase = 0
  private readonly perCycle: number
  private readonly blocksPerCycle: number

  constructor(
    readonly shape: ShapeId,
    private readonly random: () => number,
    opts: { perCycle?: number; rate?: number } = {},
  ) {
    this.perCycle = opts.perCycle ?? 64
    this.blocksPerCycle = Math.max(
      1,
      SAMPLE_RATE / (rateHz(opts.rate ?? WARBLER_RATE) * BLOCK),
    )
  }

  next(): number {
    const phase = this.i / this.perCycle
    const wrapped =
      this.i > 0 && Math.floor(phase) > Math.floor((this.i - 1) / this.perCycle)
    let value: number
    switch (this.shape) {
      case "sampleHold":
        if (this.i === 0 || wrapped) this.hold = this.random() * 2 - 1
        value = this.hold
        break
      case "randomWalk":
        // range = 2^32 / 20: a step of (range / 2 − random % range), i.e. ±5 % of full scale
        if (this.i === 0) this.hold = 0.05 - this.random() * 0.1
        else if (wrapped)
          this.hold = clamp(
            this.hold - this.hold / 16 + 0.05 - this.random() * 0.1,
          )
        value = this.hold
        break
      case "warbler":
        this.warble(phase)
        value = this.hold
        break
      default:
        value = wave(this.shape, phase)
    }
    this.i++
    return value
  }

  /** Run the Warbler's audio blocks up to `phase`: it runs at twice the rate. */
  private warble(phase: number) {
    const step = 1 / this.blocksPerCycle
    const perWarble = this.blocksPerCycle / 2
    while (this.blockPhase + step <= phase + 1e-12) {
      const before = this.blockPhase * 2
      const after = (this.blockPhase + step) * 2
      if (Math.floor(after + 1e-12) > Math.floor(before + 1e-12)) {
        this.target = this.random() * 2 - 1
        this.speed = 0
      }
      this.speed += (this.target - this.hold) / (256 * perWarble)
      this.hold = clamp(this.hold + this.speed)
      this.blockPhase += step
    }
  }
}

/** The rate the figure's Warbler runs at: slow enough that it reaches, and swings past, its targets. */
export const WARBLER_RATE = 10
