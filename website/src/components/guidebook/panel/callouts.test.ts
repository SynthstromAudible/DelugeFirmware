import { strict as assert } from "node:assert"
import { test } from "node:test"
import { layoutCallouts, LABEL_PITCH } from "./callouts.ts"
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
