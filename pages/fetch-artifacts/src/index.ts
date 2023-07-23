// This is a rube-goldberg machine of hacks to try and fetch all the firmware
// binaries during build time. This consists of two phases -- a webpack Loader
// which retrieves the list of assets, and a Webpack Plugin that injects assets
// for all of the firmware binaries. The plugin parses the output of the loader
// to add the assets.

import { Octokit } from "@octokit/core";
import { restEndpointMethods } from "@octokit/plugin-rest-endpoint-methods";
import { fromBuffer as zipFromBuffer } from "yauzl";
import { components as OctoComponent } from "@octokit/openapi-types";
import * as path from "path";
import * as fs from "node:fs/promises";

const OctokitPlugin = Octokit.plugin(restEndpointMethods);
const Octo = new OctokitPlugin({ auth: process.env.GITHUB_TOKEN });

type CacheDir = string | undefined;

interface Config {
  owner: string;
  repo: string;
}

export interface WorkflowArtifact {
  url: string;
  assetPath: string;
  expired: boolean;
  data?: Buffer;
}

interface ArtifactMap {
  [id: string]: WorkflowArtifact;
}

interface WorkflowRun {
  id: number;
  commit_url: string;
  commit_shortname: string;
  run_completion_time: string;
  short_message: string;
  run_url: string;
  artifacts: ArtifactMap;
}

export interface AllWorkflowRuns {
  config: Config;
  runs: Array<WorkflowRun>;
}

async function fetchArtifact(
  cacheDir: CacheDir,
  config: Config,
  artifact: WorkflowArtifact,
): Promise<Required<WorkflowArtifact>> {
  let dataArrayBuffer: ArrayBuffer | undefined = undefined;
  let cachePath = undefined;
  if (cacheDir) {
    cachePath = path.join(cacheDir, artifact.assetPath);

    try {
      artifact.data = await fs.readFile(cachePath);
      console.log(`Cache hit for ${artifact.assetPath}`);
      return artifact as Required<WorkflowArtifact>;
    } catch (e) {
      // nothing to do, the rest of this function will try to find the artifact
    }
  }

  console.log("Get asset " + artifact.assetPath);
  for (let i = 0; i < 5; ++i) {
    let remainingRetries = 4 - i;
    try {
      const data = await fetch(artifact.url, {
        headers: {
          Accept: "application/vnd.github+json",
          Authorization: `Bearer ${process.env.GITHUB_TOKEN}`,
          "X-GitHub-Api-Version": "2022-11-28",
        },
      }).then((response) => {
        if (!response.ok) {
          throw new Error(`Failed to download: ${response.statusText}`);
        }
        return response;
      });

      dataArrayBuffer = await data.arrayBuffer();
    } catch (e) {
      console.warn(
        `Failed to get data for ${artifact.assetPath}, ${remainingRetries} retries left: ${e}`,
      );
    }
  }

  artifact.data = await new Promise((resolve, reject) => {
    let buffer;
    if (dataArrayBuffer) {
      buffer = Buffer.from(dataArrayBuffer as ArrayBuffer);
    } else {
      throw new Error(`Failed to get any data for zip archive ${artifact.assetPath}`);
    }
    const zip = zipFromBuffer(buffer, { lazyEntries: true }, (err, zipfile) => {
      if (err) {
        throw err;
      }
      console.log("reading retrieved zip file");
      zipfile.readEntry();
      let foundFile = false;
      zipfile.on("entry", (entry) => {
        if (/\.bin$/.test(entry.fileName)) {
          zipfile.openReadStream(entry, function (err, readStream) {
            if (err) throw err;
            let bufferSegments: Array<Uint8Array> = [];

            readStream.on("data", (chunk) => bufferSegments.push(chunk));
            readStream.on("end", () => resolve(Buffer.concat(bufferSegments)));
            readStream.on("error", (err) =>
              reject(`err converting stream - ${err}`),
            );
          });
        } else {
          zipfile.readEntry();
        }
      });
      zipfile.on("end", () => {
        if (!foundFile) {
          reject("Failed to find any matching file");
        }
        zipfile.close();
      });
    });
  });

  if (artifact.data) {
    if (cachePath) {
      await fs.mkdir(path.dirname(cachePath), {
        recursive: true,
      });
      await fs.writeFile(cachePath, artifact.data);
    }

    return artifact as Required<WorkflowArtifact>;
  } else {
    throw new Error(`Artifact data missing from ${artifact.assetPath}`);
  }
}

async function getActionPackageForRun(
  cacheDir: CacheDir,
  config: Config,
  run: OctoComponent["schemas"]["workflow-run"],
): Promise<WorkflowRun | undefined> {
  const artifactsRes = await Octo.rest.actions.listWorkflowRunArtifacts({
    ...config,
    ...{
      run_id: run.id,
    },
  });
  const artifacts = artifactsRes.data;
  let artifactMap: any = {};
  let numArtifacts = 0;

  if (run.head_commit === null) {
    throw new Error("Run head commit was null!");
  }
  const commit = run.head_commit.id;
  const commitUrl = `https://github.com/SynthstromAudible/DelugeFirmware/tree/${commit}`;
  const commitShortname = commit.substring(commit.length - 7, commit.length);

  artifacts.artifacts.forEach((artifact) => {
    if (artifact.name == "all-elfs") {
      // We don't want to rehost the ELF files, develoeprs should know how to
      // find them
      return;
    }
    if (artifact.expired) {
      return;
    }
    numArtifacts += 1;
    artifactMap[artifact.name] = {
      url: artifact.archive_download_url,
      assetPath: `${commitShortname}/${artifact.name}`,
      expired: artifact.expired,
    };
  });

  if (numArtifacts < 4) {
    return undefined;
  }

  await Promise.all(
    Object.values(artifactMap).map((artifact) =>
      fetchArtifact(cacheDir, config, artifact as WorkflowArtifact),
    ),
  );

  const message = run.head_commit.message.split("\n")[0];

  return {
    id: run.id,
    commit_url: commitUrl,
    commit_shortname: commitShortname,
    run_completion_time: run.updated_at,
    short_message: message,
    run_url: run.html_url,
    artifacts: artifactMap,
  };
}

// Get all action runs, returning an object that encapsulates that + the runs that have firmware objects
//
async function getAll(
  cacheDir: CacheDir,
  config: Config,
): Promise<AllWorkflowRuns> {
  const workflows = await Octo.rest.actions.listRepoWorkflows({
    ...config,
  });
  const buildWorkflow = workflows.data.workflows.find(
    (workflow) => workflow.name == "Build",
  );
  console.info("Got workflows");
  if (!buildWorkflow) {
    throw new Error("Failed to find build workflow");
  }
  const workflowRuns = await Octo.rest.actions.listWorkflowRuns({
    ...config,
    ...{
      workflow_id: buildWorkflow.id,
      branch: "community",
      status: "completed",
    },
  });
  console.info("Got runs");
  const actionPackages: Array<WorkflowRun | undefined> = await Promise.all(
    workflowRuns.data.workflow_runs.map((run) =>
      getActionPackageForRun(cacheDir, config, run),
    ),
  );
  return {
    config,
    runs: actionPackages.filter(
      (item) => item !== undefined,
    ) as Array<WorkflowRun>,
  };
}

////////////////////////////////////////////////////////////////////////////////
// Configuration
////////////////////////////////////////////////////////////////////////////////

const CONFIG: Config = {
  owner: "bobtwinkles",
  repo: "DelugeFirmware",
};

////////////////////////////////////////////////////////////////////////////////
// Main
//
// Retrieve all the artifacts, and then shuffle the resulting data in to the
// site directory
////////////////////////////////////////////////////////////////////////////////

// Get all the data
const allRuns = await getAll(process.env.DF_CACHE, CONFIG);

// Write it out to the nextjs `public` directory
const siteBase = "../site/";
await Promise.all(
  allRuns.runs.map((run) =>
    Promise.all(
      Object.values(run.artifacts).map(async (artifact) => {
        const target = path.join(
          siteBase,
          `public/firmware/${artifact.assetPath}`,
        );

        if (artifact.data) {
          await fs.mkdir(path.dirname(target), {
            recursive: true,
          });
          await fs.writeFile(target, artifact.data);
        } else {
          throw new Error(`Artifact ${artifact.assetPath} is missing data`);
        }
      }),
    ),
  ),
);
// And write the metadata to the page file
for (const run of allRuns.runs) {
  for (const artifact of Object.values(run.artifacts)) {
    // Throw away the actual data so it's not serialized in to the JSON
    delete artifact.data;
  }
}
await fs.writeFile(
  path.join(siteBase, "src/app/df_images.json"),
  JSON.stringify(allRuns),
);
