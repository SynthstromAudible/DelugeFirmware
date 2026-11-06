import type { ShapeId } from "./lfo.ts"

/** Values per LFO cycle in the plots. */
export const PER_CYCLE = 64
/** Values across a plot: four cycles, so the random shapes show several steps. */
export const WINDOW = PER_CYCLE * 4
/**
 * Cycles run before the first trace, so the random shapes are under way: Random Walk takes about
 * sixteen cycles to wander out from zero to its usual range.
 */
export const PREROLL = PER_CYCLE * 48
/** Values a second as the signals advance: half a cycle a second, slowed for reading. */
export const SPEED = PER_CYCLE / 2
/** Random Walk's steps are a few percent of full scale (it settles within about ±8 %): drawn taller
 * to be seen, and labelled so. */
export const SCALE: Record<ShapeId, number> = {
  sine: 1,
  triangle: 1,
  square: 1,
  saw: 1,
  sampleHold: 1,
  randomWalk: 6,
  warbler: 1,
}
