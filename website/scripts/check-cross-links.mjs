// Post-build check and report for Guidebook cross-links (remark-cross-links).
//
// Every `a.xref` link in the built Guidebook must point at a built page, and its #anchor must
// exist on that page (menu item anchors live in component HTML the links validator can't see).
// Prints how many links each term got, then exits non-zero if any link is broken.
//
// Usage: node scripts/check-cross-links.mjs [distDir]   (after `pnpm run build`)
import { existsSync, readFileSync, readdirSync, statSync } from "node:fs"
import path from "node:path"

const dist = path.resolve(process.argv[2] ?? "dist")
const base = (process.env.SITE_BASE_PATH ?? "").replace(/\/$/, "")

const htmlFiles = (dir) =>
  readdirSync(dir).flatMap((name) => {
    const p = path.join(dir, name)
    return statSync(p).isDirectory()
      ? htmlFiles(p)
      : p.endsWith(".html")
        ? [p]
        : []
  })

const pageFile = (href) => {
  const rel = href.slice(base.length).replace(/^\/+|\/+$/g, "")
  for (const f of [
    path.join(dist, rel, "index.html"),
    path.join(dist, `${rel}.html`),
  ]) {
    if (existsSync(f)) return f
  }
  return null
}

const idsCache = new Map()
const idsOf = (file) => {
  if (!idsCache.has(file)) {
    const ids = new Set(
      [...readFileSync(file, "utf8").matchAll(/\sid="([^"]+)"/g)].map(
        (m) => m[1],
      ),
    )
    idsCache.set(file, ids)
  }
  return idsCache.get(file)
}

const decode = (s) =>
  s.replaceAll("&amp;", "&").replaceAll("&#39;", "'").replaceAll("&quot;", '"')

const guidebookDir = path.join(dist, "guidebook")
if (!existsSync(guidebookDir)) {
  console.error(`No built guidebook at ${guidebookDir}; run the build first.`)
  process.exit(2)
}

const counts = { concept: new Map(), menu: new Map() }
const broken = []
let total = 0
for (const file of htmlFiles(guidebookDir)) {
  const html = readFileSync(file, "utf8")
  for (const m of html.matchAll(
    /<a\b[^>]*class="xref xref-(concept|menu)"[^>]*>([^<]*)<\/a>/g,
  )) {
    const [tag, kind, text] = m
    const href = decode(tag.match(/href="([^"]*)"/)?.[1] ?? "")
    total++
    const [page, hash] = href.split("#")
    const target = page ? pageFile(page) : file
    if (!target)
      broken.push(`${path.relative(dist, file)}: ${href} (no such page)`)
    else if (hash && !idsOf(target).has(decodeURIComponent(hash)))
      broken.push(
        `${path.relative(dist, file)}: ${href} (no #${hash} on that page)`,
      )
    const key = `${text.trim()} → ${href.replace(base, "")}`
    counts[kind].set(key, (counts[kind].get(key) ?? 0) + 1)
  }
}

for (const kind of ["concept", "menu"]) {
  const rows = [...counts[kind]].sort((a, b) => b[1] - a[1])
  console.log(
    `\n${kind} links: ${rows.reduce((n, r) => n + r[1], 0)} (${rows.length} distinct)`,
  )
  for (const [key, n] of rows) console.log(`  ${String(n).padStart(3)}  ${key}`)
}
console.log(
  `\n${total} cross-links in the built Guidebook, ${broken.length} broken.`,
)
if (broken.length) {
  for (const b of broken) console.error(`  ✗ ${b}`)
  process.exit(1)
}
