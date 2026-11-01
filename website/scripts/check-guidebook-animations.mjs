// Checks the Guidebook's interactive behaviour against a running `astro preview`.
//   node scripts/check-guidebook-animations.mjs [baseUrl]   (default http://127.0.0.1:4321)
// Exits 1 if any check fails.
import fs from "node:fs"
import { chromium } from "playwright"

const BASE = (process.argv[2] ?? "http://127.0.0.1:4321").replace(/\/$/, "")
const url = (path) => `${BASE}/guidebook/${path}`
const failures = []
const check = (name, ok, detail = "") => {
  console.log(
    `${ok ? "PASS" : "FAIL"}  ${name}${detail ? `  (${detail})` : ""}`,
  )
  if (!ok) failures.push(name)
}

// Records, per figure id, every pad column that was lit (works across replays).
const recordLitColumns = () => {
  window.__lit = {}
  new MutationObserver((records) => {
    for (const r of records) {
      const el = r.target
      if (!el.classList?.contains("gb-cell-on")) continue
      const fig = el.closest(".gb-figure")?.id
      const col = el.getAttribute("data-col")
      ;(window.__lit[fig] ??= new Set()).add(col)
    }
  }).observe(document, {
    subtree: true,
    attributes: true,
    attributeFilter: ["class"],
  })
}
const lit = (page, id) =>
  page.evaluate((id) => [...(window.__lit[id] ?? [])], id)
const replayVisible = (page, id) =>
  page.locator(`[id="${id}"] .gb-replay`).isVisible()
const open = async (context, path, id) => {
  const page = await context.newPage()
  const errors = []
  page.on("pageerror", (e) => errors.push(e.message))
  page.on("console", (m) => m.type() === "error" && errors.push(m.text()))
  await page.addInitScript(recordLitColumns)
  await page.goto(url(path), { waitUntil: "load" })
  await page.locator(`[id="${id}"]`).scrollIntoViewIfNeeded()
  return { page, errors }
}
const sel = (id) => `[id="${id}"]`
const waitFor = async (fn, ms) => {
  const end = Date.now() + ms
  while (Date.now() < end) {
    if (await fn()) return true
    await new Promise((r) => setTimeout(r, 50))
  }
  return false
}

const checks = {
  async replayClear(browser) {
    const ctx = await browser.newContext({
      viewport: { width: 390, height: 844 },
    })
    const page = await ctx.newPage()
    const overlaps = []
    for (const path of [
      "09-audio/09-sampling-recording-audio-within-kits",
      "03-sequencing/01-sequencing-workflow",
    ]) {
      await page.goto(url(path), { waitUntil: "load" })
      overlaps.push(
        ...(await page.evaluate(() =>
          [...document.querySelectorAll(".gb-figure[data-animate]")].flatMap(
            (fig) => {
              const btn = fig.querySelector(".gb-replay")
              btn.hidden = false
              const b = btn.getBoundingClientRect()
              const a = fig
                .querySelector(".gb-figure-art")
                .getBoundingClientRect()
              const hit =
                b.left < a.right &&
                b.right > a.left &&
                b.top < a.bottom &&
                b.bottom > a.top
              return hit ? [fig.id] : []
            },
          ),
        )),
      )
    }
    check(
      "replay button sits outside the figure art",
      overlaps.length === 0,
      overlaps.join(", "),
    )
    await ctx.close()
  },

  async stepTabStops(browser) {
    const page = await browser.newPage()
    await page.goto(url("08-arranger-view/03-arranger-view-basics"), {
      waitUntil: "load",
    })
    const perList = await page.evaluate(() =>
      [...document.querySelectorAll("ol.sl-steps")].map(
        (ol) => [...ol.children].filter((li) => li.tabIndex === 0).length,
      ),
    )
    check(
      "steps: one tab stop per list (roving tabindex)",
      perList.length > 0 && perList.every((n) => n === 1),
      perList.join(","),
    )
    await page.close()
  },

  async playhead(browser) {
    const ctx = await browser.newContext()
    const { page } = await open(
      ctx,
      "03-sequencing/08-navigating-the-grid",
      "fig-3.8-1",
    )
    const replay = page.locator(`${sel("fig-3.8-1")} .gb-replay`)
    const lightsUp = await waitFor(
      async () => (await lit(page, "fig-3.8-1")).length > 0,
      1000,
    )
    check("playhead: pads light up on 3.8", lightsUp)
    let notesCovered = 0
    for (let t = 0; t < 10; t++) {
      notesCovered += await page
        .locator(`${sel("fig-3.8-1")} .gb-cell-on[data-note]`)
        .count()
      await page.waitForTimeout(60)
    }
    check(
      "playhead: sequenced pads never covered",
      notesCovered === 0,
      `${notesCovered}`,
    )
    const done = await waitFor(() => replay.isVisible(), 5000)
    const left = await page.locator(`${sel("fig-3.8-1")} .gb-cell-on`).count()
    check(
      "playhead: replay appears after one pass, nothing left lit",
      done && left === 0,
      `lit after: ${left}`,
    )
    await ctx.close()
  },

  async drawIn(browser) {
    const ctx = await browser.newContext()
    const { page } = await open(
      ctx,
      "03-sequencing/01-sequencing-workflow",
      "fig-3.1-1",
    )
    const state = () =>
      page.evaluate((s) => {
        const els = [...document.querySelector(s).querySelectorAll("svg *")]
        return {
          running: els
            .flatMap((e) => e.getAnimations())
            .filter((a) => a.playState === "running").length,
          dashed: els.filter(
            (e) => parseFloat(getComputedStyle(e).strokeDashoffset) > 0,
          ).length,
          faded: els.filter(
            (e) =>
              parseFloat(getComputedStyle(e).opacity) <
              parseFloat(e.getAttribute("opacity") ?? "1"),
          ).length,
        }
      }, sel("fig-3.1-1"))
    const early = await state()
    check("draw: 3.1 is animating", early.running > 0, JSON.stringify(early))
    await page.waitForTimeout(3000)
    const end = await state()
    check(
      "draw: 3.1 final state within 3 s",
      end.running === 0 && end.dashed === 0 && end.faded === 0,
      JSON.stringify(end),
    )
    await ctx.close()
  },

  async reducedMotion(browser) {
    const ctx = await browser.newContext({ reducedMotion: "reduce" })
    for (const [path, id] of [
      ["03-sequencing/08-navigating-the-grid", "fig-3.8-1"],
      ["03-sequencing/01-sequencing-workflow", "fig-3.1-1"],
    ]) {
      const { page } = await open(ctx, path, id)
      const faded = await page.evaluate(
        (s) =>
          [...document.querySelector(s).querySelectorAll("svg *")].filter(
            (e) =>
              parseFloat(getComputedStyle(e).opacity) <
              parseFloat(e.getAttribute("opacity") ?? "1"),
          ).length,
        sel(id),
      )
      await page.waitForTimeout(800)
      const litCols = (await lit(page, id)).length
      const replay = await replayVisible(page, id)
      check(
        `reduced motion: ${id} static, no replay`,
        faded === 0 && litCols === 0 && !replay,
        `faded ${faded}, lit ${litCols}, replay ${replay}`,
      )
    }
    await ctx.close()
  },

  async noJs(browser) {
    // Every animated figure in the content, found from its MDX.
    const docs = "src/content/docs/guidebook"
    const animated = fs
      .readdirSync(docs, { recursive: true })
      .filter((f) => f.endsWith(".mdx"))
      .map((f) => [
        f.replace(/\.mdx$/, ""),
        fs.readFileSync(`${docs}/${f}`, "utf8"),
      ])
      .filter(([, mdx]) => mdx.includes("animate="))
    const ctx = await browser.newContext({ javaScriptEnabled: false })
    const page = await ctx.newPage()
    const bad = []
    let figures = 0
    for (const [path] of animated) {
      await page.goto(url(path), { waitUntil: "load" })
      const result = await page.evaluate(() =>
        [...document.querySelectorAll(".gb-figure[data-animate]")].map(
          (fig) => {
            const svg = fig.querySelector("svg")
            const box = svg?.getBoundingClientRect()
            const faded = [...(svg?.querySelectorAll("*") ?? [])].filter(
              (e) =>
                parseFloat(getComputedStyle(e).opacity) <
                parseFloat(e.getAttribute("opacity") ?? "1"),
            ).length
            const replay = !fig.querySelector(".gb-replay")?.hidden
            return {
              id: fig.id,
              ok: box?.width > 0 && box?.height > 0 && faded === 0 && !replay,
            }
          },
        ),
      )
      figures += result.length
      bad.push(...result.filter((r) => !r.ok).map((r) => r.id))
    }
    check(
      "no JS: every animated figure fully drawn, no replay button",
      figures === 24 && bad.length === 0,
      `${figures} figures${bad.length ? `, bad: ${bad.join(", ")}` : ""}`,
    )
    await ctx.close()
  },

  async independentReplay(browser) {
    const ctx = await browser.newContext()
    const path = "03-sequencing/12-parameter-automation-basics"
    const { page } = await open(ctx, path, "fig-3.12-1")
    await page.locator(sel("fig-3.12-2")).scrollIntoViewIfNeeded()
    const both = await waitFor(
      async () =>
        (await replayVisible(page, "fig-3.12-1")) &&
        (await replayVisible(page, "fig-3.12-2")),
      8000,
    )
    await page.evaluate(() => (window.__lit = {}))
    await page.locator(`${sel("fig-3.12-1")} .gb-replay`).click()
    await page.waitForTimeout(600)
    const a = (await lit(page, "fig-3.12-1")).length
    const b = (await lit(page, "fig-3.12-2")).length
    check(
      "two figures: replaying one leaves the other alone",
      both && a > 0 && b === 0,
      `A ${a} cols, B ${b} cols`,
    )
    await ctx.close()
  },

  async allColumns(browser) {
    const ctx = await browser.newContext()
    const id = "fig-3.9-1"
    const { page, errors } = await open(
      ctx,
      "03-sequencing/09-clip-length-position",
      id,
    )
    await waitFor(() => replayVisible(page, id), 6000)
    const tagged = await page.evaluate(
      (s) => [
        ...new Set(
          [...document.querySelectorAll(`${s} .gb-cell`)].map((p) =>
            p.getAttribute("data-col"),
          ),
        ),
      ],
      sel(id),
    )
    const swept = await lit(page, id)
    check(
      "multi-grid: 3.9-1 sweeps every grid column, no errors",
      swept.length === tagged.length && errors.length === 0,
      `${swept.length}/${tagged.length} cols, ${errors.length} errors ${errors.join("; ")}`,
    )
    await ctx.close()
  },

  async stepFocus(browser) {
    const page = await browser.newPage()
    await page.goto(url("05-kits/03-kit-presets-samples"), {
      waitUntil: "load",
    })
    const steps = page.locator("ol.sl-steps").first().locator(":scope > li")
    await steps.nth(2).click()
    const current = () =>
      page
        .locator("ol.sl-steps")
        .first()
        .locator(':scope > li[aria-current="step"]')
        .evaluateAll((els) =>
          els.map((el) => [...el.parentElement.children].indexOf(el)),
        )
    const afterClick = await current()
    check(
      "steps: click marks only step 3",
      afterClick.join() === "2",
      `${afterClick}`,
    )
    await steps.nth(2).press("ArrowDown")
    const afterDown = await current()
    const focused = await page.evaluate(() =>
      [...document.activeElement.parentElement.children].indexOf(
        document.activeElement,
      ),
    )
    check(
      "steps: ArrowDown moves to step 4",
      afterDown.join() === "3" && focused === 3,
      `${afterDown}`,
    )
    const onPage = await page.locator('li[aria-current="step"]').count()
    check("steps: one current step on the page", onPage === 1, `${onPage}`)
    await page.close()
  },
}

const browser = await chromium.launch()
try {
  for (const run of Object.values(checks)) await run(browser)
} finally {
  await browser.close()
}
if (failures.length) {
  console.error(`\n${failures.length} check(s) failed`)
  process.exit(1)
}
console.log("\nall checks passed")
