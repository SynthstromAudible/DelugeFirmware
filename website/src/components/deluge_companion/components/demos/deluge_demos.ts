import type { StepOrSubstep } from "../../types/shortcut.js"
import {
  buildHorizontalZoomDemoCells,
  createHorizontalZoomDemoLoop,
  getHorizontalZoomCellDetailStyle,
  getHorizontalZoomCellFillStyle,
  isStandaloneHorizontalZoomShortcut,
  type DemoCell,
  type HorizontalZoomDemoLoop,
} from "./horizontal_zoom_demo.js"
import {
  createPerformanceViewOpeningDemoLoop,
  getPerformanceViewOpeningCellDetailStyle,
  getPerformanceViewOpeningCellFillStyle,
  isStandalonePerformanceViewEntryShortcut,
  type PerformanceViewOpeningDemoLoop,
} from "./performance_view_open_demo.js"

export {
  appendTurnIndicator,
  clearTurnIndicators,
  shouldBlinkTurnControl,
} from "./encoder_turn_demo.js"
export { buildQwertyPadColorMap } from "./qwerty_demo.js"
export {
  getHorizontalZoomTurnAngle,
  getLatestHorizontalZoomTurnAngle,
} from "./horizontal_zoom_demo.js"

const GRID_COLS = 16
const GRID_ROWS = 8

export type DelugeDemoLoop =
  HorizontalZoomDemoLoop | PerformanceViewOpeningDemoLoop

// Generic demo contract consumed by Deluge UI component.
export type DelugeDemoDefinition = {
  id: string
  matches: (steps: StepOrSubstep[]) => boolean
  createLoop: (onCells: (cells: DemoCell[]) => void) => DelugeDemoLoop
  getCellFillStyle: (cell: DemoCell) => string
  getCellDetailStyle: (cell: DemoCell) => string
}

// Horizontal zoom demo wiring.
const horizontalZoomDemo: DelugeDemoDefinition = {
  id: "horizontal-zoom",
  matches: isStandaloneHorizontalZoomShortcut,
  createLoop(onCells) {
    return createHorizontalZoomDemoLoop((runtime) => {
      onCells(
        buildHorizontalZoomDemoCells(
          GRID_ROWS,
          GRID_COLS,
          runtime.fromScale,
          runtime.toScale,
          runtime.renderBlend,
        ),
      )
    })
  },
  getCellFillStyle: getHorizontalZoomCellFillStyle,
  getCellDetailStyle: getHorizontalZoomCellDetailStyle,
}

// Performance view opening demo wiring.
const performanceViewOpeningDemo: DelugeDemoDefinition = {
  id: "performance-view-opening",
  matches: isStandalonePerformanceViewEntryShortcut,
  createLoop(onCells) {
    return createPerformanceViewOpeningDemoLoop(onCells)
  },
  getCellFillStyle: getPerformanceViewOpeningCellFillStyle,
  getCellDetailStyle: getPerformanceViewOpeningCellDetailStyle,
}

const DEMOS: DelugeDemoDefinition[] = [
  horizontalZoomDemo,
  performanceViewOpeningDemo,
]

export type { DemoCell }

// Selects first demo definition matching the current step sequence.
export function pickActiveDelugeDemo(
  steps: StepOrSubstep[],
): DelugeDemoDefinition | undefined {
  return DEMOS.find((demo) => demo.matches(steps))
}
