import { Octokit } from "@octokit/core";
import { components } from "@octokit/openapi-types";
import { restEndpointMethods } from "@octokit/plugin-rest-endpoint-methods";
import { Stream } from "stream";
import { fromBuffer as zipFromBuffer } from "yauzl";

const OctokitPlugin = Octokit.plugin(restEndpointMethods);
const Octo = new OctokitPlugin({
  auth: process.env.GITHUB_TOKEN,
  request: {
    fetch: (url: string, option: any) => {
      // console.log(url, option);
      return fetch(url, option);
    },
  },
});

type CacheDir = string | undefined;

interface Config {
  owner: string;
  repo: string;
}

const CONFIG = process.env.REPO
  ? {
      owner: process.env.REPO.split('/')[0],
      repo: process.env.REPO.split('/')[1],
    }
  : { owner: undefined, repo: undefined };

export const BIN_FILE_NAMES = [
  "release-7seg.bin",
  "debug-7seg.bin",
  "release-oled.bin",
  "debug-oled.bin",
];

export interface WorkflowArtifact {
  id: number;
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
  commit_sha: string;
  run_completion_time: string;
  short_message: string;
  run_url: string;
}

export interface AllWorkflowRuns {
  config: Config;
  runs: Array<WorkflowRun>;
}

async function fetchArtifact(
  config: Config,
  artifactId: number,
): Promise<ArrayBuffer> {
  let dataArrayBuffer: ArrayBuffer | undefined = undefined;

  for (let i = 0; i < 5; ++i) {
    let remainingRetries = 4 - i;
    try {
      const data = await Octo.rest.actions.downloadArtifact({
        ...config,
        ...{
          artifact_id: artifactId,
          archive_format: "zip",
        },
      });

      dataArrayBuffer = data.data as ArrayBuffer;
    } catch (e) {
      console.warn(
        `Failed to get data for ${artifactId}, ${remainingRetries} retries left: ${e}`,
      );
    }
  }

  return await new Promise((resolve, reject) => {
    let buffer;
    if (dataArrayBuffer) {
      buffer = Buffer.from(dataArrayBuffer as ArrayBuffer);
    } else {
      throw new Error(`Failed to get any data for zip archive ${artifactId}`);
    }
    const zip = zipFromBuffer(
      buffer,
      { lazyEntries: true },
      (err: any, zipfile: any) => {
        if (err) {
          throw err;
        }
        console.log(`${artifactId}: reading retrieved zip file`);
        zipfile.readEntry();
        let foundFile = false;
        zipfile.on("entry", (entry: any) => {
          if (/\.bin$/.test(entry.fileName)) {
            zipfile.openReadStream(
              entry,
              function (err: any, readStream: Stream) {
                if (err) throw err;
                let bufferSegments: Array<Uint8Array> = [];

                readStream.on("data", (chunk: Uint8Array) =>
                  bufferSegments.push(chunk),
                );
                readStream.on("end", () =>
                  resolve(Buffer.concat(bufferSegments)),
                );
                readStream.on("error", (err: any) =>
                  reject(`err converting stream - ${err}`),
                );
              },
            );
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
      },
    );
  });
}

export async function getArtifactData(hash: string, artifactName: string) {
  if (!CONFIG.owner || !CONFIG.repo) {
    throw new Error("Config missing and not found in environment");
  }
  const config = {
    owner: CONFIG.owner!,
    repo: CONFIG.repo!,
  };
  const runs = await Octo.rest.actions
    .listWorkflowRuns({
      ...config,
      ...{ workflow_id: "build.yml", head_sha: hash, conclusion: "success" },
    })
    .then((resp) => resp.data.workflow_runs);
  const mostRecent = runs.reduce(
    (best: any, next: any) =>
      best ? (best.updated_at > next.updated_at ? best : next) : next,
    undefined,
  );
  const artifacts = await Octo.rest.actions
    .listWorkflowRunArtifacts({
      ...config,
      ...{
        run_id: mostRecent.id,
      },
    })
    .then((resp) => resp.data);

  for (const artifact of artifacts.artifacts) {
    if (artifact.name !== artifactName || artifact.expired) {
      continue;
    }

    const data = await fetchArtifact(config, artifact.id);
    const dataView = new Uint8Array(data);
    let binary = "";
    let len = dataView.byteLength;
    for (let i = 0; i < len; i++) {
      binary += String.fromCharCode(dataView[i]);
    }
    return btoa(binary);
  }
  console.log("warning: no matching artifact found");
  return undefined;
}

async function getActionPackageForRun(
  config: Config,
  run: components["schemas"]["workflow-run"],
): Promise<ArtifactMap | undefined> {
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
      id: artifact.id,
      assetPath: `${commitShortname}/${artifact.name}`,
      expired: artifact.expired,
    };
  });

  if (numArtifacts < 4) {
    return undefined;
  }

  return artifactMap;
}

export async function getAllRuns(config?: Config): Promise<AllWorkflowRuns> {
  if (!config) {
    if (!CONFIG.owner || !CONFIG.repo) {
      throw new Error("Config missing and not found in environment");
    }
    config = {
      owner: CONFIG.owner!,
      repo: CONFIG.repo!,
    };
  }
  const workflows = await Octo.rest.actions.listRepoWorkflows({
    ...config,
  });
  const buildWorkflow = workflows.data.workflows.find(
    (workflow) => workflow.name == "Build",
  );
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
  const actionPackages: Array<WorkflowRun | undefined> = await Promise.all(
    workflowRuns.data.workflow_runs.map(async (run) => {
      if (run.head_commit === null) {
        throw new Error("Run head commit was null!");
      }
      const commit = run.head_commit.id;
      const commitUrl = `https://github.com/SynthstromAudible/DelugeFirmware/tree/${commit}`;

      const message = run.head_commit.message.split("\n")[0];
      const artifacts = await getActionPackageForRun(config!, run);

      if (!artifacts) {
        return undefined;
      }

      return {
        id: run.id,
        commit_url: commitUrl,
        commit_sha: commit,
        run_completion_time: run.updated_at,
        short_message: message,
        run_url: run.html_url,
      };
    }),
  );
  return {
    config,
    runs: actionPackages.filter(
      (item) => typeof item !== "undefined",
    ) as Array<WorkflowRun>,
  };
}

export function getCommitDetails(sha: string): Promise<any> {
  if (!CONFIG.owner || !CONFIG.repo) {
    throw new Error("Config missing and not found in environment");
  }
  const config = {
    owner: CONFIG.owner!,
    repo: CONFIG.repo!,
  };

  return Octo.rest.git
    .getCommit({
      ...config,
      ...{
        commit_sha: sha,
      },
    })
    .then((response) => response.data);
}
