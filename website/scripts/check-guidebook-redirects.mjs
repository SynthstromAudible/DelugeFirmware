// Every pre-reorg guidebook URL (src/data/guidebook-redirects.json) has a built redirect page
// pointing at its new URL. Run after `CI=true pnpm build`: node scripts/check-guidebook-redirects.mjs
import { existsSync, readFileSync } from "node:fs"
import { join } from "node:path"

const dist = new URL("../dist/", import.meta.url).pathname
const redirects = JSON.parse(
  readFileSync(
    new URL("../src/data/guidebook-redirects.json", import.meta.url),
  ),
)
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
  if (!target || !target.replaceAll("&#38;", "&").endsWith(to))
    missing.push(`${from}: points at ${target}, not ${to}`)
}
for (const m of missing) console.error(m)
console.log(
  `${Object.keys(redirects).length} redirects, ${missing.length} missing`,
)
process.exit(missing.length ? 1 : 0)
