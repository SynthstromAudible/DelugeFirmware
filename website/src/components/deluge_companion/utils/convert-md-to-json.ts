import * as fs from "fs"
import { extname, basename, join } from "node:path"
import { parseMd } from "../lib/md-convert.js"
import type { Shortcut } from "../types/shortcut.js"

const inputPath = "./src/components/deluge_companion/data/shortcuts/"
const outputPath = "./src/components/deluge_companion/data/v4.1.0.json"

function getShortcutMarkdownFiles(dirPath: string): string[] {
  // Returns recursively discovered, sorted markdown files matching shortcut naming rules.
  const entries = fs
    .readdirSync(dirPath, { withFileTypes: true })
    .sort((a, b) => a.name.localeCompare(b.name))

  const files: string[] = []

  for (const entry of entries) {
    const fullPath = join(dirPath, entry.name)

    if (entry.isDirectory()) {
      files.push(...getShortcutMarkdownFiles(fullPath))
      continue
    }

    if (
      entry.isFile() &&
      extname(entry.name) === ".md" &&
      /^\d{2}[-_].+\.md$/.test(entry.name)
    ) {
      files.push(fullPath)
    }
  }

  return files
}

// Returns JSON text formatted for repo-friendly diffs.
function stringifyShortcutData(shortcuts: Shortcut[]) {
  return `${JSON.stringify(shortcuts, null, 2).replace(
    /^(\s*)"(views|firmware)": \[\n((?:\s+\d+,?\n)+)\1\]/gm,
    (_, indent: string, key: string, values: string) => {
      const compactValues = values
        .trim()
        .split("\n")
        .map((line) => line.trim().replace(/,$/, ""))
        .filter(Boolean)
        .join(", ")

      return `${indent}"${key}": [${compactValues}]`
    },
  )}\n`
}

// Returns void after regenerating the shortcuts JSON file from markdown sources.
function regenerateJson() {
  const mdFiles = getShortcutMarkdownFiles(inputPath)

  const output = mdFiles.flatMap((filePath) => {
    const mdContent = fs.readFileSync(filePath, "utf-8")
    const filename = basename(filePath)
    const subCapabilityId = filename.replace(/\.md$/, "")
    const shortcuts = parseMd(mdContent, { subCapabilityId })

    return shortcuts.map((shortcut) => {
      if (!shortcut.capabilityParentId) {
        throw new Error(
          `Shortcut file "${filePath}" is missing "capability: ..." metadata at the top of the file.`,
        )
      }

      return shortcut
    })
  })

  fs.writeFileSync(outputPath, stringifyShortcutData(output))
  console.log("DONE.")
}

const watchMode = process.argv.includes("--watch")

regenerateJson()

if (watchMode) {
  console.log("Watching markdown shortcut files for changes...")
  fs.watch(inputPath, { persistent: true, recursive: true }, (eventType, filename) => {
    if (!filename || extname(filename) !== ".md") {
      return
    }
    console.log(`[${eventType}] ${filename} -> regenerating JSON`)
    regenerateJson()
  })
}
