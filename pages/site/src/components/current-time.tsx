"use client";

import * as dayjs from "dayjs";
import RelativeTime from "dayjs/plugin/relativeTime";
import React from "react";

dayjs.extend(RelativeTime);

export default function CurrentTime(props) {
  return (
    <React.StrictMode>
      <span>{dayjs(props.time).fromNow()}</span>
    </React.StrictMode>
  );
}
