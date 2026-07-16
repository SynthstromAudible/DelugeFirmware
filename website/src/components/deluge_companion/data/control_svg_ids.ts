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
