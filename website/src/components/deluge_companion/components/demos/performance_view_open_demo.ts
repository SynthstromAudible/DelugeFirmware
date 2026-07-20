import { Action } from "../../data/actions.js"
import { Control } from "../../data/targets.js"
import { isStep, type Step, type StepOrSubstep } from "../../types/shortcut.js"
import type { DemoCell } from "./horizontal_zoom_demo.js"

const GRID_ROWS = 8
const PERFORMANCE_VIEW_OPENING_DURATION_MS = 1120
const PERFORMANCE_VIEW_OPENING_REVEAL_MS = 320

export type PerformanceViewOpeningDemoRuntime = {
  elapsedMs: number
}

export type PerformanceViewOpeningDemoLoop = {
  start: () => void
  stop: () => void
  isRunning: () => boolean
}

const DEFAULT_FILL: [number, number, number] = [178, 183, 190]
const DEFAULT_STROKE: [number, number, number] = [208, 213, 220]

// Approximate the color wash in the attached photo from left to right.
const COLUMN_COLOURS: [number, number, number][] = [
  [255, 145, 195],
  [255, 168, 205],
  [255, 191, 214],
  [255, 214, 221],
  [231, 244, 214],
  [214, 244, 226],
  [192, 241, 239],
  [171, 233, 248],
  [163, 222, 252],
  [201, 193, 252],
  [213, 196, 252],
  [223, 201, 252],
  [216, 198, 250],
  [207, 196, 252],
  [177, 211, 255],
  [100, 240, 242],
]

const GRID_COLS = COLUMN_COLOURS.length

function clamp01(value: number): number {
  return Math.max(0, Math.min(1, value))
}

function smoothstep(value: number): number {
  const clamped = clamp01(value)
  return clamped * clamped * (3 - 2 * clamped)
}

function mixChannel(from: number, to: number, amount: number): number {
  return from * (1 - amount) + to * amount
}

function rgbCss(r: number, g: number, b: number, alpha: number): string {
  return `rgba(${Math.round(r)}, ${Math.round(g)}, ${Math.round(b)}, ${alpha})`
}

function mixColour(
  from: [number, number, number],
  to: [number, number, number],
  amount: number,
): [number, number, number] {
  return [
    mixChannel(from[0], to[0], amount),
    mixChannel(from[1], to[1], amount),
    mixChannel(from[2], to[2], amount),
  ]
}

function brightenColour(
  colour: [number, number, number],
  amount: number,
): [number, number, number] {
  return mixColour(colour, [255, 255, 255], amount)
}

function getColumnColour(col: number): [number, number, number] {
  return COLUMN_COLOURS[
    Math.max(0, Math.min(COLUMN_COLOURS.length - 1, col - 1))
  ]
}

function getTargetColourForCell(
  row: number,
  col: number,
): [number, number, number] {
  const rowLift = 0.02 + (GRID_ROWS - row) * 0.018
  return brightenColour(getColumnColour(col), rowLift)
}

function getCellRevealStartMs(row: number, col: number): number {
  return (col - 1) * 24 + (GRID_ROWS - row) * 10
}

function getCellRevealIntensity(
  elapsedMs: number,
  row: number,
  col: number,
): number {
  const reveal =
    (elapsedMs - getCellRevealStartMs(row, col)) /
    PERFORMANCE_VIEW_OPENING_REVEAL_MS
  const shimmer =
    0.96 + 0.04 * Math.sin(elapsedMs / 90 + row * 0.28 + col * 0.16)
  return clamp01(smoothstep(reveal) * shimmer)
}

function flattenShortcutSteps(source: StepOrSubstep[]): Step[] {
  return source.flatMap((step) => (isStep(step) ? [step] : step.substeps))
}

// Matches the single press or hold that opens Performance View.
export function isStandalonePerformanceViewEntryShortcut(
  source: StepOrSubstep[],
): boolean {
  const flattened = flattenShortcutSteps(source)
  if (flattened.length !== 1) {
    return false
  }

  return (
    flattened[0].control === Control.PERFORMANCE &&
    (flattened[0].action === Action.PRESS ||
      flattened[0].action === Action.HOLD)
  )
}

// Produces the live grid colors for the opening animation.
export function buildPerformanceViewOpeningDemoCells(
  rows: number,
  cols: number,
  elapsedMs: number,
): DemoCell[] {
  const cells: DemoCell[] = []

  for (let row = 1; row <= rows; row++) {
    for (let col = 1; col <= cols; col++) {
      const reveal =
        elapsedMs >= PERFORMANCE_VIEW_OPENING_DURATION_MS
          ? 1
          : getCellRevealIntensity(elapsedMs, row, col)

      cells.push({
        row,
        col,
        intensity: reveal,
        fromIntensity: 0,
        toIntensity: 1,
      })
    }
  }

  return cells
}

export function createInitialPerformanceViewOpeningDemoRuntime(): PerformanceViewOpeningDemoRuntime {
  return {
    elapsedMs: 0,
  }
}

export function advancePerformanceViewOpeningDemoRuntime(
  runtime: PerformanceViewOpeningDemoRuntime,
  dtMs: number,
): PerformanceViewOpeningDemoRuntime {
  return {
    elapsedMs: Math.min(
      PERFORMANCE_VIEW_OPENING_DURATION_MS,
      runtime.elapsedMs + dtMs,
    ),
  }
}

export function createPerformanceViewOpeningDemoLoop(
  onFrame: (cells: DemoCell[]) => void,
): PerformanceViewOpeningDemoLoop {
  let runtime = createInitialPerformanceViewOpeningDemoRuntime()
  let rafId: number | undefined
  let lastTs: number | undefined
  let running = false

  const publish = () => {
    onFrame(
      buildPerformanceViewOpeningDemoCells(
        GRID_ROWS,
        GRID_COLS,
        runtime.elapsedMs,
      ),
    )
  }

  const tick = (ts: number) => {
    if (!running) {
      return
    }

    const dt = lastTs === undefined ? 16 : Math.min(40, ts - lastTs)
    lastTs = ts
    runtime = advancePerformanceViewOpeningDemoRuntime(runtime, dt)
    publish()

    if (runtime.elapsedMs >= PERFORMANCE_VIEW_OPENING_DURATION_MS) {
      running = false
      rafId = undefined
      return
    }

    rafId = requestAnimationFrame(tick)
  }

  return {
    start() {
      this.stop()
      runtime = createInitialPerformanceViewOpeningDemoRuntime()
      running = true
      publish()
      rafId = requestAnimationFrame(tick)
    },
    stop() {
      if (rafId !== undefined) {
        cancelAnimationFrame(rafId)
        rafId = undefined
      }

      running = false
      lastTs = undefined
      runtime = createInitialPerformanceViewOpeningDemoRuntime()
      publish()
    },
    isRunning() {
      return running
    },
  }
}

function getIntensityBlend(intensity: number): number {
  return Math.pow(clamp01(intensity), 0.72)
}

function getCellTargetColour(cell: DemoCell): [number, number, number] {
  return getTargetColourForCell(cell.row, cell.col)
}

export function getPerformanceViewOpeningCellFillStyle(cell: DemoCell): string {
  const blend = getIntensityBlend(cell.intensity)
  const [targetR, targetG, targetB] = getCellTargetColour(cell)

  const fill = mixColour(DEFAULT_FILL, [targetR, targetG, targetB], blend)
  const stroke = mixColour(
    DEFAULT_STROKE,
    brightenColour([targetR, targetG, targetB], 0.16),
    blend,
  )

  return `fill:${rgbCss(fill[0], fill[1], fill[2], 1)};stroke:${rgbCss(stroke[0], stroke[1], stroke[2], 1)};`
}

export function getPerformanceViewOpeningCellDetailStyle(
  cell: DemoCell,
): string {
  const intensity = clamp01(cell.intensity)
  if (intensity <= 0.001) {
    return ""
  }

  const [targetR, targetG, targetB] = getCellTargetColour(cell)
  const highlight = brightenColour([targetR, targetG, targetB], 0.38)
  const alpha = intensity * 0.18

  return `fill:${rgbCss(highlight[0], highlight[1], highlight[2], alpha)};`
}
