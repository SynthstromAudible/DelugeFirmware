import type { APIRoute } from "astro"
import { readFile } from "node:fs/promises"
import path from "node:path"

/** Dev-only: serves PDF page renders from GUIDEBOOK_PDF_DIR. */
export function getStaticPaths() {
  const dir = process.env.GUIDEBOOK_PDF_DIR
  if (!import.meta.env.DEV || !dir) return []
  return Array.from({ length: 338 }, (_, i) => ({ params: { n: `${i + 1}` } }))
}

export const GET: APIRoute = async ({ params }) => {
  const dir = process.env.GUIDEBOOK_PDF_DIR ?? ""
  const file = path.join(dir, `page-${String(params.n).padStart(3, "0")}.png`)
  return new Response(new Uint8Array(await readFile(file)), {
    headers: { "Content-Type": "image/png" },
  })
}
