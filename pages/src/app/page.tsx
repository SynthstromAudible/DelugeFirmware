import CommitList from "../components/commit-list";
import { fromBuffer as zipFromBuffer } from "yauzl";
import {
  getAllInterestingCommits,
  getAllRuns,
  lookupBuildWorkflowId,
  validateConfig,
} from "../data";
import Link from "next/link";

export default async function Home() {
  const h1Style = "font-bold text-2xl py-4";

  // const { config, runs } = params;
  const config = validateConfig();
  const workflowId = await lookupBuildWorkflowId(config);
  const allCommits = await getAllInterestingCommits(config);
  const [communityRuns, prRuns] = await Promise.all([
    getAllRuns(
      config,
      workflowId,
      Array.from(allCommits.filter((c) => typeof c.pr === "undefined")),
    ),
    getAllRuns(
      config,
      workflowId,
      Array.from(allCommits.filter((c) => typeof c.pr !== "undefined")),
    ),
  ]);

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
          <CommitList runData={ communityRuns } />
        </div>

        <h1 className={h1Style} id="build-pr">
          Recent Builds (by PR)
        </h1>

        <div className="max-w-full overflow-x-auto display-contents">
          <CommitList runData={ prRuns } />
        </div>
      </div>
    </main>
  );
}
