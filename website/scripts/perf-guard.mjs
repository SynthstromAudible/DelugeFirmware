import { spawn } from "node:child_process"
import process from "node:process"
import { chromium } from "playwright"

const config = {
  runs: Number(process.env.PERF_RUNS ?? 3),
  port: Number(process.env.PERF_PORT ?? 4322),
  path: process.env.PERF_PATH ?? "/reference/deluge_companion",
  externalUrl: process.env.PERF_URL,
  maxMedianInteractiveMs: Number(process.env.MAX_MEDIAN_INTERACTIVE_MS ?? 1300),
  maxMedianDclMs: Number(process.env.MAX_MEDIAN_DCL_MS ?? 300),
  maxAvgRequests: Number(process.env.MAX_AVG_REQUESTS ?? 18),
  maxAvgScripts: Number(process.env.MAX_AVG_SCRIPTS ?? 11),
  maxAvgScriptTransferBytes: Number(
    process.env.MAX_AVG_SCRIPT_TRANSFER_BYTES ?? 130000,
  ),
}

function average(values) {
  return values.reduce((acc, value) => acc + value, 0) / values.length
}

function median(values) {
  const sorted = [...values].sort((a, b) => a - b)
  const middle = Math.floor(sorted.length / 2)
  if (sorted.length % 2 === 0) {
    return (sorted[middle - 1] + sorted[middle]) / 2
  }
  return sorted[middle]
}

function normalizePath(path) {
  if (!path.startsWith("/")) {
    return `/${path}`
  }
  return path
}

function makeUrl(baseUrl, path) {
  const normalizedPath = normalizePath(path)
  return new URL(normalizedPath, baseUrl).toString()
}

async function waitForHttp(url, timeoutMs = 60000) {
  const startedAt = Date.now()
  // eslint-disable-next-line no-constant-condition
  while (true) {
    try {
      const response = await fetch(url)
      if (response.ok || response.status === 404) {
        return
      }
    } catch {
      // Server may not be ready yet.
    }

    if (Date.now() - startedAt > timeoutMs) {
      throw new Error(`Timed out waiting for server at ${url}`)
    }

    await new Promise((resolve) => setTimeout(resolve, 500))
  }
}

function startPreviewServer(port) {
  const child = spawn(
    "pnpm",
    ["astro", "preview", "--host", "127.0.0.1", "--port", String(port)],
    {
      stdio: "inherit",
      shell: false,
    },
  )

  return child
}

async function runProbe(targetUrl) {
  const browser = await chromium.launch({
    headless: true,
    args: ["--disable-dev-shm-usage", "--no-sandbox", "--disable-gpu"],
  })
  const context = await browser.newContext()
  const page = await context.newPage()

  const requests = []

  page.on("request", (request) => {
    requests.push(request)
  })

  const startedAt = Date.now()

  await page.goto(targetUrl, { waitUntil: "domcontentloaded" })
  const domContentLoadedAt = Date.now() - startedAt

  await page.waitForLoadState("networkidle")
  const networkIdleAt = Date.now() - startedAt

  await page.waitForSelector('#dc-companion-host input[type="search"]', {
    state: "visible",
    timeout: 30000,
  })
  const companionInteractiveAt = Date.now() - startedAt

  const resourceSummary = await page.evaluate(() => {
    const entries = performance.getEntriesByType("resource")
    const byType = {}
    for (const entry of entries) {
      const type = entry.initiatorType || "other"
      byType[type] ??= { count: 0, totalTransfer: 0 }
      byType[type].count += 1
      byType[type].totalTransfer += entry.transferSize || 0
    }
    return byType
  })

  await browser.close()

  return {
    domContentLoadedAt,
    networkIdleAt,
    companionInteractiveAt,
    totalRequests: requests.length,
    scriptCount: resourceSummary.script?.count ?? 0,
    scriptTransferBytes: resourceSummary.script?.totalTransfer ?? 0,
  }
}

function printSummary(targetUrl, runs, stats) {
  console.log("\nDeluge Companion Perf Guard")
  console.log(`Target: ${targetUrl}`)
  console.log(`Runs: ${runs.length}`)
  console.log(
    JSON.stringify(
      {
        thresholds: {
          maxMedianInteractiveMs: config.maxMedianInteractiveMs,
          maxMedianDclMs: config.maxMedianDclMs,
          maxAvgRequests: config.maxAvgRequests,
          maxAvgScripts: config.maxAvgScripts,
          maxAvgScriptTransferBytes: config.maxAvgScriptTransferBytes,
        },
        aggregate: stats,
        runs,
      },
      null,
      2,
    ),
  )
}

function evaluateThresholds(stats) {
  const failures = []

  if (stats.medianInteractiveMs > config.maxMedianInteractiveMs) {
    failures.push(
      `median interactive ${stats.medianInteractiveMs}ms > ${config.maxMedianInteractiveMs}ms`,
    )
  }

  if (stats.medianDclMs > config.maxMedianDclMs) {
    failures.push(`median DCL ${stats.medianDclMs}ms > ${config.maxMedianDclMs}ms`)
  }

  if (stats.avgRequests > config.maxAvgRequests) {
    failures.push(
      `average requests ${stats.avgRequests} > ${config.maxAvgRequests}`,
    )
  }

  if (stats.avgScripts > config.maxAvgScripts) {
    failures.push(`average scripts ${stats.avgScripts} > ${config.maxAvgScripts}`)
  }

  if (stats.avgScriptTransferBytes > config.maxAvgScriptTransferBytes) {
    failures.push(
      `average script transfer ${stats.avgScriptTransferBytes}B > ${config.maxAvgScriptTransferBytes}B`,
    )
  }

  return failures
}

async function main() {
  let previewServer
  const baseUrl = config.externalUrl ?? `http://127.0.0.1:${config.port}`
  const targetUrl = makeUrl(baseUrl, config.path)

  try {
    if (!config.externalUrl) {
      previewServer = startPreviewServer(config.port)
      await waitForHttp(baseUrl)
    }

    const runs = []
    for (let i = 0; i < config.runs; i += 1) {
      // eslint-disable-next-line no-await-in-loop
      const result = await runProbe(targetUrl)
      runs.push(result)
    }

    const stats = {
      medianInteractiveMs: Math.round(median(runs.map((run) => run.companionInteractiveAt))),
      medianDclMs: Math.round(median(runs.map((run) => run.domContentLoadedAt))),
      avgRequests: Math.round(average(runs.map((run) => run.totalRequests))),
      avgScripts: Math.round(average(runs.map((run) => run.scriptCount))),
      avgScriptTransferBytes: Math.round(
        average(runs.map((run) => run.scriptTransferBytes)),
      ),
      avgNetworkIdleMs: Math.round(average(runs.map((run) => run.networkIdleAt))),
    }

    printSummary(targetUrl, runs, stats)

    const failures = evaluateThresholds(stats)
    if (failures.length > 0) {
      console.error("\nPerf guard failed:")
      for (const failure of failures) {
        console.error(`- ${failure}`)
      }
      process.exitCode = 1
      return
    }

    console.log("\nPerf guard passed.")
  } finally {
    if (previewServer) {
      previewServer.kill("SIGTERM")
    }
  }
}

main().catch((error) => {
  console.error(error)
  process.exit(1)
})
