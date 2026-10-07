import { execFileSync } from "node:child_process"
import { mkdirSync, writeFileSync } from "node:fs"
import { dirname, resolve } from "node:path"
import { fileURLToPath } from "node:url"

const websiteDirectory = resolve(dirname(fileURLToPath(import.meta.url)), "..")
const repositoryDirectory = resolve(websiteDirectory, "..")
const outputPath = resolve(
  websiteDirectory,
  "src/data/generated/recent-changes.json",
)
const repositoryUrl =
  process.env.CHANGELOG_REPOSITORY_URL ??
  "https://github.com/SynthstromAudible/DelugeFirmware"
const repositoryApiUrl = repositoryUrl.replace(
  "https://github.com/",
  "https://api.github.com/repos/",
)
const scanLimit = 120
const displayLimit = 18

function git(...args) {
  return execFileSync("git", args, {
    cwd: repositoryDirectory,
    encoding: "utf8",
  }).trim()
}

function normalizeSubject(subject) {
  return subject
    .replace(/\s*\(#\d+\)\s*$/, "")
    .replace(
      /^(?:fix|fixed|feature|docs?|chore|refactor)(?:\([^)]*\))?:\s*/i,
      "",
    )
    .trim()
    .toLowerCase()
}

function revertedSubject(subject) {
  const match = subject.match(/^Revert\s+["“](.+?)["”](?:\s+\(#\d+\))?$/i)
  return match?.[1]
}

async function currentPullRequest(pullRequest) {
  const headers = {
    Accept: "application/vnd.github+json",
    "X-GitHub-Api-Version": "2022-11-28",
  }
  if (process.env.GITHUB_TOKEN) {
    headers.Authorization = `Bearer ${process.env.GITHUB_TOKEN}`
  }

  try {
    const response = await fetch(`${repositoryApiUrl}/pulls/${pullRequest}`, {
      headers,
    })
    if (!response.ok) {
      throw new Error(`GitHub returned ${response.status}`)
    }
    const data = await response.json()
    return { title: data.title, url: data.html_url }
  } catch (error) {
    console.warn(
      `Could not refresh PR #${pullRequest}; using its merge commit title (${error.message}).`,
    )
    return null
  }
}

function changedPaths(hash) {
  return git("diff-tree", "--root", "--no-commit-id", "--name-only", "-r", hash)
    .split("\n")
    .filter(Boolean)
}

function categoryFor(subject, paths) {
  if (
    paths.length > 0 &&
    paths.every(
      (path) => path.startsWith("website/") || path.startsWith("docs/"),
    )
  ) {
    return "Documentation"
  }
  if (
    /^(?:docs?)(?:\([^)]*\))?:|\b(?:docs?|documentation|manual|website)\b/i.test(
      subject,
    )
  ) {
    return "Documentation"
  }
  if (
    /^(?:fix|fixed)(?:\([^)]*\))?\s*[:/-]|\bfix(?:es|ed)?\b|\bbug\b/i.test(
      subject,
    )
  ) {
    return "Fixes"
  }
  if (
    /^(?:add|added|feature)(?:\([^)]*\))?\s*[:/-]|\b(?:add|added|enable|support|introduce)(?:s|d)?\b/i.test(
      subject,
    )
  ) {
    return "Features"
  }
  if (
    /^(?:build|ci|chore|refactor)(?:\([^)]*\))?:|\b(?:bump|cleanup|refactor)\b/i.test(
      subject,
    )
  ) {
    return "Maintenance"
  }
  return "Other changes"
}

function displaySubject(subject) {
  return subject
    .replace(/\s*\(#\d+\)\s*$/, "")
    .replace(
      /^(?:feat(?:ure)?|fix|docs?|chore|refactor)(?:\([^)]*\))?\s*[:/-]\s*/i,
      "",
    )
    .replace(/^./, (character) => character.toUpperCase())
}

const rawLog = git(
  "log",
  "HEAD",
  "--first-parent",
  `-${scanLimit}`,
  "--format=%H%x1f%cI%x1f%s%x1e",
)

const commits = rawLog
  .split("\x1e")
  .map((record) => record.trim())
  .filter(Boolean)
  .map((record) => {
    const [hash, date, subject] = record.split("\x1f")
    const pullRequest = subject.match(/\(#(\d+)\)\s*$/)?.[1]
    return { hash, date, subject, pullRequest }
  })

const revertedSubjects = new Set(
  commits
    .map(({ subject }) => revertedSubject(subject))
    .filter(Boolean)
    .map(normalizeSubject),
)

const activeCandidates = commits
  .filter(({ subject }) => !revertedSubject(subject))
  .filter(({ subject }) => !revertedSubjects.has(normalizeSubject(subject)))
  .slice(0, displayLimit)

const activeCommits = await Promise.all(
  activeCandidates.map(async ({ hash, date, subject, pullRequest }) => {
    const currentPr = pullRequest ? await currentPullRequest(pullRequest) : null
    const currentSubject = currentPr?.title ?? subject
    const paths = changedPaths(hash)
    return {
      hash,
      shortHash: hash.slice(0, 8),
      date,
      title: displaySubject(currentSubject),
      category: categoryFor(currentSubject, paths),
      url:
        currentPr?.url ??
        (pullRequest
          ? `${repositoryUrl}/pull/${pullRequest}`
          : `${repositoryUrl}/commit/${hash}`),
      pullRequest: pullRequest ? Number(pullRequest) : null,
    }
  }),
)

const categoryOrder = [
  "Features",
  "Fixes",
  "Documentation",
  "Maintenance",
  "Other changes",
]
const categoryCounts = Object.fromEntries(
  categoryOrder.map((category) => [
    category,
    activeCommits.filter((commit) => commit.category === category).length,
  ]),
)
const summaryParts = categoryOrder
  .filter((category) => categoryCounts[category] > 0)
  .map((category) => {
    const count = categoryCounts[category]
    const singular = {
      Features: "feature",
      Fixes: "fix",
      Documentation: "documentation update",
      Maintenance: "maintenance update",
      "Other changes": "other change",
    }[category]
    const plural = {
      Features: "features",
      Fixes: "fixes",
      Documentation: "documentation updates",
      Maintenance: "maintenance updates",
      "Other changes": "other changes",
    }[category]
    return `${count} ${count === 1 ? singular : plural}`
  })

const output = {
  branch: "main",
  generatedAt: git("show", "-s", "--format=%cI", "HEAD"),
  head: git("rev-parse", "HEAD"),
  scannedCommitCount: commits.length,
  detectedRevertCount: revertedSubjects.size,
  summary: `The latest activity includes ${new Intl.ListFormat("en", { style: "long", type: "conjunction" }).format(summaryParts)}.`,
  categories: categoryOrder
    .map((name) => ({
      name,
      commits: activeCommits.filter((commit) => commit.category === name),
    }))
    .filter(({ commits: entries }) => entries.length > 0),
}

mkdirSync(dirname(outputPath), { recursive: true })
writeFileSync(outputPath, `${JSON.stringify(output, null, 2)}\n`)
console.log(
  `Generated ${activeCommits.length} recent changes (${revertedSubjects.size} detected reverts excluded).`,
)
