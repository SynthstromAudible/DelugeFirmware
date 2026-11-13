import {
  resolveControl,
  sequenceKeys,
} from "../../../markdown-directives/remark-deluge-key/controls.ts"
import type { PanelMap } from "./panel-map.ts"

/** A control a step uses, and how: held while the chord's last control is pressed, or turned. */
export type StepControl = { id: string; action: "press" | "hold" | "turn" }

/** `:key` control names -> panel control ids (see PanelMap). */
const PANEL_IDS: Record<string, string> = {
  Shift: "Shift",
  AffectEntire: "Affect-Entire",
  Clip: "Clip",
  CrossScreen: "Cross-screen",
  CV: "CV",
  Keyboard: "Keyboard",
  Kit: "Kit",
  Load: "Load",
  MIDI: "MIDI",
  Record: "Record",
  Play: "Play",
  Save: "Save",
  Scale: "Scale",
  Song: "Song",
  SyncScaling: "Sync-scaling",
  Synth: "Synth",
  TapTempo: "Tap-Tempo",
  TripletsView: "Triplets-View",
  Learn: "Learn-Input",
  Input: "Learn-Input",
  LearnInput: "Learn-Input",
  Back: "Back-Undo",
  Undo: "Back-Undo",
  BackUndo: "Back-Undo",
  // parameter buttons, in the panel's printed order
  LevelPan: "Function-1",
  CutoffRes: "Function-2",
  DelayTimeAmount: "Function-4",
  SidechainReverb: "Function-5",
  ModRateDepth: "Function-6",
  StutterCustom1: "Function-7",
  Horizontal: "Horizontal-Encoder",
  Vertical: "Vertical-Encoder",
  Select: "Select-Encoder",
  Tempo: "Tempo-Encoder",
}

/** Pads named for their column rather than a label. */
const PAD_COLUMNS: Record<string, number> = {
  audition: 18,
  section: 18,
  launch: 17,
  mute: 17,
}

function padIds(
  modifiers: string[],
  panel: PanelMap,
  warn: (msg: string) => void,
): string[] {
  if (!modifiers.length) return []
  const column = modifiers.length === 1 ? PAD_COLUMNS[modifiers[0]] : undefined
  if (column)
    return Object.keys(panel.controls).filter((id) =>
      id.startsWith(`pad-${column}-`),
    )
  const label = modifiers.join(" ").toUpperCase()
  const id = panel.padLabels[label]
  if (id) return [id]
  warn(
    panel.ambiguousPadLabels[label]
      ? `pad label ${label} is printed under several pads (${panel.ambiguousPadLabels[label].join(", ")}); not highlighted`
      : `no pad labelled ${label} on the panel; not highlighted`,
  )
  return []
}

function controlIds(
  key: string,
  panel: PanelMap,
  warn: (msg: string) => void,
): { ids: string[]; turn: boolean } {
  const { name, modifiers } = resolveControl(key)
  const turn = modifiers.includes("turn")
  if (name === "Pad") return { ids: padIds(modifiers, panel, warn), turn }
  if (name === "Gold") {
    const which = modifiers.includes("upper")
      ? "Upper-Gold-Encoder"
      : modifiers.includes("lower")
        ? "Lower-Gold-Encoder"
        : undefined
    return { ids: which ? [which] : [], turn }
  }
  const id = PANEL_IDS[name]
  return { ids: id ? [id] : [], turn }
}

/**
 * The panel controls a `:key[...]` sequence uses, in order. In each chord every control but the
 * last is held; knobs named with "Turn" are turned. Unknown pad labels warn and are skipped.
 */
export function keyControls(
  sequence: string,
  panel: PanelMap,
  warn: (msg: string) => void,
): StepControl[] {
  return sequenceKeys(sequence).flatMap((chord) =>
    chord.flatMap((key, i) => {
      const { ids, turn } = controlIds(key, panel, warn)
      const action: StepControl["action"] = turn
        ? "turn"
        : i < chord.length - 1
          ? "hold"
          : "press"
      return ids.map((id) => ({ id, action }))
    }),
  )
}
