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
      "09-kits/05-sampling-recording-audio-within-kits",
      "04-sequencing/01-sequencing-workflow",
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
    await page.goto(url("14-arranger-view/03-arranger-view-basics"), {
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
      "04-sequencing/10-navigating-the-grid",
      "fig-4.10-1",
    )
    const replay = page.locator(`${sel("fig-4.10-1")} .gb-replay`)
    const lightsUp = await waitFor(
      async () => (await lit(page, "fig-4.10-1")).length > 0,
      1000,
    )
    check("playhead: pads light up on 3.8", lightsUp)
    let notesCovered = 0
    for (let t = 0; t < 10; t++) {
      notesCovered += await page
        .locator(`${sel("fig-4.10-1")} .gb-cell-on[data-note]`)
        .count()
      await page.waitForTimeout(60)
    }
    check(
      "playhead: sequenced pads never covered",
      notesCovered === 0,
      `${notesCovered}`,
    )
    const done = await waitFor(() => replay.isVisible(), 5000)
    const left = await page.locator(`${sel("fig-4.10-1")} .gb-cell-on`).count()
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
      "04-sequencing/01-sequencing-workflow",
      "fig-4.1-1",
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
      }, sel("fig-4.1-1"))
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
      ["04-sequencing/10-navigating-the-grid", "fig-4.10-1"],
      ["04-sequencing/01-sequencing-workflow", "fig-4.1-1"],
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
      // 26 before the panel-figure pilot moved 3.2-1, 3.9-1 and 3.14-1 to PanelFigure
      figures === 23 && bad.length === 0,
      `${figures} figures${bad.length ? `, bad: ${bad.join(", ")}` : ""}`,
    )
    await ctx.close()
  },

  async independentReplay(browser) {
    const ctx = await browser.newContext()
    const path = "06-automation/01-parameter-automation-basics"
    const { page } = await open(ctx, path, "fig-6.1-1")
    await page.locator(sel("fig-6.1-2")).scrollIntoViewIfNeeded()
    const both = await waitFor(
      async () =>
        (await replayVisible(page, "fig-6.1-1")) &&
        (await replayVisible(page, "fig-6.1-2")),
      8000,
    )
    await page.evaluate(() => (window.__lit = {}))
    await page.locator(`${sel("fig-6.1-1")} .gb-replay`).click()
    await page.waitForTimeout(600)
    const a = (await lit(page, "fig-6.1-1")).length
    const b = (await lit(page, "fig-6.1-2")).length
    check(
      "two figures: replaying one leaves the other alone",
      both && a > 0 && b === 0,
      `A ${a} cols, B ${b} cols`,
    )
    await ctx.close()
  },

  async allColumns(browser) {
    const ctx = await browser.newContext()
    // 3.2-3: three direction grids (3.9-1 became a panel figure)
    const id = "fig-4.2-3"
    const { page, errors } = await open(
      ctx,
      "04-sequencing/02-playing-a-sequence",
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
      "multi-grid: 3.2-3 sweeps every grid column, no errors",
      swept.length === tagged.length && errors.length === 0,
      `${swept.length}/${tagged.length} cols, ${errors.length} errors ${errors.join("; ")}`,
    )
    await ctx.close()
  },

  async panelFigures(browser) {
    const fig = sel("fig-4.2-1")
    // The figure renders one SVG; the page script records the step it shows in data-step.
    const shown = (page) =>
      page.evaluate((s) => {
        const f = document.querySelector(s)
        return `${f.querySelectorAll("svg.gb-panel-art").length} svg, step ${f.dataset.step ?? "none"}`
      }, fig)

    const page = await browser.newPage()
    await page.goto(url("04-sequencing/02-playing-a-sequence"), {
      waitUntil: "load",
    })
    // The demo starts collapsed; its summary opens it.
    const art = page.locator(`${fig} .gb-panel-art-wrap`)
    check(
      "panel: the step demo starts collapsed",
      !(await art.isVisible()) &&
        (await page.locator(`${fig} details.gb-panel-demo`).count()) === 1,
    )
    await page.locator(`${fig} summary`).click()
    check("panel: opening the demo shows the panel", await art.isVisible())
    check(
      "panel: before any step, the final state shows (one SVG, no step)",
      (await shown(page)) === "1 svg, step none",
      await shown(page),
    )
    await page
      .locator("ol.sl-steps")
      .first()
      .locator(":scope > li")
      .nth(1)
      .click()
    const play = await page.evaluate((s) => {
      const el = document.querySelector(`${s} [data-c="Play"]`)
      const art = document.querySelector(`${s} .gb-panel-art-wrap`)
      const by = art?.getAttribute("aria-describedby")
      return {
        cls: el?.getAttribute("class") ?? "",
        described: by ? (document.getElementById(by)?.textContent ?? "") : "",
      }
    }, fig)
    check(
      "panel: step 2 shows frame 2 with PLAY lit green and highlighted",
      (await shown(page)) === "1 svg, step 1" &&
        /led-green/.test(play.cls) &&
        /hl-press/.test(play.cls),
      `${await shown(page)}; ${play.cls}`,
    )
    check(
      "panel: the frame is described by its step",
      /start playback/.test(play.described),
      play.described.slice(0, 60),
    )
    await page.close()

    // 3.2-1's window grows to include the horizontal encoder its step 3 uses.
    const pw = await browser.newPage()
    await pw.goto(url("04-sequencing/02-playing-a-sequence"), {
      waitUntil: "load",
    })
    await pw.locator(`${fig} summary`).click()
    const viewBoxX = await pw.evaluate(
      (s) =>
        Number(
          document
            .querySelector(`${s} svg.gb-panel-art`)
            .getAttribute("viewBox")
            .split(" ")[0],
        ),
      fig,
    )
    check(
      "panel: 3.2-1 shows the horizontal encoder its steps use",
      viewBoxX <= 987 - 180,
      `viewBox x ${viewBoxX}`,
    )

    // Play walks through the steps, the list following, and stops after the last.
    const state = () =>
      pw.evaluate((s) => {
        const f = document.querySelector(s)
        const cur = [
          ...document.querySelectorAll("ol.sl-steps"),
        ][0].querySelector(':scope > li[aria-current="step"]')
        return {
          step: f.dataset.step ?? "none",
          current: cur ? [...cur.parentElement.children].indexOf(cur) : -1,
          button: f.querySelector(".gb-panel-play").textContent.trim(),
        }
      }, fig)
    await pw.locator(`${fig} .gb-panel-play`).click()
    await new Promise((r) => setTimeout(r, 2000))
    const mid = await state()
    check(
      "panel: play advances the figure and the list together",
      mid.step === "1" && mid.current === 1 && mid.button.includes("Stop"),
      JSON.stringify(mid),
    )
    await new Promise((r) => setTimeout(r, 3500))
    const end = await state()
    check(
      "panel: play stops on the last step",
      end.step === "2" && end.button.includes("Play"),
      JSON.stringify(end),
    )
    await pw.close()

    // 3.14-1 sits under an "Example" sub-heading after its Steps: still bound, 6 steps.
    const p1 = await browser.newPage()
    await p1.goto(url("04-sequencing/13-euclidean-sequencing"), {
      waitUntil: "load",
    })
    const bound = await p1.evaluate(
      () => document.getElementById("fig-4.13-1")?.dataset.steps ?? "unbound",
    )
    check("panel: 3.14-1 is bound to its 6 steps", bound === "6", bound)
    await p1.close()

    const noJs = await browser.newContext({ javaScriptEnabled: false })
    const p2 = await noJs.newPage()
    await p2.goto(url("04-sequencing/02-playing-a-sequence"), {
      waitUntil: "load",
    })
    check(
      "panel: without JS there is no play button",
      !(await p2.locator(`${fig} .gb-panel-play`).isVisible()),
    )
    check(
      "panel: without JS the final state shows (one SVG, no step)",
      (await shown(p2)) === "1 svg, step none",
      await shown(p2),
    )
    await p2.goto(url("04-sequencing/03-tempo-swing"), { waitUntil: "load" })
    check(
      "demo: without JS auto demos are not shown",
      !(await p2.locator("#panel-demo-1").isVisible()),
    )
    await noJs.close()

    // Auto demos (added under every :key Steps): collapsed, SVG loaded on open, steps drive it.
    const pa = await browser.newPage()
    await pa.goto(url("04-sequencing/03-tempo-swing"), { waitUntil: "load" })
    const demo = "#panel-demo-1"
    const demoState = () =>
      pa.evaluate((s) => {
        const f = document.querySelector(s)
        return {
          hidden: f.hidden,
          open: f.querySelector("details").open,
          svgs: f.querySelectorAll("svg.gb-panel-art").length,
          tempo:
            f
              .querySelector('[data-c="Tempo-Encoder"]')
              ?.getAttribute("class") ?? "",
        }
      }, demo)
    const before = await demoState()
    check(
      "demo: auto demo shows collapsed, with no SVG until opened",
      !before.hidden && !before.open && before.svgs === 0,
      JSON.stringify(before),
    )
    await pa.locator(`${demo} summary`).click()
    await pa.waitForSelector(`${demo} svg.gb-panel-art`)
    const opened = await demoState()
    check(
      "demo: opening loads the panel with the steps' controls marked",
      opened.svgs === 1 && /\bhl\b/.test(opened.tempo),
      JSON.stringify(opened),
    )
    await pa
      .locator("ol.sl-steps")
      .first()
      .locator(":scope > li")
      .nth(3)
      .click()
    const last = await demoState()
    check(
      "demo: a step with no controls clears the marks",
      !/\bhl\b/.test(last.tempo),
      JSON.stringify(last),
    )
    await pa.close()

    const still = await browser.newContext({ reducedMotion: "reduce" })
    const p3 = await still.newPage()
    await p3.goto(url("04-sequencing/02-playing-a-sequence"), {
      waitUntil: "load",
    })
    const animations = await p3.evaluate(
      (s) =>
        [...document.querySelectorAll(`${s} .hl, ${s} .led-blink`)]
          .map((el) => getComputedStyle(el).animationName)
          .filter((n) => n !== "none").length,
      fig,
    )
    check(
      "panel: reduced motion stops highlight animation",
      animations === 0,
      `${animations}`,
    )
    await still.close()

    const narrow = await browser.newContext({
      viewport: { width: 390, height: 900 },
    })
    const p4 = await narrow.newPage()
    await p4.goto(url("03-clips/11-clip-view-parameter-affect-group-changes"), {
      waitUntil: "load",
    })
    const callouts = await p4.evaluate(() =>
      [
        ...document.querySelectorAll('[id="fig-3.11-1"] .gb-panel-callouts li'),
      ].map((li) => getComputedStyle(li).position),
    )
    check(
      "panel: 2.8-1 callouts become a list on narrow screens",
      callouts.length === 4 && callouts.every((pos) => pos === "static"),
      `${callouts}`,
    )
    await narrow.close()

    const wide = await browser.newContext({
      viewport: { width: 1400, height: 1000 },
    })
    const p5 = await wide.newPage()
    await p5.goto(url("03-clips/11-clip-view-parameter-affect-group-changes"), {
      waitUntil: "load",
    })
    const layout = await p5.evaluate(() => {
      const fig = document.getElementById("fig-3.11-1")
      const boxes = [...fig.querySelectorAll(".gb-panel-callouts li")].map(
        (li) => {
          const r = li.getBoundingClientRect()
          return { side: li.dataset.side, top: r.top, bottom: r.bottom }
        },
      )
      const overlaps = boxes.some((a, i) =>
        boxes.some(
          (b, j) =>
            i < j && a.side === b.side && a.top < b.bottom && b.top < a.bottom,
        ),
      )
      return { anchored: fig.classList.contains("gb-panel-anchored"), overlaps }
    })
    check(
      "panel: wide 2.8-1 callouts never overlap (beside the panel when they fit, else a list)",
      !(layout.anchored && layout.overlaps),
      JSON.stringify(layout),
    )
    await wide.close()
  },

  async stepFocus(browser) {
    const page = await browser.newPage()
    await page.goto(url("09-kits/04-kit-presets-samples"), {
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

  async envelope(browser) {
    const path = "10-modulation/03-envelopes-and-low-frequency-oscillators"
    const id = "fig-10.3-1"
    const fig = sel(id)
    const curve = (page) =>
      page.locator(`${fig} .gb-env-curve`).getAttribute("d")

    const noJs = await browser.newContext({ javaScriptEnabled: false })
    const still = await noJs.newPage()
    await still.goto(url(path), { waitUntil: "load" })
    check(
      "envelope: without JS, a still curve with every stage named",
      ((await curve(still)) ?? "").length > 100 &&
        (await still.locator(`${fig} .gb-env-stage`).count()) === 4,
    )
    check(
      "envelope: without JS, readouts read-only, no Hold note or TRY IT",
      (await still.locator(`${fig} input[data-param]:disabled`).count()) ===
        4 &&
        !(await still.locator(`${fig} .gb-env-hold`).isVisible()) &&
        !(await still.locator(`${fig} .gb-env-sticker`).isVisible()),
    )
    await noJs.close()

    const ctx = await browser.newContext()
    const { page, errors } = await open(ctx, path, id)
    check(
      "envelope: a TRY IT note points at the controls",
      await page.locator(`${fig} .gb-env-sticker`).isVisible(),
    )
    const before = await curve(page)
    const attack = page.locator(`${fig} input[data-param="attack"]`)
    const start = Number(await attack.inputValue())
    await attack.focus()
    await attack.press("ArrowRight")
    const out = await page
      .locator(`${fig} label:has([data-param="attack"]) output`)
      .textContent()
    check(
      "envelope: arrow key on a readout changes the stage and redraws",
      out === String(start + 1) && (await curve(page)) !== before,
      `${out}`,
    )

    const hold = page.locator(`${fig} .gb-env-hold`)
    const box = await hold.boundingBox()
    await page.mouse.move(box.x + box.width / 2, box.y + box.height / 2)
    await page.mouse.down()
    const playing = await waitFor(
      async () => (await page.locator(`${fig} .gb-env-dot`).count()) === 1,
      1000,
    )
    const offWhileHeld = await page
      .locator(`${fig} text:text-is("Note OFF")`)
      .count()
    check(
      "envelope: holding the note plays it, no note off yet",
      playing &&
        offWhileHeld === 0 &&
        (await hold.getAttribute("aria-pressed")) === "true",
    )
    await page.waitForTimeout(400)
    await page.mouse.up()
    const released = await waitFor(
      async () =>
        (await page.locator(`${fig} text:text-is("Note OFF")`).count()) === 1,
      1000,
    )
    check(
      "envelope: letting go marks note off and releases",
      released && (await hold.getAttribute("aria-pressed")) === "false",
    )

    await page.locator(`${fig} input[value="other"]`).check()
    await attack.press("ArrowLeft") // stops the note, redraws still
    const sustainCaption = await page
      .locator(`${fig} label:has([data-param="sustain"]) .gb-env-oled-caption`)
      .textContent()
    check(
      "envelope: other destination is bipolar about the destination value",
      (await page.locator(`${fig} .gb-env-dest`).count()) === 1 &&
        /^(= dest\. value|[+−]\d+ % of depth)$/.test(sustainCaption),
      `${sustainCaption}`,
    )
    check("envelope: no script errors", errors.length === 0, errors.join("; "))
    await ctx.close()
  },

  async lfo(browser) {
    const path = "10-modulation/03-envelopes-and-low-frequency-oscillators"
    const id = "fig-10.3-2"
    const fig = sel(id)
    const traces = (page) =>
      page
        .locator(`${fig} .gb-lfo-curve`)
        .evaluateAll((els) => els.map((e) => e.getAttribute("d")))

    const noJs = await browser.newContext({ javaScriptEnabled: false })
    const still = await noJs.newPage()
    await still.goto(url(path), { waitUntil: "load" })
    const names = await still.locator(`${fig} .gb-lfo-name`).allTextContents()
    check(
      "lfo: without JS, all seven shapes drawn and named, no Pause",
      names.join() ===
        "Sine,Triangle,Square,Saw,Sample & Hold,Random Walk,Warbler" &&
        (await traces(still)).every((d) => (d ?? "").length > 100) &&
        !(await still.locator(`${fig} .gb-lfo-pause`).isVisible()),
      names.join(),
    )
    await noJs.close()

    const ctx = await browser.newContext()
    const { page, errors } = await open(ctx, path, id)
    const a = await traces(page)
    await page.waitForTimeout(400)
    const b = await traces(page)
    check(
      "lfo: the signals advance",
      a.every((d, i) => d !== b[i]),
    )
    await page.locator(`${fig} .gb-lfo-pause`).click()
    await page.waitForTimeout(100)
    const c = await traces(page)
    await page.waitForTimeout(400)
    const d = await traces(page)
    check(
      "lfo: Pause stops them",
      c.every((x, i) => x === d[i]) &&
        (await page.locator(`${fig} .gb-lfo-pause`).textContent()) === "Play",
    )
    check("lfo: no script errors", errors.length === 0, errors.join("; "))
    await ctx.close()

    const calm = await browser.newContext({ reducedMotion: "reduce" })
    const r = await open(calm, path, id)
    const e = await traces(r.page)
    await r.page.waitForTimeout(400)
    check(
      "lfo: reduced motion starts paused",
      (await traces(r.page)).every((x, i) => x === e[i]) &&
        (await r.page.locator(`${fig} .gb-lfo-pause`).textContent()) === "Play",
    )
    await calm.close()
  },

  async osc(browser) {
    const path = "07-synthesizers/06-oscillators"
    const digital = sel("fig-7.6-2")
    const analog = sel("fig-7.6-4")
    const traces = (page, fig) =>
      page
        .locator(`${fig} .gb-lfo-curve`)
        .evaluateAll((els) => els.map((e) => e.getAttribute("d")))
    const names = (page, fig) =>
      page.locator(`${fig} .gb-lfo-name`).allTextContents()

    const noJs = await browser.newContext({ javaScriptEnabled: false })
    const still = await noJs.newPage()
    await still.goto(url(path), { waitUntil: "load" })
    const dn = (await names(still, digital)).join()
    const an = (await names(still, analog)).join()
    const [dSq] = (await traces(still, digital)).slice(3)
    const [aSq] = (await traces(still, analog)).slice(1)
    check(
      "osc: without JS, the book's shapes drawn and named, no Pause",
      dn === "Triangle,Sine,Sawtooth,Square" &&
        an === "Sawtooth,ASquare" &&
        [
          ...(await traces(still, digital)),
          ...(await traces(still, analog)),
        ].every((d) => (d ?? "").length > 100) &&
        !(await still.locator(`${digital} .gb-lfo-pause`).isVisible()),
      `${dn} | ${an}`,
    )
    check(
      "osc: the analog square is the firmware's recording, not the digital square",
      dSq !== aSq,
    )
    await noJs.close()

    const ctx = await browser.newContext()
    const { page, errors } = await open(ctx, path, "fig-7.6-2")
    const a = await traces(page, digital)
    await page.waitForTimeout(400)
    const b = await traces(page, digital)
    check(
      "osc: the signals advance",
      a.every((d, i) => d !== b[i]),
    )
    await page.locator(`${digital} .gb-lfo-pause`).click()
    await page.waitForTimeout(100)
    const c = await traces(page, digital)
    await page.waitForTimeout(400)
    check(
      "osc: Pause stops them",
      (await traces(page, digital)).every((x, i) => x === c[i]) &&
        (await page.locator(`${digital} .gb-lfo-pause`).textContent()) ===
          "Play",
    )
    await page.locator(analog).scrollIntoViewIfNeeded()
    const e = await traces(page, analog)
    await page.waitForTimeout(400)
    check(
      "osc: the analog figure runs on its own",
      (await traces(page, analog)).every((x, i) => x !== e[i]),
    )
    check("osc: no script errors", errors.length === 0, errors.join("; "))
    await ctx.close()

    const calm = await browser.newContext({ reducedMotion: "reduce" })
    const r = await open(calm, path, "fig-7.6-2")
    const f = await traces(r.page, digital)
    await r.page.waitForTimeout(400)
    check(
      "osc: reduced motion starts paused",
      (await traces(r.page, digital)).every((x, i) => x === f[i]) &&
        (await r.page.locator(`${digital} .gb-lfo-pause`).textContent()) ===
          "Play",
    )
    await calm.close()
  },

  async index(browser) {
    const path = "index-a-z"
    const visible = (page, sel) =>
      page
        .locator(sel)
        .evaluateAll((els) =>
          els
            .filter((e) => e.offsetParent !== null)
            .map((e) => e.textContent.trim()),
        )

    const noJs = await browser.newContext({ javaScriptEnabled: false })
    const still = await noJs.newPage()
    await still.goto(url(path), { waitUntil: "load" })
    const heads = await visible(still, ".gb-index-head")
    check(
      "index: without JS, every entry listed and no filter box",
      heads.length > 100 &&
        heads.includes("Clip") &&
        !(await still.locator(".gb-index-filter").isVisible()),
      `${heads.length} entries`,
    )
    await noJs.close()

    const ctx = await browser.newContext()
    const page = await ctx.newPage()
    const errors = []
    page.on("pageerror", (e) => errors.push(String(e)))
    await page.goto(url(path), { waitUntil: "load" })
    const input = page.locator(".gb-index-filter input")
    await input.fill("delete")
    const delHeads = await visible(page, ".gb-index-head")
    const delSubs = await visible(page, ".gb-index-subs .gb-index-label")
    check(
      "index: filtering for 'delete' keeps its see entry and the deleting tasks, drops the rest",
      delHeads.includes("Delete") &&
        delSubs.includes("Deleting a clip from within session view") &&
        !delHeads.includes("Swing") &&
        delSubs.every((s) => /delet/i.test(s)),
      `${delHeads.length} entries, ${delSubs.length} subentries`,
    )
    await input.fill("swing")
    const swing = await visible(page, ".gb-index-head")
    check(
      "index: a headword match shows the whole entry",
      swing.includes("Swing") &&
        (await visible(page, ".gb-index-subs .gb-index-label")).includes(
          "Adjusting the swing interval setting",
        ),
    )
    await input.fill("zzqx")
    check(
      "index: no match says so",
      (await visible(page, ".gb-index-head")).length === 0 &&
        (await page.locator(".gb-index-none").isVisible()),
    )
    await input.fill("")
    check(
      "index: clearing the filter shows everything again",
      (await visible(page, ".gb-index-head")).length === heads.length,
    )
    check("index: no script errors", errors.length === 0, errors.join("; "))
    await ctx.close()
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
