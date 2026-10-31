// Checks the Guidebook's interactive behaviour against a running `astro preview`.
//   node scripts/check-guidebook-animations.mjs [baseUrl]   (default http://127.0.0.1:4321)
// Exits 1 if any check fails.
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

const checks = {
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
