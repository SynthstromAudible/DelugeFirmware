import { strict as assert } from "node:assert"
import { test } from "node:test"
import {
  labelsFit,
  layoutCallouts,
  LABEL_PITCH,
  stackLabels,
} from "./callouts.ts"
import type { PanelMap } from "./panel-map.ts"

const panel = {
  controls: {
    Play: { kind: "button", shape: "circle", cx: 5000, cy: 900, r: 60 },
    Record: { kind: "button", shape: "circle", cx: 5000, cy: 930, r: 60 },
    "pad-1-1": {
      kind: "pad",
      shape: "rect",
      x: 300,
      y: 1500,
      w: 180,
      h: 180,
      rx: 18,
      cx: 390,
      cy: 1590,
    },
  },
} as unknown as PanelMap

const window = [4000, 300, 1700, 1200] as [number, number, number, number]

test("an unknown control is an error naming it", () => {
  assert.throws(
    () =>
      layoutCallouts(
        [{ control: "Nope", text: "x", side: "left" }],
        panel,
        window,
      ),
    /Nope/,
  )
})

test("labels on one side never overlap, and keep control order", () => {
  const placed = layoutCallouts(
    [
      { control: "Record", text: "Record", side: "left" },
      { control: "Play", text: "Play", side: "left" },
    ],
    panel,
    window,
  )
  const [play, record] = [...placed].sort((a, b) => a.label.y - b.label.y)
  assert.equal(play.control, "Play") // Play is higher on the panel
  assert.ok(record.label.y - play.label.y >= LABEL_PITCH * window[3] - 1e-6)
})

test("a leader ends on a circle's outline, towards its label", () => {
  const [c] = layoutCallouts(
    [{ control: "Play", text: "Play", side: "left" }],
    panel,
    window,
  )
  const d = Math.hypot(c.leader.x2 - 5000, c.leader.y2 - 900)
  assert.ok(Math.abs(d - 60) <= 1, `distance ${d}`)
  assert.ok(c.leader.x2 < 5000, "leader comes from the left")
})

test("a leader ends on a rect's edge", () => {
  const [c] = layoutCallouts(
    [{ control: "pad-1-1", text: "pad", side: "top" }],
    panel,
    [0, 1000, 1000, 1000],
  )
  assert.ok(Math.abs(c.leader.y2 - 1500) <= 1, `y ${c.leader.y2}`)
})

test("labels sit at the window edge on their side", () => {
  const [l] = layoutCallouts(
    [{ control: "Play", text: "Play", side: "left" }],
    panel,
    window,
  )
  const [r] = layoutCallouts(
    [{ control: "Play", text: "Play", side: "right" }],
    panel,
    window,
  )
  assert.equal(l.label.x, window[0])
  assert.equal(r.label.x, window[0] + window[2])
})

test("stackLabels keeps labels at their controls when there is room", () => {
  assert.deepEqual(stackLabels([100, 300], [40, 40], 8, 1000), [80, 280])
})

test("stackLabels pushes overlapping labels apart, in order", () => {
  // centred on 100 and 110 with heights 60: the second starts below the first plus the gap
  const tops = stackLabels([100, 110], [60, 60], 8, 1000)
  assert.equal(tops[0], 70)
  assert.equal(tops[1], 70 + 60 + 8)
})

test("stackLabels keeps the stack inside the figure", () => {
  const tops = stackLabels([950, 990], [60, 60], 8, 1000)
  assert.ok(tops[1] + 60 <= 1000)
  assert.ok(tops[0] + 60 + 8 <= tops[1])
  assert.ok(tops[0] >= 0)
})

test("labelsFit: the stack must fit the figure's height", () => {
  assert.equal(labelsFit([60, 60], 8, 128), true)
  assert.equal(labelsFit([130, 130], 6, 216), false)
  assert.equal(labelsFit([], 6, 10), true)
})
