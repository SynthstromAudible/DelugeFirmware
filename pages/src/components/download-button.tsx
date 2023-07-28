"use client";

import { useEffect, useState } from "react";

interface Props {
  artifact: string;
  artifact_data: string;
}

export function Download(properties: Props) {
  const { artifact, artifact_data } = properties;
  const [blob, setBlob] = useState("");

  useEffect(() => {
    console.log(artifact_data);
    setBlob(
      URL.createObjectURL(
        new Blob(
          [
            Uint8Array.from(
              atob(artifact_data),
              (m) => m.codePointAt(0) as number,
            ),
          ],
          {
            type: "application/octet-stream",
          },
        ),
      ),
    );
  }, []);

  return (
    <a
      className="p-4 bg-neutral-800 hover:bg-neutral-700 border border-neutral-400 rounded-2xl"
      href={blob}
      target="_blank"
      download={artifact}
    >
      Download {artifact}
    </a>
  );
}

