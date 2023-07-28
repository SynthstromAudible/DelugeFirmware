"use client";

const dayjs = require("dayjs");
import RelativeTime from "dayjs/plugin/relativeTime";
import React from "react";

dayjs.extend(RelativeTime);

export default function CurrentTime(props: { time: string }) {
  return (
    <React.StrictMode>
      <span>{dayjs(props.time).fromNow()}</span>
    </React.StrictMode>
  );
}
