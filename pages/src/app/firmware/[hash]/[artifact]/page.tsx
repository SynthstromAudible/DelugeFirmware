import { BIN_FILE_NAMES, getAllRuns, getArtifactData } from "../../../../data";
import { Download } from "../../../../components/download-button";
import { Suspense } from "react";

interface Params {
  hash: string;
  artifact: string;
}

export default async function Page({ params }: { params: Params }) {
  const { hash, artifact } = params;

  const artifactData = await getArtifactData(hash, artifact);

  return (
    <div className="p-24 m-auto">
      <Suspense
        fallback={
          <div>
            We could not render the download button, you probably need to enable
            javascript (sorry)
          </div>
        }
      >
        {artifactData ? (
          <Download artifact={artifact} artifact_data={artifactData} />
        ) : (
          <div>
            Failed to retrieve artifact at site build time, please open an
            issue!
          </div>
        )}
      </Suspense>
    </div>
  );
}

export async function generateStaticParams(arg: any) {
  const allRuns = await getAllRuns();

  return allRuns.runs.flatMap((run) =>
    Array.from(BIN_FILE_NAMES, (fname) => ({
      hash: run.commit_sha,
      artifact: fname,
    })),
  );
}
