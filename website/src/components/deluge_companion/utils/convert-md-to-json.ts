import * as fs from "fs"
import { extname } from "node:path"
import { parseMd } from "../lib/md-convert.js"

const inputPath = "./src/components/deluge_companion/data/shortcuts/"
const outputPath = "./src/components/deluge_companion/data/v4.1.0.json"

function regenerateJson() {
  const inputPathFiles = fs.readdirSync(inputPath)
  const mdFiles = inputPathFiles.filter((file) => extname(file) === ".md")

  const output = mdFiles.flatMap((filename) => {
    const mdContent = fs.readFileSync(inputPath + filename, "utf-8")
    return parseMd(mdContent)
  })

  fs.writeFileSync(outputPath, JSON.stringify(output, null, 2))
  console.log("DONE.")
}

const watchMode = process.argv.includes("--watch")

regenerateJson()

if (watchMode) {
  console.log("Watching markdown shortcut files for changes...")
  fs.watch(inputPath, { persistent: true }, (eventType, filename) => {
    if (!filename || extname(filename) !== ".md") {
      return
    }
    console.log(`[${eventType}] ${filename} -> regenerating JSON`)
    regenerateJson()
  })
}
