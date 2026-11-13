/**
 * The moving plots of the LFO (6.3) and oscillator (4.9) figures: each tile's signal advances like
 * a scope trace, SPEED values a second, a whole value at a time plus the fraction as a shift so it
 * moves smoothly. The figure's Pause button stops them; they start paused for readers who prefer
 * reduced motion, and stop while off screen. Plots are redrawn at the width shown, so their text
 * keeps its size.
 */
import { TILE, renderTile } from "./lfo-plot.ts"

export type ScopeTile = {
  /** Unique within the figure: names the plot's clip path. */
  key: string
  svg: SVGSVGElement
  stream: { next(): number }
  scale?: number
}

export function bindScope(
  figure: HTMLElement,
  tiles: ScopeTile[],
  opts: { window: number; preroll: number; speed: number },
): void {
  const button = figure.querySelector<HTMLButtonElement>(".gb-lfo-pause")
  const actions = figure.querySelector<HTMLElement>(".gb-lfo-actions")
  if (!button || !actions) return

  const live = tiles.map((t) => {
    for (let k = 0; k < opts.preroll; k++) t.stream.next()
    const values = Array.from({ length: opts.window + 1 }, () =>
      t.stream.next(),
    )
    return { ...t, values, width: TILE.w }
  })

  let shift = 0
  const draw = () => {
    for (const t of live)
      t.svg.innerHTML = renderTile(t.values, {
        clipId: `${figure.id}-${t.key}`,
        width: t.width,
        shift,
        scale: t.scale ?? 1,
      })
  }

  let paused = matchMedia("(prefers-reduced-motion: reduce)").matches
  let visible = false
  let frame = 0
  let last = 0
  const tick = (now: number) => {
    shift += ((now - last) / 1000) * opts.speed
    last = now
    while (shift >= 1) {
      shift -= 1
      for (const t of live) {
        t.values.shift()
        t.values.push(t.stream.next())
      }
    }
    draw()
    frame = requestAnimationFrame(tick)
  }
  const run = () => {
    cancelAnimationFrame(frame)
    if (paused || !visible) return
    last = performance.now()
    frame = requestAnimationFrame(tick)
  }

  const label = () => {
    button.textContent = paused ? "Play" : "Pause"
    button.setAttribute("aria-pressed", String(paused))
  }
  button.addEventListener("click", () => {
    paused = !paused
    label()
    run()
  })
  new IntersectionObserver(([entry]) => {
    visible = entry.isIntersecting
    run()
  }).observe(figure)

  for (const t of live)
    new ResizeObserver(() => {
      const shown = Math.max(
        120,
        Math.round(t.svg.getBoundingClientRect().width),
      )
      if (shown === t.width) return
      t.width = shown
      t.svg.setAttribute("viewBox", `0 0 ${shown} ${TILE.h}`)
      draw()
    }).observe(t.svg)

  label()
  actions.hidden = false
  draw()
  run()
}
