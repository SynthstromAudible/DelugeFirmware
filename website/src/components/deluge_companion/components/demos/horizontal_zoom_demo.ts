import { Action } from "../../data/actions.js"
import { Control } from "../../data/targets.js"
import { isStep, type Step, type StepOrSubstep } from "../../types/shortcut.js"

const GRID_ROWS = 8
const DEMO_PIN_COL = 1
const DEMO_HOLD_START_MS = 520
const DEMO_HOLD_ZOOMED_MS = 520
const DEMO_HOLD_RETURN_MS = 520
const SCALE_16TH = 1
const SCALE_32TH = 2

// Public timing/scale constants consumed by UI and tests.
export const HORIZONTAL_ZOOM_INITIAL_SCALE = SCALE_16TH
export const HORIZONTAL_ZOOM_TRANSITION_MS = 360

// Scripted phases of the looping zoom animation.
export type ZoomDemoStep =
  | {
      kind: "hold"
      scale: number
      durationMs: number
      label: "8th" | "16th" | "32nd" | "64th"
    }
  | {
      kind: "transition"
      fromScale: number
      toScale: number
      fromLabel: "8th" | "16th" | "32nd" | "64th"
      toLabel: "8th" | "16th" | "32nd" | "64th"
    }

export type DemoCell = {
  row: number
  col: number
  intensity: number
  fromIntensity: number
  toIntensity: number
}

// Mutable runtime state for animation progression.
export type HorizontalZoomDemoRuntime = {
  stepIndex: number
  stepElapsedMs: number
  scale: number
  fromScale: number
  toScale: number
  renderBlend: number
  direction: 1 | -1
}

// Runtime controls returned to caller.
export type HorizontalZoomDemoLoop = {
  start: () => void
  stop: () => void
  isRunning: () => boolean
}

type DemoNote = {
  row: number
  col: number
}

// Starting pattern matched to reference video around 6s.
const DEMO_NOTES: DemoNote[] = [
  { row: 8, col: 1 },
  { row: 8, col: 2 },
  { row: 8, col: 3 },
  { row: 8, col: 4 },
  { row: 8, col: 5 },
  { row: 8, col: 6 },
  { row: 8, col: 7 },
  { row: 8, col: 8 },
  { row: 7, col: 2 },
  { row: 7, col: 3 },
  { row: 7, col: 4 },
  { row: 7, col: 5 },
  { row: 7, col: 6 },
  { row: 7, col: 7 },
  { row: 7, col: 8 },
  { row: 6, col: 3 },
  { row: 6, col: 4 },
  { row: 6, col: 5 },
  { row: 6, col: 6 },
  { row: 6, col: 7 },
  { row: 6, col: 8 },
  { row: 5, col: 4 },
  { row: 5, col: 5 },
  { row: 5, col: 6 },
  { row: 5, col: 7 },
  { row: 5, col: 8 },
  { row: 4, col: 5 },
  { row: 4, col: 6 },
  { row: 4, col: 7 },
  { row: 4, col: 8 },
  { row: 3, col: 6 },
  { row: 3, col: 7 },
  { row: 3, col: 8 },
  { row: 2, col: 7 },
  { row: 2, col: 8 },
  { row: 1, col: 8 },
]

export const HORIZONTAL_ZOOM_STEPS: ZoomDemoStep[] = [
  {
    kind: "hold",
    scale: SCALE_16TH,
    durationMs: DEMO_HOLD_START_MS,
    label: "16th",
  },
  {
    kind: "transition",
    fromScale: SCALE_16TH,
    toScale: SCALE_32TH,
    fromLabel: "16th",
    toLabel: "32nd",
  },
  {
    kind: "hold",
    scale: SCALE_32TH,
    durationMs: DEMO_HOLD_ZOOMED_MS,
    label: "32nd",
  },
  {
    kind: "transition",
    fromScale: SCALE_32TH,
    toScale: SCALE_16TH,
    fromLabel: "32nd",
    toLabel: "16th",
  },
  {
    kind: "hold",
    scale: SCALE_16TH,
    durationMs: DEMO_HOLD_RETURN_MS,
    label: "16th",
  },
]

// Creates deterministic initial runtime state.
export function createInitialHorizontalZoomDemoRuntime(): HorizontalZoomDemoRuntime {
  return {
    stepIndex: 0,
    stepElapsedMs: 0,
    scale: HORIZONTAL_ZOOM_INITIAL_SCALE,
    fromScale: HORIZONTAL_ZOOM_INITIAL_SCALE,
    toScale: HORIZONTAL_ZOOM_INITIAL_SCALE,
    renderBlend: 0,
    direction: 1,
  }
}

// Advances runtime by dt, handling hold/transition phases and wrapping step index.
export function advanceHorizontalZoomDemoRuntime(
  runtime: HorizontalZoomDemoRuntime,
  dtMs: number,
): HorizontalZoomDemoRuntime {
  const next: HorizontalZoomDemoRuntime = {
    ...runtime,
    stepElapsedMs: runtime.stepElapsedMs + dtMs,
  }
  const step = HORIZONTAL_ZOOM_STEPS[next.stepIndex]

  // Branch: fixed-scale hold phase.
  if (step.kind === "hold") {
    next.scale = step.scale
    next.fromScale = step.scale
    next.toScale = step.scale
    next.renderBlend = 0
    next.direction = 1

    // Branch: advance to next scripted phase.
    if (next.stepElapsedMs >= step.durationMs) {
      next.stepElapsedMs -= step.durationMs
      next.stepIndex = (next.stepIndex + 1) % HORIZONTAL_ZOOM_STEPS.length
    }
  } else {
    // Branch: interpolation phase between two scales.
    const t = Math.min(1, next.stepElapsedMs / HORIZONTAL_ZOOM_TRANSITION_MS)
    const eased = 0.5 - 0.5 * Math.cos(Math.PI * t)

    next.fromScale = step.fromScale
    next.toScale = step.toScale
    next.renderBlend = eased
    next.scale = next.fromScale + (next.toScale - next.fromScale) * eased
    next.direction = next.toScale > next.fromScale ? 1 : -1

    // Branch: transition completed, move to next phase.
    if (next.stepElapsedMs >= HORIZONTAL_ZOOM_TRANSITION_MS) {
      next.stepElapsedMs -= HORIZONTAL_ZOOM_TRANSITION_MS
      next.stepIndex = (next.stepIndex + 1) % HORIZONTAL_ZOOM_STEPS.length
    }
  }

  return next
}

// Creates RAF-driven loop that publishes runtime frames.
export function createHorizontalZoomDemoLoop(
  onFrame: (runtime: HorizontalZoomDemoRuntime) => void,
): HorizontalZoomDemoLoop {
  let runtime = createInitialHorizontalZoomDemoRuntime()
  let rafId: number | undefined
  let lastTs: number | undefined
  let running = false

  const tick = (ts: number) => {
    // Branch: ignore queued frames after stop.
    if (!running) {
      return
    }

    const dt = lastTs === undefined ? 16 : Math.min(40, ts - lastTs)
    lastTs = ts
    runtime = advanceHorizontalZoomDemoRuntime(runtime, dt)
    onFrame(runtime)
    rafId = requestAnimationFrame(tick)
  }

  return {
    start() {
      this.stop()
      runtime = createInitialHorizontalZoomDemoRuntime()
      onFrame(runtime)
      running = true
      rafId = requestAnimationFrame(tick)
    },
    stop() {
      if (rafId !== undefined) {
        cancelAnimationFrame(rafId)
        rafId = undefined
      }

      running = false
      lastTs = undefined
      runtime = createInitialHorizontalZoomDemoRuntime()
      onFrame(runtime)
    },
    isRunning() {
      return running
    },
  }
}

// Exact Deluge row colour gradient from firmware (bottom -> top).
const ROW_COLOUR: [number, number, number][] = [
  [0, 255, 0],
  [36, 219, 0],
  [73, 182, 0],
  [109, 146, 0],
  [146, 109, 0],
  [182, 73, 0],
  [219, 36, 0],
  [255, 0, 0],
]

function flattenShortcutSteps(source: StepOrSubstep[]): Step[] {
  return source.flatMap((step) => (isStep(step) ? [step] : step.substeps))
}

// Matches exactly the standalone "hold X + turn X" shortcut pattern.
export function isStandaloneHorizontalZoomShortcut(
  source: StepOrSubstep[],
): boolean {
  const flattened = flattenShortcutSteps(source)
  // Branch: strict matcher expects exactly two flattened steps.
  if (flattened.length !== 2) {
    return false
  }

  const hasHoldX = flattened.some(
    (step) => step.action === Action.HOLD && step.control === Control.X,
  )
  const hasTurnX = flattened.some(
    (step) => step.action === Action.TURN && step.control === Control.X,
  )

  return hasHoldX && hasTurnX
}

// Returns overlap length for one-dimensional intervals.
function overlap(a0: number, a1: number, b0: number, b1: number): number {
  return Math.max(0, Math.min(a1, b1) - Math.max(a0, b0))
}

// Maps source column interval into scaled coordinate space around pinned column.
function getScaledInterval(col: number, scale: number): [number, number] {
  const pin = DEMO_PIN_COL - 1
  const srcLeft = col - 1
  const srcRight = col
  const left = pin + (srcLeft - pin) * scale
  const right = pin + (srcRight - pin) * scale
  return [left, right]
}

// Computes fractional intensity for one output cell at a given horizontal scale.
export function getHorizontalZoomRowColIntensity(
  row: number,
  col: number,
  scale: number,
): number {
  const outLeft = col - 1
  const outRight = col
  let total = 0

  for (const note of DEMO_NOTES) {
    // Branch: row mismatch contributes no overlap.
    if (note.row !== row) {
      continue
    }

    const [noteLeft, noteRight] = getScaledInterval(note.col, scale)
    total += overlap(outLeft, outRight, noteLeft, noteRight)
    // Branch: clamp once we reach full intensity.
    if (total >= 1) {
      return 1
    }
  }

  return Math.min(1, total)
}

// Builds display cells by blending from-scale and to-scale intensities.
export function buildHorizontalZoomDemoCells(
  rows: number,
  cols: number,
  fromScale: number,
  toScale: number,
  renderBlend: number,
): DemoCell[] {
  const cells: DemoCell[] = []
  for (let row = 1; row <= rows; row++) {
    for (let col = 1; col <= cols; col++) {
      const fromIntensity = getHorizontalZoomRowColIntensity(
        row,
        col,
        fromScale,
      )
      const toIntensity = getHorizontalZoomRowColIntensity(row, col, toScale)
      const blended =
        fromIntensity * (1 - renderBlend) + toIntensity * renderBlend
      cells.push({ row, col, intensity: blended, fromIntensity, toIntensity })
    }
  }

  return cells
}

// Formats rgb color with alpha using modern CSS syntax.
function rgbCss(r: number, g: number, b: number, alpha: number): string {
  return `rgb(${Math.round(r)} ${Math.round(g)} ${Math.round(b)} / ${alpha})`
}

// Converts display row index to firmware-style gradient color.
function rowBaseColourForDisplayRow(
  displayRow: number,
): [number, number, number] {
  // displayRow: 1 = top, 8 = bottom. Firmware array is bottom -> top.
  const idx = GRID_ROWS - displayRow
  return ROW_COLOUR[idx]
}

// Returns full fill+stroke inline style for one demo cell.
export function getHorizontalZoomCellFillStyle(cell: DemoCell): string {
  const i = Math.max(0, Math.min(1, cell.intensity))
  const [r, g, b] = rowBaseColourForDisplayRow(cell.row)
  const alpha = i > 0.001 ? 0.08 + i * 0.92 : 0

  const fillBoost = 0.42 + i * 0.78
  const strokeBoost = 0.2 + i * 0.44

  const fillR = Math.min(255, r * fillBoost)
  const fillG = Math.min(255, g * fillBoost)
  const fillB = Math.min(255, b * fillBoost)

  const strokeR = Math.min(255, r * strokeBoost)
  const strokeG = Math.min(255, g * strokeBoost)
  const strokeB = Math.min(255, b * strokeBoost)

  return `fill:${rgbCss(fillR, fillG, fillB, alpha)};stroke:${rgbCss(strokeR, strokeG, strokeB, alpha)};`
}

// Returns subtle top-detail overlay style for one demo cell.
export function getHorizontalZoomCellDetailStyle(cell: DemoCell): string {
  const i = Math.max(0, Math.min(1, cell.intensity))
  const detailAlpha = i > 0.001 ? i * 0.22 : 0
  const [r, g, b] = rowBaseColourForDisplayRow(cell.row)

  const mix = 0.84
  const detailR = r * (1 - mix) + 255 * mix
  const detailG = g * (1 - mix) + 255 * mix
  const detailB = b * (1 - mix) + 255 * mix

  return `fill:${rgbCss(detailR, detailG, detailB, detailAlpha)};`
}
