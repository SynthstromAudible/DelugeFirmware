import {
  BIN_FILE_NAMES,
  getAllInterestingCommits,
  getAllRuns,
  getArtifactData,
  getCommitDetails,
  lookupBuildWorkflowId,
  validateConfig,
} from "../../../data";
import Link from "next/link";

interface Params {
  hash: string;
}

export default async function Page({ params }: { params: Params }) {
  const { hash } = params;

  const config = validateConfig();
  const data = await getCommitDetails(hash);

  return (
    <div className="w-128 border border-neutral-400 rounded shadow p-4 m-auto">
      <div className="flex flex-row">
        <div className="py-2">
          <a
            href={data.html_url}
            className="font-mono bg-neutral-800 p-2 mx-2 rounded-lg"
          >
            {hash.substr(0, 7)}
          </a>
        </div>
        <div className="flex flex-col flex-grow">
          <pre className="px-4 py-2 text-md rounded bg-neutral-800">
            {data.message}
          </pre>
          <div className="flex flex-row justify-between px-8 underline">
            {BIN_FILE_NAMES.map((name) => (
              <Link key={name} className="mx-4" href={`${hash}/${name}`}>
                {name}
              </Link>
            ))}
          </div>
        </div>
      </div>
    </div>
  );
}

export async function generateStaticParams() {
  const config = validateConfig();
  const workflowId = await lookupBuildWorkflowId(config);
  const allCommits = await getAllInterestingCommits(config);
  const allRuns = await getAllRuns(config, workflowId, allCommits);

  return Array.from(allRuns.runs, (run) => {
    return { hash: run.commit_sha };
  });
}
