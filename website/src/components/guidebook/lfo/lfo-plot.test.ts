import { strict as assert } from "node:assert"
import { test } from "node:test"
import { renderTile } from "./lfo-plot.ts"

const values = [0, 1, 0, -1, 0]

test("a tile in the book's style: axis, dashed centre, labels, accent curve", () => {
  const svg = renderTile(values, { clipId: "c1" })
  for (const cls of ["gb-lfo-axis", "gb-lfo-centre", "gb-lfo-curve"])
    assert.ok(svg.includes(`class="${cls}"`), cls)
  assert.ok(svg.includes(">AMPLITUDE<") && svg.includes(">TIME<"))
  assert.ok(svg.includes('<clipPath id="c1">') && svg.includes("url(#c1)"))
  // the next-to-last value at the right edge (the last waits past it, for smooth shifting)
  assert.match(svg, /d="M16 46L76 6L136 46L196 86L256 46"/)
})

test("shift slides the trace left; scale draws small signals taller and says so", () => {
  const moved = renderTile(values, { clipId: "c", shift: 0.5 })
  assert.match(moved, /d="M-14 46L/)
  const tall = renderTile([0.1, -0.1], { clipId: "c", scale: 5 })
  assert.ok(tall.includes(">drawn ×5<"))
  assert.match(tall, /d="M16 26L196 66"/)
  assert.ok(!renderTile(values, { clipId: "c" }).includes("drawn ×"))
})
