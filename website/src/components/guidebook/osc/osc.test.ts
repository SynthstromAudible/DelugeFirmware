import { strict as assert } from "node:assert"
import { test } from "node:test"
import oscJson from "../../../data/guidebook-osc.json"
import { OscDataSchema, OscStream, SHAPES, oscWave } from "./osc.ts"

const data = OscDataSchema.parse(oscJson)
const near = (a: number, b: number, eps: number, msg = "") =>
  assert.ok(Math.abs(a - b) <= eps, `${msg} ${a} != ${b} ± ${eps}`)

test("the book's two groups, in its order and with its names", () => {
  assert.deepEqual(
    SHAPES.digital.map((s) => s.name),
    ["Triangle", "Sine", "Sawtooth", "Square"],
  )
  assert.deepEqual(
    SHAPES.analog.map((s) => s.name),
    ["Sawtooth", "ASquare"],
  )
})

test("digital shapes follow the firmware: the saw rises (renderCrudeSawWave)", () => {
  near(oscWave("sine", 0.25, data), 1, 1e-9)
  near(oscWave("square", 0.1, data), 1, 1e-9)
  near(oscWave("square", 0.6, data), -1, 1e-9)
  // the crude saw is the phase as a signed number: rising through the cycle, then dropping
  assert.ok(oscWave("saw", 0.2, data) < oscWave("saw", 0.4, data))
  assert.ok(oscWave("saw", 0.6, data) < oscWave("saw", 0.8, data))
  near(oscWave("triangle", 0.5, data), 1, 1e-9)
})

test("analog shapes are the firmware's recorded tables, scaled to fill the plot", () => {
  const sq = data.analogSquare.values
  const peak = Math.max(...sq.map(Math.abs))
  near(oscWave("analogSquare", 0, data), sq[0] / peak, 1e-9, "first point")
  near(oscWave("analogSquare", 64 / 256, data), sq[64] / peak, 1e-9)
  // between two points: linear
  near(
    oscWave("analogSquare", 64.5 / 256, data),
    (sq[64] + sq[65]) / 2 / peak,
    1e-9,
  )
  // the analog square droops through each half-cycle
  assert.ok(
    oscWave("analogSquare", 0.05, data) > oscWave("analogSquare", 0.45, data),
  )
  const all = Array.from({ length: 256 }, (_, i) =>
    Math.abs(oscWave("analogSaw", i / 256, data)),
  )
  near(Math.max(...all), 1, 1e-9, "analog saw fills the plot")
})

test("a stream gives perCycle values a cycle, repeating", () => {
  const s = new OscStream("square", data, { perCycle: 8 })
  const first = Array.from({ length: 8 }, () => s.next())
  const second = Array.from({ length: 8 }, () => s.next())
  assert.deepEqual(first, second)
  assert.deepEqual(first, [1, 1, 1, 1, -1, -1, -1, -1])
})
