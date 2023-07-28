import CurrentTime from "../components/current-time";
import { fromBuffer as zipFromBuffer } from "yauzl";
import { getAllRuns, BIN_FILE_NAMES } from "../data";
import Link from "next/link";

export default async function Home() {
  const h1Style = "font-bold text-2xl py-4";

  // const { config, runs } = params;
  const allRuns = await getAllRuns();

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
              {allRuns.runs.map((run: any) => (
                <tr key={run.id} className="border border-neutral-700">
                  <td className="p-2 max-w-lg">
                    <div className="flex flex-row justify-start align-start">
                      <div className="flex flex-none h-min">
                        <a
                          href={run.commit_url}
                          className="font-mono bg-neutral-800 py-1 px-2 mx-2 rounded-lg"
                        >
                          {run.commit_sha.substr(0, 7)}
                        </a>
                      </div>
                      <div className="">
                        <span className="mr-1">{run.short_message},</span>
                        <CurrentTime time={run.run_completion_time} />
                      </div>
                    </div>
                  </td>
                  <td className="flex flex-col p-2">
                    {Array.from(BIN_FILE_NAMES, (artifactName) => (
                      <div key={artifactName} className="flex">
                        <Link
                          className="underline break-words min-w-max"
                          href={`firmware/${run.commit_sha}/${artifactName}`}
                        >
                          {artifactName}
                        </Link>
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
        <div>
          Coming soon!
        </div>
      </div>
    </main>
  );
}
