// Every pre-reorg guidebook URL (src/data/guidebook-redirects.json) has a built redirect page
// pointing at its new URL under the site's base path. Run after `CI=true pnpm build`, with the same
// SITE_BASE_PATH: node scripts/check-guidebook-redirects.mjs
import { existsSync, readFileSync } from "node:fs"
import { join } from "node:path"

const dist = new URL("../dist/", import.meta.url).pathname
const redirects = JSON.parse(
  readFileSync(
    new URL("../src/data/guidebook-redirects.json", import.meta.url),
  ),
)
const base = (process.env.SITE_BASE_PATH || "").replace(/\/$/, "")
const missing = []
for (const [from, to] of Object.entries(redirects)) {
  const file = join(dist, from, "index.html")
  if (!existsSync(file)) {
    missing.push(`${from}: no redirect page`)
    continue
  }
  const html = readFileSync(file, "utf8")
  const target = /http-equiv="refresh" content="\d+;url=([^"]+)"/.exec(
    html,
  )?.[1]
  if (target?.replaceAll("&#38;", "&") !== base + to)
    missing.push(`${from}: points at ${target}, not ${base + to}`)
}
for (const m of missing) console.error(m)
console.log(
  `${Object.keys(redirects).length} redirects, ${missing.length} missing`,
)
process.exit(missing.length ? 1 : 0)
