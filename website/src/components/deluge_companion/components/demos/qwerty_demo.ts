export type QwertyPadColor = {
  x: number
  y: number
  color: string
}

// Matches firmware QwertyUI::drawKeys() grid coordinates (home row = y3).
export function buildQwertyPadColorMap(): QwertyPadColor[] {
  const low = "rgb(150, 150, 150)"
  const mid = "rgb(192, 192, 192)"
  const high = "rgb(228, 228, 228)"
  const red = "rgb(214, 29, 36)"
  const green = "rgb(97, 188, 86)"
  const blue = "rgb(92, 150, 207)"

  const pads = new Map<string, string>()
  const setPad = (x: number, y: number, color: string) => {
    pads.set(`${x},${y}`, color)
  }

  for (let i = 0; i < 11; i++) {
    if (i < 10) {
      setPad(3 + i, 5, mid)
      setPad(3 + i, 4, low)
    }

    setPad(3 + i, 3, low)

    if (i < 9) {
      setPad(3 + i, 2, low)
    }
  }

  setPad(13, 5, low)

  // Home-row accents.
  ;[3, 4, 5, 10, 11, 12].forEach((x) => setPad(x, 3, mid))
  ;[6, 9].forEach((x) => setPad(x, 3, high))

  // Space bar.
  for (let i = 0; i < 6; i++) {
    setPad(5 + i, 1, high)
  }

  // Special keys.
  for (let x = 0; x < 2; x++) {
    setPad(14 + x, 5, red)
    setPad(14 + x, 3, green)
    setPad(1 + x, 2, blue)
    setPad(13 + x, 2, blue)
  }

  return [...pads.entries()].map(([key, color]) => {
    const [x, y] = key.split(",").map(Number)
    return { x, y, color }
  })
}
