import { strict as assert } from "node:assert"
import { test } from "node:test"
import { keyControls } from "./key-controls.ts"
import type { PanelMap } from "./panel-map.ts"

const circle = { kind: "button", shape: "circle", cx: 0, cy: 0, r: 10 } as const
const pad = {
  kind: "pad",
  shape: "rect",
  x: 0,
  y: 0,
  w: 10,
  h: 10,
  rx: 2,
  cx: 5,
  cy: 5,
} as const

const panel: PanelMap = {
  viewBox: [0, 0, 5808, 3952],
  controls: {
    Shift: circle,
    Play: circle,
    Clip: circle,
    "Horizontal-Encoder": { ...circle, kind: "encoder" },
    "Select-Encoder": { ...circle, kind: "encoder" },
    "Upper-Gold-Encoder": { ...circle, kind: "encoder" },
    "Function-1": circle,
    "Function-2": circle,
    "pad-3-1": pad,
    ...Object.fromEntries(
      [1, 2, 3, 4, 5, 6, 7, 8].flatMap((r) => [
        [`pad-17-${r}`, pad],
        [`pad-18-${r}`, pad],
      ]),
    ),
  },
  padLabels: { NOISE: "pad-3-1", NUMBER: "pad-4-3" },
  ambiguousPadLabels: { ATTACK: ["pad-9-6", "pad-12-6"] },
  regions: { full: [0, 0, 5808, 3952] },
  unread: [],
  problems: [],
}

const quiet = (): void => undefined

test("a chord holds all but its last control", () => {
  assert.deepEqual(keyControls("Shift + Play", panel, quiet), [
    { id: "Shift", action: "hold" },
    { id: "Play", action: "press" },
  ])
})

test("knobs turn or press", () => {
  assert.deepEqual(keyControls("Turn Select", panel, quiet), [
    { id: "Select-Encoder", action: "turn" },
  ])
  assert.deepEqual(keyControls("Horizontal", panel, quiet), [
    { id: "Horizontal-Encoder", action: "press" },
  ])
})

test("gold knobs and parameter buttons", () => {
  assert.deepEqual(keyControls("Turn Gold Upper", panel, quiet), [
    { id: "Upper-Gold-Encoder", action: "turn" },
  ])
  assert.deepEqual(keyControls("LevelPan", panel, quiet), [
    { id: "Function-1", action: "press" },
  ])
  assert.deepEqual(keyControls("CutoffRes", panel, quiet), [
    { id: "Function-2", action: "press" },
  ])
})

test("a pad named by its label", () => {
  assert.deepEqual(keyControls("Shift + Noise Pad", panel, quiet), [
    { id: "Shift", action: "hold" },
    { id: "pad-3-1", action: "press" },
  ])
})

test("audition and launch pads mean their whole column", () => {
  const audition = keyControls("Audition Pad", panel, quiet)
  assert.equal(audition.length, 8)
  assert.ok(audition.every((c) => /^pad-18-[1-8]$/.test(c.id)))
  const launch = keyControls("Launch Pad", panel, quiet)
  assert.ok(launch.length === 8 && launch.every((c) => /^pad-17-/.test(c.id)))
})

test("a plain Pad highlights nothing", () => {
  assert.deepEqual(keyControls("Pad", panel, quiet), [])
})

test("an unknown pad label warns once and keeps the rest", () => {
  const warnings: string[] = []
  const got = keyControls("Shift + Zzz Pad", panel, (m) => warnings.push(m))
  assert.deepEqual(got, [{ id: "Shift", action: "hold" }])
  assert.equal(warnings.length, 1)
  assert.match(warnings[0], /Zzz/i)
})

test("an ambiguous pad label warns instead of guessing", () => {
  const warnings: string[] = []
  assert.deepEqual(
    keyControls("Attack Pad", panel, (m) => warnings.push(m)),
    [],
  )
  assert.match(warnings[0], /ATTACK/)
})

test("chord sequences give the controls of every chord in order", () => {
  assert.deepEqual(keyControls("Clip > Shift + Play", panel, quiet), [
    { id: "Clip", action: "press" },
    { id: "Shift", action: "hold" },
    { id: "Play", action: "press" },
  ])
})
