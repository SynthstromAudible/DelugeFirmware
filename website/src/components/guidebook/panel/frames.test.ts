import { strict as assert } from "node:assert"
import { test } from "node:test"
import { validateFrames } from "./frames.ts"
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
  controls: { Play: circle, "pad-1-1": pad },
  padLabels: {},
  ambiguousPadLabels: {},
  regions: { full: [0, 0, 5808, 3952], transport: [4000, 300, 1700, 1200] },
  unread: [],
  problems: [],
}

const file = (steps: unknown[], extra = {}) => ({
  figure: "fig-x",
  source: "hand",
  window: "transport",
  steps,
  ...extra,
})

test("accepts frames matching the step count", () => {
  const f = validateFrames(file([{}, { leds: { Play: "green" } }]), {
    figure: "fig-x",
    stepCount: 2,
    panel,
  })
  assert.equal(f.steps.length, 2)
})

test("a step-count mismatch names the figure and both counts", () => {
  assert.throws(
    () =>
      validateFrames(file([{}, {}, {}]), {
        figure: "fig-x",
        stepCount: 2,
        panel,
      }),
    (e: Error) =>
      /fig-x/.test(e.message) && /3/.test(e.message) && /2/.test(e.message),
  )
})

test("without steps a figure has exactly one frame", () => {
  assert.equal(
    validateFrames(file([{}]), { figure: "fig-x", stepCount: undefined, panel })
      .steps.length,
    1,
  )
  assert.throws(() =>
    validateFrames(file([]), { figure: "fig-x", stepCount: undefined, panel }),
  )
})

test("several frames with no Steps bound is an error, not a silent static figure", () => {
  assert.throws(
    () =>
      validateFrames(file([{}, {}, {}]), {
        figure: "fig-x",
        stepCount: undefined,
        panel,
      }),
    (e: Error) =>
      /fig-x/.test(e.message) &&
      /3 frames/.test(e.message) &&
      /no Steps/.test(e.message),
  )
})

test("rejects controls the panel doesn't have", () => {
  assert.throws(
    () =>
      validateFrames(file([{ pads: { "pad-99-1": "#ff0000" } }]), {
        figure: "fig-x",
        stepCount: 1,
        panel,
      }),
    /pad-99-1/,
  )
  assert.throws(
    () =>
      validateFrames(file([{ leds: { Nope: "red" } }]), {
        figure: "fig-x",
        stepCount: 1,
        panel,
      }),
    /Nope/,
  )
})

test("rejects an unknown window name and a non-hex pad colour", () => {
  assert.throws(
    () =>
      validateFrames(file([{}], { window: "attic" }), {
        figure: "fig-x",
        stepCount: 1,
        panel,
      }),
    /attic/,
  )
  assert.throws(() =>
    validateFrames(file([{ pads: { "pad-1-1": "red" } }]), {
      figure: "fig-x",
      stepCount: 1,
      panel,
    }),
  )
})

test("rejects a frame file for another figure", () => {
  assert.throws(
    () => validateFrames(file([{}]), { figure: "fig-y", stepCount: 1, panel }),
    /fig-y/,
  )
})

test("orange is an LED colour (parameter buttons, Affect Entire)", () => {
  const f = validateFrames(file([{ leds: { Play: "orange" } }]), {
    figure: "fig-x",
    stepCount: 1,
    panel,
  })
  assert.equal(f.steps[0].leds?.Play, "orange")
})
