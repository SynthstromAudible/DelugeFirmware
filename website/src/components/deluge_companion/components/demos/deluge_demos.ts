import type { StepOrSubstep } from "../../types/shortcut.js";
import {
  buildHorizontalZoomDemoCells,
  createHorizontalZoomDemoLoop,
  getHorizontalZoomCellDetailStyle,
  getHorizontalZoomCellFillStyle,
  isStandaloneHorizontalZoomShortcut,
  type DemoCell,
  type HorizontalZoomDemoLoop,
} from "./horizontal_zoom_demo.js";

const GRID_COLS = 16;
const GRID_ROWS = 8;

export type DelugeDemoLoop = HorizontalZoomDemoLoop;

// Generic demo contract consumed by Deluge UI component.
export type DelugeDemoDefinition = {
  id: string;
  matches: (steps: StepOrSubstep[]) => boolean;
  createLoop: (onCells: (cells: DemoCell[]) => void) => DelugeDemoLoop;
  getCellFillStyle: (cell: DemoCell) => string;
  getCellDetailStyle: (cell: DemoCell) => string;
};

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
      );
    });
  },
  getCellFillStyle: getHorizontalZoomCellFillStyle,
  getCellDetailStyle: getHorizontalZoomCellDetailStyle,
};

const DEMOS: DelugeDemoDefinition[] = [horizontalZoomDemo];

export type { DemoCell };

// Selects first demo definition matching the current step sequence.
export function pickActiveDelugeDemo(steps: StepOrSubstep[]): DelugeDemoDefinition | undefined {
  return DEMOS.find((demo) => demo.matches(steps));
}
