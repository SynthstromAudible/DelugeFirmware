// Every guidebook link in the built site resolves: the target page exists and, for an #anchor,
// the page has an element with that id. Run after `CI=true pnpm build`, with the same
// SITE_BASE_PATH: node scripts/check-guidebook-links.mjs [dist dir]
import { existsSync, readFileSync, readdirSync, statSync } from "node:fs"
import { join } from "node:path"

const dist = process.argv[2] ?? new URL("../dist/", import.meta.url).pathname
const base = (process.env.SITE_BASE_PATH || "").replace(/\/$/, "")
const pages = (dir) =>
  readdirSync(dir).flatMap((name) => {
    const p = join(dir, name)
    return statSync(p).isDirectory()
      ? pages(p)
      : name === "index.html"
        ? [p]
        : []
  })
const ids = new Map()
const idsOf = (file) => {
  if (!ids.has(file)) {
    const html = readFileSync(file, "utf8")
    ids.set(
      file,
      new Set([...html.matchAll(/\sid="([^"]+)"/g)].map((m) => m[1])),
    )
  }
  return ids.get(file)
}
const isRedirect = (html) => html.includes('http-equiv="refresh"')

let links = 0
let prefixed = 0
const unique = new Set()
const problems = []
for (const file of pages(join(dist, "guidebook"))) {
  const html = readFileSync(file, "utf8")
  if (isRedirect(html)) continue
  const here = file
    .slice(dist.length - 1, -"index.html".length)
    .replace(/\/$/, "")
  for (const [, href] of html.matchAll(/\shref="([^"]+)"/g)) {
    if (!href.startsWith(`${base}/guidebook`) && !href.startsWith("#")) continue
    links++
    if (!href.startsWith("#")) prefixed++
    const [path, anchor] = href.startsWith("#")
      ? [here, href.slice(1)]
      : href.slice(base.length).split("#")
    const key = `${path}#${anchor ?? ""}`
    if (unique.has(key)) continue
    unique.add(key)
    const target = join(dist, path, "index.html")
    if (!existsSync(target)) {
      problems.push(`${here}: ${href}: no page`)
      continue
    }
    if (isRedirect(readFileSync(target, "utf8"))) {
      problems.push(`${here}: ${href}: links to a redirect`)
      continue
    }
    if (anchor && !idsOf(target).has(decodeURIComponent(anchor)))
      problems.push(`${here}: ${href}: no #${anchor}`)
  }
}
if (!prefixed)
  problems.push(
    `no links to ${base}/guidebook found: is SITE_BASE_PATH the one the site was built with?`,
  )
for (const p of problems) console.error(p)
console.log(
  `${links} links, ${unique.size} unique, ${problems.length} problems`,
)
process.exit(problems.length ? 1 : 0)
