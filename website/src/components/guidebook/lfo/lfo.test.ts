import { strict as assert } from "node:assert"
import { test } from "node:test"
import { LfoStream, SHAPES, rateHz, seeded, wave } from "./lfo.ts"

const near = (a: number, b: number, eps: number, msg = "") =>
  assert.ok(Math.abs(a - b) <= eps, `${msg} ${a} != ${b} ± ${eps}`)

test("the seven shapes, in the firmware's order", () => {
  assert.deepEqual(
    SHAPES.map((s) => s.id),
    [
      "sine",
      "triangle",
      "square",
      "saw",
      "sampleHold",
      "randomWalk",
      "warbler",
    ],
  )
})

test("fixed shapes follow src/deluge/util/waves.h and lfo.h", () => {
  near(wave("sine", 0), 0, 1e-9)
  near(wave("sine", 0.25), 1, 1e-9)
  // triangle: from the bottom, up to the top at half a cycle
  near(wave("triangle", 0), -1, 1e-9)
  near(wave("triangle", 0.25), 0, 1e-9)
  near(wave("triangle", 0.5), 1, 1e-9)
  // square: high for the first half
  assert.equal(wave("square", 0.1), 1)
  assert.equal(wave("square", 0.6), -1)
  // saw: the phase as a signed number: up from 0, wraps to the bottom at half a cycle
  near(wave("saw", 0), 0, 1e-9)
  near(wave("saw", 0.25), 0.5, 1e-9)
  near(wave("saw", 0.5), -1, 1e-9)
  near(wave("saw", 0.75), -0.5, 1e-9)
})

test("LFO rate: knob 25 is about 5 Hz, each 25 steps eight octaves", () => {
  near(rateHz(25), 5.0, 0.01)
  near(rateHz(0) * 256, rateHz(25), 0.01)
  near(rateHz(50) / 256, rateHz(25), 0.01)
})

const run = (
  shape: (typeof SHAPES)[number]["id"],
  cycles: number,
  perCycle = 64,
  opts = {},
) => {
  const s = new LfoStream(shape, seeded(7), { perCycle, ...opts })
  return Array.from({ length: cycles * perCycle }, () => s.next())
}

test("sample & hold: one random value per cycle", () => {
  const v = run("sampleHold", 6)
  for (let c = 0; c < 6; c++) {
    const cycle = v.slice(c * 64, c * 64 + 64)
    assert.ok(
      cycle.every((x) => x === cycle[0]),
      `cycle ${c} constant`,
    )
  }
  assert.ok(new Set(v).size >= 5, "new values")
  assert.ok(v.every((x) => x >= -1 && x <= 1))
})

test("random walk: starts within ±5 %, steps at most 5 %, stays near zero", () => {
  const v = run("randomWalk", 2000, 4)
  assert.ok(Math.abs(v[0]) <= 0.05)
  for (let i = 4; i < v.length; i += 4)
    assert.ok(
      Math.abs(v[i] - v[i - 4]) <= 0.05 + Math.abs(v[i - 4]) / 16 + 1e-9,
    )
  assert.ok(Math.max(...v.map(Math.abs)) < 0.5)
})

test("warbler: glides to a new target every half cycle; slow rates get there, fast ones barely move", () => {
  const moved = (knob: number) => {
    const s = new LfoStream("warbler", seeded(3), { perCycle: 64, rate: knob })
    // as on the Deluge, it holds 0 until its first wrap (half a cycle), then heads for a target
    for (let i = 0; i < 32; i++) assert.equal(s.next(), 0)
    let far = 0
    for (let i = 0; i < 32; i++) far = Math.max(far, Math.abs(s.next()))
    return far / Math.abs(s.target)
  }
  assert.ok(moved(8) > 0.5, `slow ${moved(8)}`)
  assert.ok(moved(35) < 0.1, `fast ${moved(35)}`)
})

test("seeded randomness repeats; streams are independent of frame timing", () => {
  assert.deepEqual(run("sampleHold", 3), run("sampleHold", 3))
})
