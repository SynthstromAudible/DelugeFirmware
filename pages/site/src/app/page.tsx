import dayjs from "dayjs";
import RelativeTime from "dayjs/plugin/relativeTime";
import { Octokit } from "@octokit/core";
import { restEndpointMethods } from "@octokit/plugin-rest-endpoint-methods";
import { components } from "@octokit/openapi-types";
import type { WorkflowArtifact , AllWorkflowRuns } from "../../../fetch-artifacts/dist/index.d.ts";

const image_data: AllWorkflowRuns = require("./df_images.json");

dayjs.extend(RelativeTime);

type WorkflowRun = components["schemas"]["workflow-run"];

const OctokitPlugin = Octokit.plugin(restEndpointMethods);
const Octo = new OctokitPlugin({ auth: process.env.GITHUB_TOKEN });
const REPO_OWNER = "SynthstromAudible";
const REPO_REPO = "DelugeFirmware";

export default async function Home() {
  const h1Style = "font-bold text-2xl py-4";

  return (
    <main className="flex flex-col justify-between">
      <img
        className="w-full h-[200px] object-cover"
        alt="header image showing a Synthstrom Audible Deluge"
        src="https://synthstrom.com/wp-content/uploads/2017/05/deluge-slider-2.jpg"
      ></img>
      <div className="flex flex-col p-4">
        <h1 className={h1Style}>Builds</h1>
        <p>
          Below you can find a list of the most recent community firmware
          builds, for both open PRs and the community main line branch.
        </p>
        <h1 className={h1Style} id="build-community">
          Recent Builds (community mainline)
        </h1>

        <div className="max-w-full overflow-x-auto display-contents">
          <table className="rounded-xl border border-neutral-700 m-auto">
            <thead className="text-xl border-b-2 border-neutral-700">
              <tr>
                <th className="p-4 border border-neutral-700">Commit </th>
                <th className="p-4 border border-neutral-700">Artifacts</th>
              </tr>
            </thead>
            <tbody>
              {image_data.runs.map((run : any) => (
                <tr key={run.id} className="border border-neutral-700">
                  <td className="p-2 max-w-lg">
                    <div className="flex flex-row justify-start align-start">
                      <div className="flex flex-none h-min">
                        <a
                          href={run.commit_url}
                          className="font-mono bg-neutral-800 py-1 px-2 mx-2 rounded-lg"
                        >
                          {run.commit_shortname}
                        </a>
                      </div>
                      <div className="">
                        <span className="mr-1">{run.short_message},</span>
                        <span>{dayjs(run.run_completion_time).fromNow()}</span>
                      </div>
                    </div>
                  </td>
                  <td className="flex flex-col p-2">
                    {Array.from(Object.entries(run.artifacts), ([n, artifact]) => (
                      <div key={n} className="flex">
                        <a
                          className="underline break-words min-w-max"
                          href={`firmware/${(artifact as WorkflowArtifact).assetPath}`}
                        >
                          {n}
                        </a>
                      </div>
                    ))}
                  </td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>

        <h1 className={h1Style} id="build-pr">
          Recent Builds (by PR)
        </h1>
      </div>
    </main>
  );
}
