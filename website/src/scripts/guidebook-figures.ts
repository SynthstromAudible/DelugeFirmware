// Light-touch Guidebook figure animations (spec §6). A figure with
// data-animate="draw" | "playhead" plays once when it first scrolls into view,
// then offers a replay button. Nothing is hidden before the script runs, and
// every animation ends on the figure's normal state; reduced motion skips it all.

type Stop = () => void

const STEP = 40 // ms between draw-in elements
const DRAW = 600 // ms per element
const TOTAL = 2500 // ms cap for a whole draw-in
const COLUMN = 120 // ms per playhead column

function draw(svg: SVGSVGElement): Stop {
  const els = [
    ...svg.querySelectorAll<SVGGraphicsElement>(
      "path, line, polyline, text, use, image, rect, circle",
    ),
  ].filter((el) => !el.closest("defs, clipPath, mask, symbol, text > *"))
  const top = (el: Element) => el.getBoundingClientRect().top
  els.sort((a, b) => top(a) - top(b))
  const step = Math.min(STEP, (TOTAL - DRAW) / Math.max(els.length, 1))
  const anims = els.map((el, i) => {
    const timing: KeyframeAnimationOptions = {
      duration: DRAW,
      delay: i * step,
      easing: "ease-out",
      fill: "backwards",
    }
    const stroked =
      getComputedStyle(el).stroke !== "none" && "getTotalLength" in el
    if (stroked) {
      const len = (el as SVGGeometryElement).getTotalLength()
      return el.animate(
        { strokeDasharray: [`${len}`, `${len}`], strokeDashoffset: [len, 0] },
        timing,
      )
    }
    return el.animate({ opacity: [0, 1] }, timing)
  })
  return () => anims.forEach((a) => a.cancel())
}

// The Deluge playhead: one column of white pads moving left to right. The cells
// (every pad position, added at build time) are numbered per grid, so all grids
// in a figure light column N together.
function playhead(svg: SVGSVGElement, done: () => void): Stop {
  const columns = new Map<number, Element[]>()
  for (const cell of svg.querySelectorAll(".gb-cell")) {
    const col = Number(cell.getAttribute("data-col"))
    columns.set(col, [...(columns.get(col) ?? []), cell])
  }
  const order = [...columns.keys()].sort((a, b) => a - b)
  let i = 0
  let lit: Element[] = []
  let timer = 0
  const stop = () => {
    window.clearInterval(timer)
    lit.forEach((c) => c.classList.remove("gb-cell-on"))
  }
  const tick = () => {
    lit.forEach((c) => c.classList.remove("gb-cell-on"))
    if (i === order.length) return (stop(), done())
    lit = columns.get(order[i++]) ?? []
    lit.forEach((c) => c.classList.add("gb-cell-on"))
  }
  timer = window.setInterval(tick, COLUMN)
  tick()
  return stop
}

function setup(fig: HTMLElement) {
  const art = fig.querySelector<HTMLElement>(".gb-figure-art")
  const svg = art?.querySelector("svg")
  const replay = fig.querySelector<HTMLButtonElement>(".gb-replay")
  if (!art || !svg || !replay) return
  let stop: Stop | undefined
  const finished = () => (replay.hidden = false)
  const play = () => {
    stop?.()
    if (fig.dataset.animate === "playhead") stop = playhead(svg, finished)
    else {
      stop = draw(svg)
      setTimeout(finished, TOTAL)
    }
  }
  replay.addEventListener("click", play)
  new IntersectionObserver(
    (entries, obs) => {
      if (!entries.some((e) => e.isIntersecting)) return
      obs.disconnect()
      play()
    },
    { threshold: 0.4 },
  ).observe(fig)
}

if (!matchMedia("(prefers-reduced-motion: reduce)").matches) {
  document
    .querySelectorAll<HTMLElement>(".gb-figure[data-animate]")
    .forEach(setup)
}
