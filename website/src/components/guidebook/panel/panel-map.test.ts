import { strict as assert } from "node:assert"
import { readFileSync } from "node:fs"
import { test } from "node:test"
import { keyControls } from "./key-controls.ts"
import { PanelMapSchema } from "./panel-map.ts"

const panel = PanelMapSchema.parse(
  JSON.parse(
    readFileSync(
      new URL("../../../data/guidebook-panel.json", import.meta.url),
      "utf8",
    ),
  ),
)

test("the committed panel has 144 pads and no build problems", () => {
  const pads = Object.keys(panel.controls).filter((id) => id.startsWith("pad-"))
  assert.equal(pads.length, 144)
  assert.deepEqual(panel.problems, [])
})

test("every control :key can name exists on the panel", () => {
  const keys = [
    "Shift",
    "AffectEntire",
    "Clip",
    "CrossScreen",
    "CV",
    "Keyboard",
    "Kit",
    "Load",
    "MIDI",
    "Record",
    "Play",
    "Save",
    "Scale",
    "Song",
    "SyncScaling",
    "Synth",
    "TapTempo",
    "TripletsView",
    "LearnInput",
    "BackUndo",
    "LevelPan",
    "CutoffRes",
    "DelayTimeAmount",
    "SidechainReverb",
    "ModRateDepth",
    "StutterCustom1",
    "Horizontal",
    "Vertical",
    "Select",
    "Tempo",
    "Gold Upper",
    "Gold Lower",
  ]
  for (const key of keys) {
    for (const { id } of keyControls(key, panel, assert.fail)) {
      assert.ok(panel.controls[id], `${key} -> ${id} is not on the panel`)
    }
  }
})
