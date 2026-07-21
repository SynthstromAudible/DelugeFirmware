import { Control } from "./targets.js"

/**
 * Direct SVG ID mappings for non-grid controls.
 *
 * Grid-based controls (pads and columns) remain coordinate-driven via
 * control_coordinates.ts + hardware_coordinates.ts.
 */
export const controlSvgIds: Partial<Record<Control, string[]>> = {
  [Control.RECORD]: ["Record"],
  [Control.PLAY]: ["Play"],
  [Control.LOAD]: ["Load"],
  [Control.SHIFT]: ["Shift"],
  [Control.BACK]: ["Back-Undo"],
  [Control.SAVE]: ["Save"],
  [Control.TAP]: ["Tap-Tempo"],
  [Control.SYNC]: ["Sync-scaling"],
  [Control.LEARN]: ["Learn-Input"],
  [Control.CLIP]: ["Clip"],
  [Control.SYNTH]: ["Synth"],
  [Control.KIT]: ["Kit"],
  [Control.MIDI]: ["MIDI"],
  [Control.CV]: ["CV"],
  [Control.CROSS]: ["Cross-screen"],
  [Control.ENTIRE]: ["Affect-Entire"],
  [Control.PARAMBUTTON]: [
    "Function-1",
    "Function-2",
    "Function-3",
    "Function-4",
    "Function-5",
    "Function-6",
    "Function-7",
    "Function-8",
  ],
  [Control.PARAMBUTTON1]: ["Function-1"],
  [Control.PARAMBUTTON2]: ["Function-2"],
  [Control.PARAMBUTTON3]: ["Function-3"],
  [Control.PARAMBUTTON4]: ["Function-4"],
  [Control.PARAMBUTTON5]: ["Function-5"],
  [Control.PARAMBUTTON6]: ["Function-6"],
  [Control.PARAMBUTTON7]: ["Function-7"],
  [Control.PARAMBUTTON8]: ["Function-8"],
  [Control.PERFORMANCE]: ["Keyboard"],
  [Control.KEY]: ["Keyboard"],
  [Control.SCALE]: ["Scale"],
  [Control.SONG]: ["Song"],
  [Control.X]: ["Horizontal-Encoder"],
  [Control.Y]: ["Vertical-Encoder"],
  [Control.SELECT]: ["Select-Encoder"],
  [Control.TEMPO]: ["Tempo-Encoder"],
  [Control.PARAMETER]: ["Lower-Gold-Encoder", "Upper-Gold-Encoder"],
  [Control.LOWER_PARAM]: ["Lower-Gold-Encoder"],
  [Control.UPPER_PARAM]: ["Upper-Gold-Encoder"],
}

export function getControlSvgIds(control: Control): string[] {
  return controlSvgIds[control] ?? []
}
